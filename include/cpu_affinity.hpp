#pragma once

#include <thread>
#include <vector>

namespace cpu_affinity {

// Logical CPU IDs, not physical core IDs. Query results are sorted and unique.
using CpuList = std::vector<unsigned int>;

// All operations target the CALLING thread, not every thread in the process.
// Native failures throw std::system_error. Invalid lists throw
// std::invalid_argument. Windows multi-processor-group systems are unsupported.
CpuList get_current_thread_affinity();
// Linux can silently intersect the requested list with online CPUs and cpuset
// restrictions. Query again to obtain the effective mask after setting it.
void set_current_thread_affinity(const CpuList& cpus);
// A snapshot only: the thread may subsequently migrate within its allowed set.
unsigned int current_cpu();

// Temporarily change the calling thread's affinity. Must remain on that thread.
// Destruction restores best-effort without throwing; call restore() explicitly
// when restoration failures must be reported.
class ScopedAffinity final {
public:
    explicit ScopedAffinity(const CpuList& cpus);
    ~ScopedAffinity() noexcept;

    ScopedAffinity(const ScopedAffinity&) = delete;
    ScopedAffinity& operator=(const ScopedAffinity&) = delete;
    ScopedAffinity(ScopedAffinity&&) = delete;
    ScopedAffinity& operator=(ScopedAffinity&&) = delete;

    void restore(); // Idempotent after a successful restore.
    const CpuList& original() const noexcept { return original_; }
    bool active() const noexcept { return active_; }

private:
    std::thread::id owner_;
    CpuList original_;
    bool active_ = false;
};

} // namespace cpu_affinity