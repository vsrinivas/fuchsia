// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/process.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <atomic>
#include <thread>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <inspector/inspector.h>
#include <pretty/hexdump.h>
#include <task-utils/get.h>
#include <task-utils/walker.h>

static int verbosity_level = 0;

void print_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

void print_zx_error(zx_status_t status, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, ": %d(%s)", status, zx_status_get_string(status));
  fprintf(stderr, "\n");
  va_end(args);
}

// While this should never fail given a valid handle,
// returns ZX_KOID_INVALID on failure.
zx_koid_t get_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  if (zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL) < 0) {
    // This shouldn't ever happen, so don't just ignore it.
    print_error("Eh? ZX_INFO_HANDLE_BASIC failed");
    return ZX_KOID_INVALID;
  }
  return info.koid;
}

// How much memory to dump, in bytes.
// Space for this is allocated on the stack, so this can't be too large.
constexpr size_t kMemoryDumpSize = 256;

void dump_memory(zx_handle_t proc, uintptr_t start, size_t len, FILE* out) {
  // Make sure we're not allocating an excessive amount of stack.
  ZX_DEBUG_ASSERT(len <= kMemoryDumpSize);

  uint8_t buf[len];
  auto res = zx_process_read_memory(proc, start, buf, len, &len);
  if (res < 0) {
    fprintf(out, "failed reading %p memory; error : %d\n", (void*)start, res);
  } else if (len != 0) {
    hexdump_very_ex(buf, len, start, hexdump_stdio_printf, out);
  }
}

void dump_thread(zx_handle_t process, inspector_dsoinfo_t* dso_list, uint64_t tid,
                 zx_handle_t thread, FILE* out) {
  zx_thread_state_general_regs_t regs;
  zx_vaddr_t pc = 0, sp = 0, fp = 0;

  if (inspector_read_general_regs(thread, &regs) != ZX_OK) {
    // Error message has already been printed.
    return;
  }

#if defined(__x86_64__)
  pc = regs.rip;
  sp = regs.rsp;
  fp = regs.rbp;
#elif defined(__aarch64__)
  pc = regs.pc;
  sp = regs.sp;
  fp = regs.r[29];
#else
  // It's unlikely we'll get here as trying to read the regs will likely
  // fail, but we don't assume that.
  printf("unsupported architecture .. coming soon.\n");
  return;
#endif

  char thread_name[ZX_MAX_NAME_LEN];
  auto status = zx_object_get_property(thread, ZX_PROP_NAME, thread_name, sizeof(thread_name));
  if (status < 0) {
    strlcpy(thread_name, "unknown", sizeof(thread_name));
  }

  fprintf(out, "<== Thread %s[%" PRIu64 "] ==>\n", thread_name, tid);

  inspector_print_general_regs(out, &regs, nullptr);

  fprintf(out, "bottom of user stack:\n");
  dump_memory(process, sp, kMemoryDumpSize, out);

  inspector_print_backtrace_markup(out, process, thread, dso_list, pc, sp, fp);

  if (verbosity_level >= 1)
    fprintf(out, "Done handling thread %" PRIu64 ".%" PRIu64 ".\n", get_koid(process),
            get_koid(thread));
}

void dump_all_threads(uint64_t pid, zx_handle_t process, FILE* out) {
  // First get the thread count so that we can allocate an appropriately
  // sized buffer. This is racy but it's the nature of the beast.
  size_t num_threads;
  zx_status_t status =
      zx_object_get_info(process, ZX_INFO_PROCESS_THREADS, nullptr, 0, nullptr, &num_threads);
  if (status != ZX_OK) {
    print_zx_error(status, "failed to get process thread info (#threads)");
    exit(1);
  }

  auto threads = std::unique_ptr<zx_koid_t[]>(new zx_koid_t[num_threads]);
  size_t records_read;
  status = zx_object_get_info(process, ZX_INFO_PROCESS_THREADS, threads.get(),
                              num_threads * sizeof(threads[0]), &records_read, nullptr);
  if (status != ZX_OK) {
    print_zx_error(status, "failed to get process thread info");
    exit(1);
  }
  ZX_DEBUG_ASSERT(records_read == num_threads);

  fprintf(out, "%zu thread(s)\n", num_threads);

  inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(process);
  inspector_print_markup_context(out, process);

  // TODO(dje): Move inspector's DebugInfoCache here, so that we can use it
  // across all threads.

  for (size_t i = 0; i < num_threads; ++i) {
    zx_koid_t tid = threads[i];
    zx_handle_t thread;
    // TODO(dje): There is value in specifying exactly the rights we need,
    // but an explicit list this early has a higher risk of bitrot.
    status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0) {
      fprintf(out, "WARNING: failed to get a handle to [%" PRIu64 ".%" PRIu64 "] : error %d\n", pid,
              tid, status);
      continue;
    }

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    status = zx_task_suspend_token(thread, &suspend_token);
    if (status != ZX_OK) {
      print_zx_error(status, "unable to suspend thread, skipping");
      zx_handle_close(thread);
      continue;
    }

    zx_signals_t observed = 0u;
    // Try to be robust and don't wait forever. The timeout is a little
    // high as we want to work well in really loaded systems.
    auto deadline = zx_deadline_after(ZX_SEC(5));
    // Currently, asking to wait for suspended means only waiting for the
    // thread to suspend. If the thread terminates instead this will wait
    // forever (or until the timeout). Thus we need to explicitly wait for
    // ZX_THREAD_TERMINATED too.
    zx_signals_t signals = ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED;
    status = zx_object_wait_one(thread, signals, deadline, &observed);
    if (status == ZX_OK) {
      if (observed & ZX_THREAD_TERMINATED) {
        fprintf(out, "Unable to print backtrace of thread %" PRIu64 ".%" PRIu64 ": terminated\n",
                pid, tid);
      } else {
        dump_thread(process, dso_list, tid, thread, out);
      }
    } else {
      print_zx_error(status,
                     "failure waiting for thread %" PRIu64 ".%" PRIu64 " to suspend, skipping", pid,
                     tid);
    }

    zx_handle_close(suspend_token);
    zx_handle_close(thread);
  }

  inspector_dso_free_list(dso_list);
}

