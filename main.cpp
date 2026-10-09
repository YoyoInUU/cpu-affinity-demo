#include "cpu_affinity.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
    cpu_affinity::CpuList cpus;
    unsigned int seconds = 2;
};

void print_help() {
    std::cout << "CPU affinity demo (current thread, logical CPU IDs)\n\n"
              << "Usage:\n"
              << "  cpu_affinity_demo [--list]\n"
              << "  cpu_affinity_demo --cpus N[,N...] [--seconds N]\n"
              << "  cpu_affinity_demo --help\n\n"
              << "--list       Show the calling thread's current allowed CPUs.\n"
              << "--cpus       Temporarily bind to CPUs from the displayed list.\n"
              << "--seconds    Run a CPU-bound demo for 1-60 seconds (default: 2).\n"
              << "Affinity is restored after the demo. No administrator access needed\n"
              << "under normal conditions. Windows requires a single processor group.\n";
}

unsigned int parse_number(std::string_view text, const char* name) {
    unsigned int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);

    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument(std::string(name) + " must be an unsigned integer");
    }

    return value;
}

cpu_affinity::CpuList parse_cpus(std::string_view text) {
    cpu_affinity::CpuList cpus;

    for (;;) {
        const auto comma = text.find(',');
        cpus.push_back(parse_number(text.substr(0, comma), "CPU ID"));

        if (comma == std::string_view::npos) {
            break;
        }

        text.remove_prefix(comma + 1);
    }

    std::sort(cpus.begin(), cpus.end());

    if (std::adjacent_find(cpus.begin(), cpus.end()) != cpus.end()) {
        throw std::invalid_argument("Duplicate CPU IDs are not allowed");
    }

    return cpus;
}

Options parse_options(int argc, char** argv) {
    Options options;
    bool has_cpus = false;
    bool has_seconds = false;
    bool has_list = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--list") {
            if (has_list) {
                throw std::invalid_argument("--list specified more than once");
            }

            has_list = true;
        } else if (arg == "--cpus" || arg == "--seconds") {
            if (++i >= argc) {
                throw std::invalid_argument(std::string(arg) + " requires a value");
            }

            if (arg == "--cpus") {
                if (has_cpus) {
                    throw std::invalid_argument("--cpus specified more than once");
                }

                options.cpus = parse_cpus(argv[i]);
                has_cpus = true;
            } else {
                if (has_seconds) {
                    throw std::invalid_argument("--seconds specified more than once");
                }

                options.seconds = parse_number(argv[i], "Seconds");

                if (options.seconds < 1 || options.seconds > 60) {
                    throw std::invalid_argument("--seconds must be between 1 and 60");
                }

                has_seconds = true;
            }
        } else {
            throw std::invalid_argument("Unknown option: " + std::string(arg));
        }
    }

    if (has_list && (has_cpus || has_seconds)) {
        throw std::invalid_argument("--list cannot be combined with workload options");
    }

    if (has_seconds && !has_cpus) {
        throw std::invalid_argument("--seconds requires --cpus");
    }

    return options;
}

void print_cpus(const cpu_affinity::CpuList& cpus) {
    for (std::size_t i = 0; i < cpus.size(); ++i) {
        if (i != 0) {
            std::cout << ',';
        }

        std::cout << cpus[i];
    }

    std::cout << '\n';
}

void run_workload(unsigned int seconds) {
    using Clock = std::chrono::steady_clock;

    const auto start = Clock::now();
    const auto deadline = start + std::chrono::seconds(seconds);

    std::uint64_t checksum = 0x123456789abcdefULL;
    std::uint64_t iterations = 0;
    std::set<unsigned int> observed;

    do {
        // Unsigned xorshift arithmetic: defined overflow, a visible checksum,
        // and bounded chunks so the workload cannot be optimized away.
        for (unsigned int i = 0; i < 4096; ++i) {
            checksum ^= checksum << 13;
            checksum ^= checksum >> 7;
            checksum ^= checksum << 17;
        }

        iterations += 4096;
        observed.insert(cpu_affinity::current_cpu());
    } while (Clock::now() < deadline);

    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();

    std::cout << "Sampled execution CPUs: ";
    print_cpus(cpu_affinity::CpuList(observed.begin(), observed.end()));

    std::cout << "Elapsed: " << std::fixed << std::setprecision(3) << elapsed
              << " s, iterations: " << iterations << ", checksum: " << checksum << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            print_help();
            return 0;
        }

        const auto options = parse_options(argc, argv);
        const auto before = cpu_affinity::get_current_thread_affinity();

        std::cout << "Initial thread affinity (logical CPU IDs): ";
        print_cpus(before);
        std::cout << "Current CPU: " << cpu_affinity::current_cpu() << '\n';

        if (options.cpus.empty()) {
            return 0;
        }

        // The demo only narrows the initial mask. The library itself permits
        // expansion when the OS/process/cgroup policy allows it.
        for (const auto cpu : options.cpus) {
            if (!std::binary_search(before.begin(), before.end(), cpu)) {
                throw std::invalid_argument("CPU " + std::to_string(cpu) +
                                            " is outside the initial affinity; use --list");
            }
        }

        {
            cpu_affinity::ScopedAffinity affinity(options.cpus);

            std::cout << "Effective pinned affinity: ";
            print_cpus(cpu_affinity::get_current_thread_affinity());

            std::cout << "Running for " << options.seconds << " second(s)...\n";
            run_workload(options.seconds);

            affinity.restore(); // Report errors instead of relying on the destructor.
        }

        std::cout << "Restored thread affinity: ";
        print_cpus(cpu_affinity::get_current_thread_affinity());
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "Error: " << error.what() << "\nRun with --help for usage.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}