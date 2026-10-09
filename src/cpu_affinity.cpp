// Feature macros must precede system headers (also supports direct compilation).
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#else
#error "cpu_affinity supports only Windows and Linux"
#endif

#include "cpu_affinity.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace cpu_affinity {
namespace {

CpuList validate_list(const CpuList& cpus) {
    if (cpus.empty()) {
        throw std::invalid_argument("CPU affinity must contain at least one CPU");
    }
    CpuList sorted = cpus;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        throw std::invalid_argument("CPU affinity must not contain duplicate IDs");
    }
    return sorted;
}

#if defined(_WIN32)

void check_windows_topology() {
    // On Windows 11, a default affinity can span multiple processor groups.
    // GetThreadGroupAffinity reports only the primary group, so treating that
    // result as the full original affinity would make RAII restoration lossy.
    if (GetActiveProcessorGroupCount() != 1 ||
        GetActiveProcessorCount(0) > std::numeric_limits<KAFFINITY>::digits) {
        throw std::system_error(std::make_error_code(std::errc::not_supported),
                                "Windows affinity requires one processor group "
                                "and a CPU mask that fits this executable");
    }
}

[[noreturn]] void throw_windows_error(const char* operation) {
    const DWORD error = GetLastError();
    throw std::system_error(static_cast<int>(error), std::system_category(), operation);
}

#else

struct FreeCpuSet {
    void operator()(cpu_set_t* set) const noexcept { CPU_FREE(set); }
};

struct NativeCpuSet {
    std::size_t size;
    std::unique_ptr<cpu_set_t, FreeCpuSet> data;

    explicit NativeCpuSet(int cpu_count)
        : size(CPU_ALLOC_SIZE(cpu_count)), data(CPU_ALLOC(cpu_count)) {
        if (!data) {
            throw std::bad_alloc();
        }
        CPU_ZERO_S(size, data.get());
    }
};

NativeCpuSet read_native_affinity() {
    // The kernel's mask can be larger than CPU_SETSIZE, even when only a few
    // CPUs are online. Probe instead of truncating the mask to 1024 bits.
    int cpu_count = CPU_SETSIZE;
    for (;;) {
        NativeCpuSet mask(cpu_count);
        if (sched_getaffinity(0, mask.size, mask.data.get()) == 0) {
            return mask;
        }
        const int error = errno;
        if (error != EINVAL || cpu_count > std::numeric_limits<int>::max() / 2) {
            throw std::system_error(error, std::generic_category(), "sched_getaffinity");
        }
        cpu_count *= 2;
    }
}

#endif

} // namespace

CpuList get_current_thread_affinity() {
    CpuList cpus;
#if defined(_WIN32)
    check_windows_topology();
    GROUP_AFFINITY mask{};
    if (!GetThreadGroupAffinity(GetCurrentThread(), &mask)) {
        throw_windows_error("GetThreadGroupAffinity");
    }
    for (unsigned int cpu = 0; cpu < std::numeric_limits<KAFFINITY>::digits; ++cpu) {
        if ((mask.Mask & (KAFFINITY{1} << cpu)) != 0) {
            cpus.push_back(cpu);
        }
    }
#else
    auto mask = read_native_affinity();
    // CPU_ALLOC rounds up, so inspect all allocated bits, not just cpu_count.
    for (std::size_t cpu = 0; cpu < mask.size * CHAR_BIT; ++cpu) {
        if (CPU_ISSET_S(cpu, mask.size, mask.data.get())) {
            cpus.push_back(static_cast<unsigned int>(cpu));
        }
    }
#endif
    if (cpus.empty()) {
        throw std::runtime_error("The operating system returned an empty CPU affinity");
    }
    return cpus;
}

void set_current_thread_affinity(const CpuList& cpus) {
    const auto sorted = validate_list(cpus);
#if defined(_WIN32)
    check_windows_topology();
    if (sorted.back() >= GetActiveProcessorCount(0)) {
        throw std::invalid_argument("CPU ID is outside the active processor group");
    }
    GROUP_AFFINITY mask{};
    mask.Group = 0;
    for (const auto cpu : sorted) {
        mask.Mask |= KAFFINITY{1} << cpu;
    }
    if (!SetThreadGroupAffinity(GetCurrentThread(), &mask, nullptr)) {
        throw_windows_error("SetThreadGroupAffinity");
    }
#else
    auto mask = read_native_affinity();
    if (sorted.back() >= mask.size * CHAR_BIT) {
        throw std::invalid_argument("CPU ID is outside the kernel affinity mask");
    }
    CPU_ZERO_S(mask.size, mask.data.get());
    for (const auto cpu : sorted) {
        CPU_SET_S(cpu, mask.size, mask.data.get());
    }
    if (sched_setaffinity(0, mask.size, mask.data.get()) != 0) {
        throw std::system_error(errno, std::generic_category(), "sched_setaffinity");
    }
    // Linux may silently intersect this mask with online CPUs and cpuset/cgroup
    // restrictions. Query the effective affinity if exact membership matters.
#endif
}

unsigned int current_cpu() {
#if defined(_WIN32)
    check_windows_topology();
    return GetCurrentProcessorNumber();
#else
    const int cpu = sched_getcpu();
    if (cpu < 0) {
        throw std::system_error(errno, std::generic_category(), "sched_getcpu");
    }
    return static_cast<unsigned int>(cpu);
#endif
}

ScopedAffinity::ScopedAffinity(const CpuList& cpus)
    : owner_(std::this_thread::get_id()), original_(get_current_thread_affinity()) {
    set_current_thread_affinity(cpus);
    active_ = true;
}

ScopedAffinity::~ScopedAffinity() noexcept {
    try {
        restore();
    } catch (...) {
        // Destructors must not throw, especially during stack unwinding.
    }
}

void ScopedAffinity::restore() {
    if (!active_) {
        return;
    }
    if (std::this_thread::get_id() != owner_) {
        throw std::logic_error("ScopedAffinity must be restored on its creating thread");
    }
    set_current_thread_affinity(original_);
    active_ = false;
}

} // namespace cpu_affinity