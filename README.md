# CPU Affinity Demo

A lightweight C++17 CPU affinity demo for Windows and Linux. It provides a **calling-thread CPU affinity API**, a command-line demo, and automated tests.

CPU affinity is implemented directly using native operating-system APIs, with **no third-party CPU affinity libraries**.

## 🎯 Project Goals

This project is designed to explore:
- CPU affinity and logical CPU IDs
- C++ systems programming
- Native Windows and Linux thread affinity APIs
- Thread scheduling and CPU migration
- Scoped resource management using RAII
- Affinity restoration during exception unwinding
- Per-thread affinity and worker isolation

The project is educational and is not intended to be a production scheduler or a rigorous performance benchmark.

CPU affinity restricts which logical CPUs the operating system may schedule a thread on. It does not change CPU priority, reserve a core exclusively, or guarantee better performance.

## 🏗️ Architecture

The demo uses one thread: it queries the original affinity, applies the selected CPU list with `ScopedAffinity`, runs a CPU-bound workload while sampling execution CPUs, and restores the original affinity.

`ScopedAffinity` also attempts restoration on scope exit or exception unwinding.

- **Windows:** Uses `GetThreadGroupAffinity()` and `SetThreadGroupAffinity()` within a single processor group, with a mask that fits the executable's native bit width.
- **Linux:** Uses `sched_getaffinity(0, ...)` and `sched_setaffinity(0, ...)` for the calling thread, with dynamically sized CPU masks.

The library validates CPU IDs while building the native affinity mask, avoiding a temporary copy and sort of the input list. The workload still samples the current CPU after each iteration chunk, but skips repeated set lookups when consecutive samples report the same CPU.

### Compilation

**File Structure**

```text
cpu-affinity-demo/
├── CMakeLists.txt
├── include/
│   └── cpu_affinity.hpp
├── src/
│   └── cpu_affinity.cpp
├── test/
│   ├── CMakeLists.txt
│   └── cpu_affinity_tests.cpp
└── main.cpp
```

**Requirements**

- Windows, or Linux running natively or through WSL 2 on Windows
- C++17 compatible compiler
- CMake 3.16+

Native Windows builds support MinGW and MSVC. Linux builds require glibc or GNU-compatible scheduling interfaces.

All build output is kept under `build/`, which is ignored by `.gitignore`. Separate `mingw`, `msvc`, and `linux` subdirectories prevent CMake cache conflicts.

**Build on Linux or Windows using WSL 2**

For WSL 2, run these commands in the **Ubuntu terminal**, not PowerShell, Command Prompt, or Git Bash.

On Ubuntu, install the build tools if needed:
```bash
sudo apt update
sudo apt install build-essential cmake
```

1. Navigate to the project and configure it:
```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
```

2. Build:
```bash
cmake --build build/linux -j
```

3. Run the demo:
```bash
./build/linux/cpu_affinity_demo --list
```

**Build on Windows using MinGW**

Run these commands in PowerShell or Command Prompt with MinGW and CMake available on `PATH`.

1. Configure the project:
```powershell
cmake -S . -B build/mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
```

2. Build:
```powershell
cmake --build build/mingw -j
```

3. Run the demo:
```powershell
.\build\mingw\cpu_affinity_demo.exe --list
```

**Build on Windows using MSVC**

Run these commands in a Visual Studio Developer PowerShell.

1. Configure the project:
```powershell
cmake -S . -B build/msvc
```

2. Build:
```powershell
cmake --build build/msvc --config Release
```

3. Run the demo:
```powershell
.\build\msvc\Release\cpu_affinity_demo.exe --list
```

### Usage

Run `--list` first and choose CPU IDs from the displayed list. **CPU 0 is not guaranteed to be available.**

```powershell
.\build\mingw\cpu_affinity_demo.exe --help
.\build\mingw\cpu_affinity_demo.exe --list
.\build\mingw\cpu_affinity_demo.exe --cpus 0 --seconds 2
.\build\mingw\cpu_affinity_demo.exe --cpus 0,2 --seconds 2
```

On Linux, use `./build/linux/cpu_affinity_demo` instead. For MSVC, use `.\build\msvc\Release\cpu_affinity_demo.exe`.

- Running without arguments is equivalent to `--list` and does not start the workload.
- `--cpus 0,2` allows **one thread** to run on CPU 0 or CPU 2; it does not create two threads.
- `--seconds` must be an integer from 1 to 60 and defaults to 2. The workload consumes approximately one logical CPU while running.
- The CLI only accepts subsets of the initial affinity, avoiding CPUs excluded by process or container policies.
- CPU IDs must be comma-separated nonnegative integers. Duplicates, empty entries, and overflowing values are rejected.

