// SPDX-License-Identifier: Apache-2.0
// Standalone JIT perf dump utility
// Implements Linux perf jitdump and perfmap protocols for profiling JIT code.
// Reference: linux/tools/perf/Documentation/jitdump-specification.txt
//
// Usage:
//   #include "dump_jit_perf.h"
//   jit_perf_dump::jitdump_recorder recorder;
//   recorder.record_code_load(code_ptr, code_size, "my_jit_kernel");
//
// For perfmap (simpler, text-based):
//   jit_perf_dump::perfmap_recorder recorder;
//   recorder.record_code_load(code_ptr, code_size, "my_jit_kernel");

#ifndef JIT_PERF_DUMP_H
#define JIT_PERF_DUMP_H

#ifdef __linux__

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

namespace jit_perf_dump {

// ============================================================================
// jitdump_recorder: writes jitdump files for `perf inject --jit`
// ============================================================================
// Protocol: Linux perf jitdump specification
//   1. Create file: $DIR/.debug/jit/jit-<pid>.dump
//   2. mmap the file with PROT_EXEC so perf records it
//   3. Write header (magic, version, elf_mach, pid, timestamp)
//   4. For each JIT code: write JIT_CODE_LOAD record
//   5. On close: write JIT_CODE_CLOSE record
//
// Workflow:
//   $ perf record -k 1 ./your_program
//   $ perf inject --jit -i perf.data -o perf.jit.data
//   $ perf report -i perf.jit.data
// ============================================================================

class jitdump_recorder {
public:
    jitdump_recorder()
        : marker_addr_(nullptr)
        , marker_size_(0)
        , fd_(-1)
        , failed_(false)
        , use_tsc_(false)
        , code_index_(0) {}

    ~jitdump_recorder() {
        write_code_close();
        finalize();
    }

    // Non-copyable
    jitdump_recorder(const jitdump_recorder &) = delete;
    jitdump_recorder &operator=(const jitdump_recorder &) = delete;

    // Record a JIT code load event.
    // code: pointer to the generated machine code
    // code_size: size in bytes
    // code_name: descriptive name (e.g. "jit_avx2_conv_fwd")
    bool record_code_load(
            const void *code, size_t code_size, const char *code_name) {
        if (!is_active()) return false;
        return write_code_load(code, code_size, code_name);
    }

    // Manually set whether to use TSC timestamps (default: false).
    // Must be called BEFORE the first record_code_load().
    void set_use_tsc(bool use_tsc) { use_tsc_ = use_tsc; }

private:
    bool is_active() {
        if (fd_ >= 0) return true;
        if (failed_) return false;
        return initialize();
    }

    bool initialize() {
        if (!open_file()) return fail();
        if (!create_marker()) return fail();
        if (!write_header()) return fail();
        return true;
    }

    void finalize() {
        close_file();
        delete_marker();
    }

    bool fail() {
        finalize();
        failed_ = true;
        return false;
    }

    static bool make_dir(const std::string &path) {
        if (path.length() >= PATH_MAX) return false;
        if (mkdir(path.c_str(), 0755) == -1 && errno != EEXIST) return false;
        return true;
    }

    std::string resolve_dump_dir() {
        const char *env = getenv("HOME");
        if (env && env[0]) return env;

        return ".";
    }

    bool open_file() {
        std::string path = resolve_dump_dir();
        if (path.empty()) return false;
        if (path.length() >= PATH_MAX) return false;

        path.reserve(PATH_MAX);

        if (!make_dir(path)) return false;

        path += "/.debug";
        if (!make_dir(path)) return false;

        path += "/jit";
        if (!make_dir(path)) return false;

        path += "/jitperf.XXXXXX";
        if (path.length() >= PATH_MAX) return false;
        if (mkdtemp(&path[0]) == nullptr) return false;

        path += "/jit-" + std::to_string(getpid()) + ".dump";
        if (path.length() >= PATH_MAX) return false;

        fd_ = open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0666);
        if (fd_ == -1) {
            fprintf(stderr,
                    "[jit_perf_dump] cannot open jitdump file '%s': %s\n",
                    path.c_str(), strerror(errno));
            return false;
        }

        return true;
    }

    void close_file() {
        if (fd_ == -1) return;
        close(fd_);
        fd_ = -1;
    }