void dump_process(zx_koid_t pid, zx::unowned_process process, FILE* out) {
  char process_name[ZX_MAX_NAME_LEN];
  auto status = process->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  if (status != ZX_OK) {
    strlcpy(process_name, "unknown", sizeof(process_name));
  }

  // We skip printing serial console's stack as this will cause a hang.
  if (strcmp(process_name, "console.cm") == 0) {
    fprintf(out, "Skipping backtrace of thread in process %" PRIu64 ": %s\n", pid, process_name);
    return;
  }

  fprintf(out, "Backtrace of threads of process %" PRIu64 ": %s\n", pid, process_name);

  dump_all_threads(pid, process->get(), out);
}

int dump_process(zx_koid_t pid) {
  zx::process process;
  zx_obj_type_t type;
  auto status = get_task_by_koid(pid, &type, process.reset_and_get_address());
  if (status != ZX_OK) {
    print_zx_error(status, "unable to get a handle to %" PRIu64, pid);
    return 1;
  }

  if (type != ZX_OBJ_TYPE_PROCESS) {
    print_error("PID %" PRIu64 " is not a process. Threads can only be dumped from processes", pid);
    return 1;
  }

  dump_process(pid, zx::unowned(process), stdout);
  return 0;
}

// Writer that writes to stdout at a throttled rate.
class Writer {
 public:
  static zx::result<std::unique_ptr<Writer>> Create();

  ~Writer() {
    Join();
    fclose(fp_);
  }

  // Signal thread that there is new data to write.
  void Signal();

  // After fp is done being used, |Signal| should be called to notify worker thread of new work.
  FILE* fp() { return fp_; }

  // Joins thread.
  void Join();

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer&& operator=(Writer&) = delete;

 private:
  Writer(FILE* fp, fbl::unique_fd fd, fzl::OwnedVmoMapper mapper, size_t baud = 115200)
      : fp_(fp), fd_(std::move(fd)), mapper_(std::move(mapper)), baud_(baud) {
    thread_ = std::thread([this]() { ThrottledWriteThread(); });
  }

  void ThrottledWriteThread();

  zx::duration BytesToDuration(size_t bytes) {
    constexpr int kBitsPerCharacter = 10;
    return (zx::sec(1) / (baud_ / kBitsPerCharacter)) * bytes;
  }

  FILE* fp_;
  fbl::unique_fd fd_;
  fzl::OwnedVmoMapper mapper_;
  std::thread thread_;
  std::atomic<size_t> offset_ = 0;
  std::atomic<bool> done_ = false;
  sync_completion_t event_;

  const size_t baud_;
};

zx::result<std::unique_ptr<Writer>> Writer::Create() {
  // We create an 1GiB VMO relying on overcommit to prevent any issues. We will decommit pages as
  // they are written out to stdout to prevent buffer from growing too much.
  constexpr size_t kVmoSize = 1024 * 1024 * 1024;
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0, &vmo);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Duplicate vmo.
  zx::vmo dup;
  status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Map vmo.
  fzl::OwnedVmoMapper mapper;
  status = mapper.Map(std::move(dup), 0, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Insert VMO into fd table.
  fdio_t* io;
  status = fdio_create(vmo.release(), &io);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  fbl::unique_fd fd(fdio_bind_to_fd(io, -1, 0));
  if (!fd) {
    fdio_unsafe_release(io);
    return zx::error(ZX_ERR_BAD_STATE);
  }

  FILE* fp = fdopen(fd.get(), "w");
  if (fp == nullptr) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  return zx::ok(new Writer(fp, std::move(fd), std::move(mapper)));
}