The demo displays the initial and effective affinity, sampled execution CPUs, elapsed time, iteration count, checksum, and restored affinity. These samples are not a complete execution trace, and the workload is a functional demonstration rather than a performance benchmark.

Exit codes:
- `0`: Success
- `2`: Argument or CPU-list error
- `1`: Operating-system or other runtime error

### API Usage

```cpp
#include "cpu_affinity.hpp"

void do_work();

void example() {
    const auto original = cpu_affinity::get_current_thread_affinity();
    cpu_affinity::ScopedAffinity guard({original.front()});

    const unsigned int cpu = cpu_affinity::current_cpu();
    (void)cpu;
    do_work();

    guard.restore(); // Explicit restoration reports failures through exceptions.
} // The destructor attempts restoration if restore() is omitted or work throws.
```

Other CMake targets can link the API using `target_link_libraries(your_target PRIVATE cpu_affinity)`.

| API | Purpose |
| --- | --- |
| `get_current_thread_affinity()` | Returns a sorted, unique list of logical CPU IDs. |
| `set_current_thread_affinity(cpus)` | Sets the calling thread's affinity; the mask may be expanded or narrowed as permitted by OS policy. |
| `current_cpu()` | Returns the CPU executing the thread at that instant; the thread may subsequently migrate. |
| `ScopedAffinity(cpus)` | Saves the original affinity and temporarily applies the requested list. |
| `ScopedAffinity::restore()` | Restores the original setting; repeated calls after a successful restore have no effect. |

Empty lists, duplicate IDs, and IDs outside the mask range throw `std::invalid_argument`. Native API failures throw `std::system_error` with an error code.

`ScopedAffinity` cannot be copied or moved. It must be used and destroyed on its creating thread and must not be operated on concurrently across threads. Destructor restoration is **best-effort** and never throws. Call `restore()` explicitly to report restoration failures, and query the effective mask afterward if needed.

### Testing

Test targets and cases are defined in `test/CMakeLists.txt`.

Run the tests for your build:

```bash
# Linux or WSL 2
ctest --test-dir build/linux --output-on-failure
```

```powershell
# Windows: MinGW
ctest --test-dir build/mingw --output-on-failure

# Windows: MSVC
ctest --test-dir build/msvc -C Release --output-on-failure
```

Tests cover affinity queries, single-CPU binding, unsorted multi-CPU lists and subsets, input-list preservation, nested guards, restoration during exception unwinding, worker isolation, and rejection of cross-thread restoration. Invalid-input tests include empty lists, adjacent and nonadjacent duplicate IDs, and out-of-range IDs in unsorted lists, and verify that affinity remains unchanged. CTest also checks CLI argument validation.

To build without tests, add `-DBUILD_TESTING=OFF` during configuration.

The Release builds and all 15 CTest cases were verified on Windows with MinGW GNU C++ 13.2.0 and on WSL Ubuntu 24.04 with GNU C++ 13.3.0. MSVC builds have not been verified.

### Platform Limitations

- **Windows:** A 64-bit executable typically supports up to 64 logical CPUs within one processor group. Multi-group systems report `not_supported` instead of truncating the CPU list or incorrectly restoring Windows 11's default cross-group affinity. API tests are marked as skipped on such systems.
- **Linux:** The kernel may silently intersect the requested mask with online CPUs and cpuset/cgroup restrictions. A successful `set_current_thread_affinity()` call does not guarantee that every requested ID took effect; query the effective affinity again.
- CPU hotplug and external policy changes may affect the effective affinity and restoration results.
- Modifying your own thread normally requires no administrator or root privileges, but OS rules, process affinity, security policies, and container restrictions still apply.
- In multithreaded programs, call the API inside each worker. Changing the main thread's affinity does not simultaneously modify other existing workers.
- macOS, external PID/process binding, setting every existing thread in a process at once, and cross-group Windows affinity are not supported.

### Native API References

- [Microsoft: GetThreadGroupAffinity](https://learn.microsoft.com/en-us/windows/win32/api/processtopologyapi/nf-processtopologyapi-getthreadgroupaffinity)
- [Microsoft: SetThreadGroupAffinity](https://learn.microsoft.com/en-us/windows/win32/api/processtopologyapi/nf-processtopologyapi-setthreadgroupaffinity)
- [Linux: sched_setaffinity / sched_getaffinity](https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html)

## 📄 License

This project is for educational purposes. No standalone license file is currently included in the repository.

---

**Author**: Yoyo Chang  
**Last Updated**: October 2026  
**C++ Standard**: C++17