    // Perf discovers the jitdump file by recording an mmap() call.
    // PROT_EXEC ensures the mmap record is not filtered out.
    bool create_marker() {
        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size == -1) return false;
        marker_size_ = (size_t)page_size;
        void *addr = mmap(nullptr, marker_size_, PROT_READ | PROT_EXEC,
                MAP_PRIVATE, fd_, 0);
        if (addr == MAP_FAILED) return false;
        marker_addr_ = addr;
        return true;
    }

    void delete_marker() {
        if (!marker_addr_) return;
        munmap(marker_addr_, marker_size_);
        marker_addr_ = nullptr;
    }

    static uint64_t get_timestamp(bool use_tsc) {
#if defined(__x86_64__) || defined(_M_X64)
        if (use_tsc) {
            uint32_t hi, lo;
            asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
            return (((uint64_t)hi) << 32) | lo;
        }
#else
        (void)use_tsc;
#endif
        struct timespec ts;
        int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
        if (rc) return 0;
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }

    static pid_t get_tid() {
        return (pid_t)syscall(__NR_gettid);
    }

    bool write_bytes(const void *buf, size_t size) {
        if (failed_) return false;
        ssize_t ret = write(fd_, buf, size);
        if (ret == -1 || (size_t)ret != size) return fail();
        return true;
    }

    bool write_header() {
        struct __attribute__((packed)) {
            uint32_t magic;
            uint32_t version;
            uint32_t total_size;
            uint32_t elf_mach;
            uint32_t pad1;
            uint32_t pid;
            uint64_t timestamp;
            uint64_t flags;
        } h;
        h.magic = 0x4A695444; // JITHEADER_MAGIC ('DTiJ')
        h.version = 1;
        h.total_size = sizeof(h);
#if defined(__x86_64__) || defined(_M_X64)
        h.elf_mach = EM_X86_64;
#elif defined(__aarch64__)
        h.elf_mach = EM_AARCH64;
#else
        h.elf_mach = EM_NONE;
#endif
        h.pad1 = 0;
        h.pid = getpid();
        h.timestamp = get_timestamp(use_tsc_);
        h.flags = use_tsc_ ? 1 : 0;
        return write_bytes(&h, sizeof(h));
    }

    bool write_code_close() {
        if (fd_ < 0 || failed_) return false;
        struct __attribute__((packed)) {
            uint32_t id;
            uint32_t total_size;
            uint64_t timestamp;
        } c;
        c.id = 3; // JIT_CODE_CLOSE
        c.total_size = sizeof(c);
        c.timestamp = get_timestamp(use_tsc_);
        return write_bytes(&c, sizeof(c));
    }

    bool write_code_load(
            const void *code, size_t code_size, const char *code_name) {
        struct __attribute__((packed)) {
            uint32_t id;
            uint32_t total_size;
            uint64_t timestamp;
            uint32_t pid;
            uint32_t tid;
            uint64_t vma;
            uint64_t code_addr;
            uint64_t code_size;
            uint64_t code_index;
        } c;
        c.id = 0; // JIT_CODE_LOAD
        size_t name_len = strlen(code_name) + 1;
        size_t total = sizeof(c) + name_len + code_size;
        c.total_size = static_cast<uint32_t>(total);
        assert(c.total_size > code_size); // overflow check
        c.timestamp = get_timestamp(use_tsc_);
        c.pid = getpid();
        c.tid = get_tid();
        c.vma = c.code_addr = (uint64_t)(uintptr_t)code;
        c.code_size = code_size;
        c.code_index = code_index_++;
        write_bytes(&c, sizeof(c));
        write_bytes(code_name, name_len);
        return write_bytes(code, code_size);
    }

    void *marker_addr_;
    size_t marker_size_;
    int fd_;
    bool failed_;
    bool use_tsc_;
    uint64_t code_index_;
};

// ============================================================================
// perfmap_recorder: writes /tmp/perf-<pid>.map for simple symbol resolution
// ============================================================================
// Protocol: simple text file, one line per symbol:
//   <hex_addr> <hex_size> <name>\n
//
// Workflow:
//   $ perf record ./your_program
//   $ perf report  (symbols auto-resolved from /tmp/perf-<pid>.map)
// ============================================================================

class perfmap_recorder {
public:
    perfmap_recorder() : fp_(nullptr), failed_(false) {}

    ~perfmap_recorder() {
        if (fp_) fclose(fp_);
    }

    // Non-copyable
    perfmap_recorder(const perfmap_recorder &) = delete;
    perfmap_recorder &operator=(const perfmap_recorder &) = delete;

    bool record_code_load(
            const void *code, size_t code_size, const char *code_name) {
        if (!is_active()) return false;
        return write_symbol(code, code_size, code_name);
    }

private:
    bool is_active() {
        if (fp_) return true;
        if (failed_) return false;
        return initialize();
    }

    bool initialize() {
        char fname[PATH_MAX];
        int ret = snprintf(fname, PATH_MAX, "/tmp/perf-%d.map", getpid());
        if (ret < 0 || ret >= PATH_MAX) return fail();

        fp_ = fopen(fname, "w+");
        if (!fp_) return fail();
        setvbuf(fp_, nullptr, _IOLBF, 0); // line-buffered
        return true;
    }

    bool fail() {
        if (fp_) { fclose(fp_); fp_ = nullptr; }
        failed_ = true;
        return false;
    }

    bool write_symbol(
            const void *code, size_t code_size, const char *code_name) {
        int ret = fprintf(fp_, "%llx %llx %s\n",
                (unsigned long long)(uintptr_t)code,
                (unsigned long long)code_size, code_name);
        if (ret < 0) return fail();
        return true;
    }

    FILE *fp_;
    bool failed_;
};

// ============================================================================
// oneDNN-style wrapper API
// ============================================================================
// This mirrors oneDNN's register_jit_code_linux_perf() surface:
//   - default jitdump + perfmap behavior
//   - a single registration entry point
//
// ============================================================================

inline void register_jit_code_linux_perf(const void *code, size_t code_size,
        const char *code_name) {
    static std::mutex m;
    static jitdump_recorder jitdump;
    static perfmap_recorder perfmap;

    std::lock_guard<std::mutex> guard(m);
    jitdump.set_use_tsc(false);
    jitdump.record_code_load(code, code_size, code_name);
    perfmap.record_code_load(code, code_size, code_name);
}

} // namespace jit_perf_dump

#endif // __linux__
#endif // JIT_PERF_DUMP_H
