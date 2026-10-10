#include "cpu_affinity.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace affinity = cpu_affinity;

static_assert(!std::is_copy_constructible<affinity::ScopedAffinity>::value);
static_assert(!std::is_move_constructible<affinity::ScopedAffinity>::value);
static_assert(std::is_nothrow_destructible<affinity::ScopedAffinity>::value);

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Operation>
void require_throws(Operation operation) {
    try {
        operation();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown");
}

int main() {
    affinity::CpuList original;
    try {
        original = affinity::get_current_thread_affinity();
    } catch (const std::system_error& error) {
        if (error.code() == std::errc::not_supported) {
            std::cout << "SKIP: " << error.what() << '\n';
            return 77;
        }
        std::cerr << "Cannot query affinity: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Cannot query affinity: " << error.what() << '\n';
        return 1;
    }

    const affinity::CpuList one_cpu{original.front()};
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"query is nonempty, sorted and unique", [&] {
            require(!original.empty(), "Empty affinity");
            require(std::is_sorted(original.begin(), original.end()), "Unsorted affinity");
            require(std::adjacent_find(original.begin(), original.end()) == original.end(),
                    "Duplicate affinity IDs");
            const auto cpu = affinity::current_cpu();
            require(std::binary_search(original.begin(), original.end(), cpu),
                    "Current CPU not in affinity");
        }},
        {"pin and automatically restore", [&] {
            {
                affinity::ScopedAffinity guard(one_cpu);
                require(guard.active(), "Guard not active");
                require(guard.original() == original, "Incorrect snapshot");
                require(affinity::get_current_thread_affinity() == one_cpu, "Pin failed");
                for (int i = 0; i < 50; ++i) {
                    std::this_thread::yield();
                    require(affinity::current_cpu() == one_cpu.front(), "Ran on another CPU");
                }
            }
            require(affinity::get_current_thread_affinity() == original, "Restore failed");
        }},
        {"accept an unsorted multi-CPU list", [&] {
            auto reversed = original;
            std::reverse(reversed.begin(), reversed.end());
            affinity::ScopedAffinity guard(reversed);
            require(affinity::get_current_thread_affinity() == original, "Multi-CPU set failed");
            guard.restore();
        }},
        {"accept an unsorted subset without modifying the input", [&] {
            affinity::CpuList cpus{original.back()};
            if (original.size() > 1) {
                cpus.push_back(original.front());
            }
            const auto input = cpus;
            auto expected = cpus;
            std::sort(expected.begin(), expected.end());
            affinity::ScopedAffinity guard(cpus);
            require(cpus == input, "CPU list was modified");
            require(affinity::get_current_thread_affinity() == expected, "Subset pin failed");
            guard.restore();
        }},
        {"restore during exception unwinding", [&] {
            require_throws<std::runtime_error>([&] {
                affinity::ScopedAffinity guard(one_cpu);
                throw std::runtime_error("Intentional test exception");
            });
            require(affinity::get_current_thread_affinity() == original, "Unwind restore failed");
        }},
        {"explicit restore is idempotent", [&] {
            affinity::ScopedAffinity guard(one_cpu);
            guard.restore();
            guard.restore();
            require(!guard.active(), "Guard still active");
            require(affinity::get_current_thread_affinity() == original, "Explicit restore failed");
        }},
        {"nested guards restore their own snapshots", [&] {
            affinity::ScopedAffinity outer(one_cpu);
            {
                affinity::ScopedAffinity inner(original);
                require(affinity::get_current_thread_affinity() == original, "Expansion failed");
            }
            require(affinity::get_current_thread_affinity() == one_cpu, "Nested restore failed");
            outer.restore();
        }},
        {"reject empty, duplicate and out-of-range CPU lists", [&] {
            require_throws<std::invalid_argument>([] {
                affinity::set_current_thread_affinity({});
            });
            require_throws<std::invalid_argument>([&] {
                affinity::set_current_thread_affinity({one_cpu.front(), one_cpu.front()});
            });
            require_throws<std::invalid_argument>([&] {
                auto duplicates = original;
                duplicates.push_back(original.front());
                affinity::set_current_thread_affinity(duplicates);
            });
            require_throws<std::invalid_argument>([] {
                affinity::set_current_thread_affinity({std::numeric_limits<unsigned int>::max()});
            });
            require_throws<std::invalid_argument>([&] {
                affinity::set_current_thread_affinity(
                    {std::numeric_limits<unsigned int>::max(), one_cpu.front()});
            });
            require(affinity::get_current_thread_affinity() == original, "Invalid input changed affinity");
        }},
        {"failed guard construction leaves affinity unchanged", [&] {
            require_throws<std::invalid_argument>([] {
                affinity::ScopedAffinity guard({});
            });
            require(affinity::get_current_thread_affinity() == original, "Failed constructor changed affinity");
        }},
        {"worker affinity does not change main thread", [&] {
            std::exception_ptr failure;
            std::thread worker([&] {
                try {
                    const auto worker_before = affinity::get_current_thread_affinity();
                    const affinity::CpuList worker_cpu{worker_before.back()};
                    affinity::ScopedAffinity guard(worker_cpu);
                    require(affinity::get_current_thread_affinity() == worker_cpu, "Worker pin failed");
                    require(affinity::current_cpu() == worker_cpu.front(), "Wrong worker CPU");
                    guard.restore();
                    require(affinity::get_current_thread_affinity() == worker_before, "Worker restore failed");
                } catch (...) {
                    failure = std::current_exception();
                }
            });
            worker.join();
            if (failure) {
                std::rethrow_exception(failure);
            }
            require(affinity::get_current_thread_affinity() == original, "Main thread affinity changed");
        }},
        {"reject restoration from another thread", [&] {
            affinity::ScopedAffinity guard(one_cpu);
            std::exception_ptr failure;
            std::thread worker([&] {
                try {
                    require_throws<std::logic_error>([&] { guard.restore(); });
                } catch (...) {
                    failure = std::current_exception();
                }
            });
            worker.join();
            if (failure) {
                std::rethrow_exception(failure);
            }
            require(guard.active(), "Cross-thread restore deactivated guard");
            guard.restore();
        }}
    };

    unsigned int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            require(affinity::get_current_thread_affinity() == original, "Test leaked its affinity");
            std::cout << "PASS: " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
            try {
                affinity::set_current_thread_affinity(original);
            } catch (const std::exception& restore_error) {
                std::cerr << "Cannot restore test thread: " << restore_error.what() << '\n';
                return 1;
            }
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " API tests passed\n";
    return failures == 0 ? 0 : 1;
}