void Writer::ThrottledWriteThread() {
  size_t decommit_offset = 0;
  size_t local_offset = 0;
  const size_t sys_page_size = zx_system_get_page_size();
  auto* current = static_cast<const uint8_t*>(mapper_.start());
  zx::time last_deadline = zx::clock::get_monotonic();
  for (;;) {
    sync_completion_wait(&event_, ZX_TIME_INFINITE);
    sync_completion_reset(&event_);
    // Write 1 line at a time, sleeping inbetween lines to achieve throttling.
    while (local_offset < offset_.load()) {
      const size_t file_offset = offset_.load();
      // Find newline.
      size_t newline_offset = 0;
      while (current[newline_offset] != '\n' && local_offset + newline_offset < file_offset) {
        newline_offset++;
      }
      if (current[newline_offset] != '\n') {
        // No newline found. Wait for more data to be found.
        break;
      }
      newline_offset++;
      auto written = fwrite(current, sizeof(uint8_t), newline_offset, stdout);
      if (written != newline_offset) {
        fprintf(stderr, "Write failed\n");
        return;
      }

      current += written;
      local_offset += written;

      // Try to decommit pages we no longer need to lower memory usage.
      if (decommit_offset < fbl::round_down(local_offset, sys_page_size)) {
        ZX_DEBUG_ASSERT(decommit_offset % sys_page_size == 0);
        const size_t size = fbl::round_down(local_offset - decommit_offset, sys_page_size);
        // This is best effort so we don't care if it fails.
        mapper_.vmo().op_range(0, decommit_offset, size, nullptr, 0);
        decommit_offset += size;
      }

      last_deadline += BytesToDuration(newline_offset);
      while (zx::clock::get_monotonic() < last_deadline) {
        zx::nanosleep(last_deadline);
      }
    }
    if (done_.load()) {
      return;
    }
  }
  return;
}

void Writer::Join() {
  done_.store(true);
  sync_completion_signal(&event_);
  thread_.join();
}

void Writer::Signal() {
  fflush(fp_);
  off_t new_offset = ftello(fp_);
  ZX_ASSERT(new_offset >= 0);
  if (static_cast<size_t>(new_offset) > offset_.load()) {
    offset_.store(static_cast<size_t>(new_offset));
    sync_completion_signal(&event_);
  }
}

int dump_all_processes() {
  zx::result<std::unique_ptr<Writer>> status_or_writer = Writer::Create();
  if (status_or_writer.is_error()) {
    fprintf(stderr, "ERROR: unable to create Writer: %s", status_or_writer.status_string());
    return 1;
  }
  auto writer = std::move(status_or_writer.value());

  auto process_callback = [](void* ctx, int depth, zx_handle_t process, zx_koid_t koid,
                             zx_koid_t parent_koid) {
    // Attempting to dump our own process will result in a hang, so we skip it.
    if (process == *zx::process::self()) {
      return ZX_OK;
    }

    auto* writer = static_cast<Writer*>(ctx);
    dump_process(koid, zx::unowned_process(process), writer->fp());
    writer->Signal();
    return ZX_OK;
  };

  auto status = walk_root_job_tree(/*job_callback=*/nullptr, process_callback,
                                   /*thread_callback=*/nullptr, /*ctx=*/writer.get());

  if (status != ZX_OK) {
    fprintf(stderr, "ERROR: unable to walk root job tree: %s", zx_status_get_string(status));
    return 1;
  }

  return 0;
}

void usage(FILE* f) {
  fprintf(f, "Usage: threads [options] [pid]\n");
  fprintf(f, "Options:\n");
  fprintf(f, "  -v[n]           = set verbosity level to N\n");
  fprintf(f,
          "  --all-processes = dump stacks for all processes currently running."
          "This will hang if not invoked via serial console.\n");
}

int main(int argc, char** argv) {
  bool all_processes = false;
  zx_koid_t pid = ZX_KOID_INVALID;

  int i;
  for (i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (arg[0] == '-') {
      if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
        usage(stdout);
        return 0;
      } else if (strcmp(arg, "--all-processes") == 0) {
        all_processes = true;
      } else if (strncmp(arg, "-v", 2) == 0) {
        if (arg[2] != '\0') {
          verbosity_level = atoi(arg + 2);
        } else {
          verbosity_level = 1;
        }
      } else {
        usage(stderr);
        return 1;
      }
    } else {
      break;
    }
  }

  inspector_set_verbosity(verbosity_level);

  zx_handle_t thread_self = thrd_get_zx_handle(thrd_current());
  if (thread_self == ZX_HANDLE_INVALID) {
    print_error("unable to get thread self");
    return 1;
  }

  if (all_processes) {
    return dump_all_processes();
  }

  if (i == argc || i + 1 != argc) {
    usage(stderr);
    return 1;
  }

  char* endptr;
  const char* pidstr = argv[i];
  pid = strtoull(pidstr, &endptr, 0);
  if (!(pidstr[0] != '\0' && *endptr == '\0')) {
    fprintf(stderr, "ERROR: invalid pid: %s", pidstr);
    return 1;
  }

  return dump_process(pid);
}
