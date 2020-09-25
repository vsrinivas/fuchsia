// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug_info_retriever.h"

#include <lib/syslog/cpp/macros.h>

#include <list>

namespace component {

namespace {
struct thread_entry {
  zx_koid_t id;
  zx::thread thread;
  zx::suspend_token suspend_token;
  fbl::String name;
};
}  // namespace

fbl::String DebugInfoRetriever::GetInfo(const zx::process* process, zx_koid_t* thread_ids,
                                        size_t num) {
  assert(process);
  zx_koid_t thread_ids_storage[kMaxThreads];

  if (thread_ids == nullptr || num == 0) {
    return "ERROR (fxbug.dev/4687): Full thread dump disabled";
    if (process->get_info(ZX_INFO_PROCESS_THREADS, &thread_ids_storage,
                          kMaxThreads * sizeof(zx_koid_t), &num, nullptr) != ZX_OK) {
      return "ERROR: failed to get threads for process";
    };
    thread_ids = thread_ids_storage;
  }

  std::list<thread_entry> threads;
  zx_status_t status;

  for (size_t i = 0; i < num; i++) {
    zx::thread thread;
    if (process->get_child(thread_ids[i], ZX_RIGHT_SAME_RIGHTS, &thread) == ZX_OK) {
      threads.push_back({.id = thread_ids[i], .thread = std::move(thread)});
    };
  }

  for (auto it = threads.begin(); it != threads.end();) {
    zx_info_thread_t thread_info;
    it->thread.get_info(ZX_INFO_THREAD, &thread_info, sizeof(thread_info), nullptr, nullptr);

    auto prev = it++;
    // All threads will resume when their suspend token goes out of scope.
    if ((status = prev->thread.suspend(&prev->suspend_token)) != ZX_OK) {
      FX_LOGS(INFO) << "Failed to suspend thread: " << status;
      threads.erase(prev);
    }
  }

  for (auto it = threads.begin(); it != threads.end();) {
    // Wait for the thread to actually get suspended, but also react if the
    // thread was terminated in between these operations.
    zx_signals_t signals = 0;

    auto prev = it++;
    if ((status = prev->thread.wait_one(ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED,
                                        zx::deadline_after(zx::msec(100)), &signals)) != ZX_OK ||
        (signals & ZX_THREAD_TERMINATED)) {
      FX_LOGS(INFO) << "Thread failed to suspend in time. Status: " << status
                    << ", signals: " << signals;
      threads.erase(prev);
    }
  }

  for (auto& entry : threads) {
    char temp_name[ZX_MAX_NAME_LEN];
    entry.thread.get_property(ZX_PROP_NAME, &temp_name, ZX_MAX_NAME_LEN);
    entry.name = temp_name;
  }

  DsoListWrapper dso(*process);
  FILE* output = tmpfile();
  if (output == nullptr) {
    FX_LOGS(ERROR) << "Failed to open tmpfile for output.";
    return "";
  }

  for (const auto& entry : threads) {
    fprintf(output, "%s (%lu):\n", entry.name.c_str(), entry.id);

    zx_thread_state_general_regs_t regs;
    if ((status = inspector_read_general_regs(entry.thread.get(), &regs)) != ZX_OK) {
      fprintf(output, "ERROR: failed to read regs, code=%d", status);
      continue;
    };
    inspector_print_general_regs(output, &regs, nullptr);

    // Get the program counter, stack, and frame pointers.
    zx_vaddr_t pc, sp, fp;
    const char* arch = "unknown";
#if defined(__x86_64__)
    arch = "x86_64";
    pc = regs.rip;
    sp = regs.rsp;
    fp = regs.rbp;
#elif defined(__aarch64__)
    arch = "aarch64";
    pc = regs.pc;
    sp = regs.sp;
    fp = regs.r[29];
#else
    fprintf(output, "unsupported architecture\n");
    continue;
#endif

    inspector_dso_print_list(output, dso.info);
    fprintf(output, "arch: %s\n", arch);
    inspector_print_backtrace(output, process->get(), entry.thread.get(), dso.info, pc, sp, fp,
                              true);

    fprintf(output, "\n");
  }

  rewind(output);

  std::vector<uint8_t> data;
  if (!files::ReadFileDescriptorToVector(fileno(output), &data)) {
    fclose(output);
    return "ERROR: Failed to read file contents";
  }

  fclose(output);
  return fbl::String((char*)data.data(), data.size());
}

}  // namespace component
