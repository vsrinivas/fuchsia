// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <benchmark/benchmark.h>
#include <launchpad/launchpad.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace {

constexpr char pname[] = "bench-process";
constexpr char tname[] = "bench-thread";

// The function is the entry point for the child process. It is copied into the child process via
// zx_vmo_write() so it must have no dependencies (other than zx_thread_exit()).
void call_exit(zx_handle_t unused, uintptr_t thread_exit_addr) {
  __typeof(zx_thread_exit)* thread_exit = (__typeof(zx_thread_exit)*)thread_exit_addr;
  (*thread_exit)();
}

// Computes the stack pointer. Modeled after zircon/stack.h.
uintptr_t compute_stack_pointer(uintptr_t stack_base, size_t stack_size) {
  uintptr_t sp = stack_base + stack_size;
  sp &= -16;
#ifdef __x86_64__
  sp -= 8;
#elif defined(__arm__) || defined(__aarch64__)
#else
#error unknown machine
#endif
  return sp;
}

class Process : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override;

 protected:
  // Initializes a minimal process that when started simply calls zx_thread_exit.
  //
  // Should be called once per benchmark iteration after zx_process_create(), but before
  // zx_process_start().
  bool InitChildProcess(benchmark::State& state);

  // Closes handles and frees resources.
  //
  // Should be called once per benchmark iteration.
  bool CloseHandles(benchmark::State& state);

  // Offset of the zx_thread_exit() syscall from the start of the vDSO.
  uintptr_t thread_exit_offset = 0;
  // Base address of child process's stack. Also serves as process entry point.
  zx_vaddr_t stack_base = 0;
  uintptr_t sp = 0;
  // Address in child process of zx_thread_exit() syscall.
  uintptr_t thread_exit_addr = 0;
  zx_handle_t proc_handle = ZX_HANDLE_INVALID;
  zx_handle_t vmar_handle = ZX_HANDLE_INVALID;
  zx_handle_t thread_handle = ZX_HANDLE_INVALID;
  zx_handle_t stack_vmo = ZX_HANDLE_INVALID;
  zx_handle_t vdso_vmo = ZX_HANDLE_INVALID;
  zx_handle_t channel = ZX_HANDLE_INVALID;
  zx_handle_t channel_to_transfer = ZX_HANDLE_INVALID;
};

void Process::SetUp(benchmark::State& state) {
  // The child process will simply call zx_thread_exit() so we need to know the address of the
  // syscall in the child's addres space. We'll compute that by finding its offset in the vDSO and
  // later adding the offset to the vDSO's base address.
  Dl_info dl_info;
  if (dladdr(reinterpret_cast<void*>(&zx_thread_exit), &dl_info) == 0) {
    state.SkipWithError("Failed to get address of syscall");
    return;
  }
  thread_exit_offset = (uintptr_t)dl_info.dli_saddr - (uintptr_t)dl_info.dli_fbase;
}

bool Process::InitChildProcess(benchmark::State& state) {
  // Initialization of the child process is modeled after mini-process.

  // In order to make a syscall, the child needs to have the vDSO mapped. Launchpad makes this
  // easy. Use launchpad to map the vDSO into the child process and compute the address of
  // zx_thread_exit(). Since launchpad takes ownership of the handles passed to
  // launchpad_create_with_process, duplicate them first so we can destroy the launchpad once the
  // vDSO is mapped.
  zx_handle_t lp_proc_handle = ZX_HANDLE_INVALID;
  if (zx_handle_duplicate(proc_handle, ZX_RIGHT_SAME_RIGHTS, &lp_proc_handle) != ZX_OK) {
    state.SkipWithError("Failed to duplicate proc_handle");
    return false;
  }
  zx_handle_t lp_vmar_handle = ZX_HANDLE_INVALID;
  if (zx_handle_duplicate(vmar_handle, ZX_RIGHT_SAME_RIGHTS, &lp_vmar_handle) != ZX_OK) {
    state.SkipWithError("Failed to duplicate vmar_handle");
    return false;
  }
  launchpad_t* lp = NULL;
  if (launchpad_create_with_process(lp_proc_handle, lp_vmar_handle, &lp) != ZX_OK) {
    state.SkipWithError("Failed to create launchpad");
    return false;
  }
  if (launchpad_get_vdso_vmo(&vdso_vmo) != ZX_OK) {
    state.SkipWithError("Failed to get vDSO");
    return false;
  }
  zx_vaddr_t vdso_base;
  if (launchpad_elf_load_extra(lp, vdso_vmo, &vdso_base, NULL) != ZX_OK) {
    state.SkipWithError("Failed to load vDSO");
    return false;
  }
  launchpad_destroy(lp);
  thread_exit_addr = vdso_base + thread_exit_offset;

  // The child process needs a stack and some code to execute. Create a stack and copy the body of
  // call_exit() to the bottom of the stack.
  constexpr uint32_t stack_perm =
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE;
  // Must be larger than the machine code of call_exit() and smaller than the stack.
  constexpr size_t num_to_copy = 1024;
  constexpr uint64_t stack_size = 4096;
  if (zx_vmo_create(stack_size, 0, &stack_vmo) != ZX_OK) {
    state.SkipWithError("Failed to create vmo");
    return false;
  }
  size_t actual = 0;
  if (zx_vmo_write(stack_vmo, reinterpret_cast<void*>(&call_exit), 0, num_to_copy, &actual) !=
      ZX_OK) {
    state.SkipWithError("Failed to write vmo");
    return false;
  }
  if (actual != num_to_copy) {
    state.SkipWithError("Failed to fully write vmo");
    return false;
  }
  if (zx_vmar_map(vmar_handle, 0, stack_vmo, 0, stack_size, stack_perm, &stack_base) != ZX_OK) {
    state.SkipWithError("Failed to map vmo");
    return false;
  }
  sp = compute_stack_pointer(stack_base, stack_size);

  // The child process needs a thread.
  if (zx_thread_create(proc_handle, tname, sizeof(tname), 0, &thread_handle) != ZX_OK) {
    state.SkipWithError("Failed to create thread");
    return false;
  }

  // It will also need a channel to its parent even though it won't use it.
  if (zx_channel_create(0, &channel, &channel_to_transfer) != ZX_OK) {
    state.SkipWithError("Failed to create channel");
    return false;
  }

  return true;
}

bool Process::CloseHandles(benchmark::State& state) {
  if (proc_handle != ZX_HANDLE_INVALID) {
    if (zx_handle_close(proc_handle) != ZX_OK) {
      state.SkipWithError("Failed to close proc_handle");
      return false;
    }
    proc_handle = ZX_HANDLE_INVALID;
  }
  if (vmar_handle != ZX_HANDLE_INVALID) {
    if (zx_handle_close(vmar_handle) != ZX_OK) {
      state.SkipWithError("Failed to close vmar_handle");
      return false;
    }
    vmar_handle = ZX_HANDLE_INVALID;
  }
  if (thread_handle != ZX_HANDLE_INVALID) {
    if (zx_handle_close(thread_handle) != ZX_OK) {
      state.SkipWithError("Failed to close thread_handle");
      return false;
    }
    thread_handle = ZX_HANDLE_INVALID;
  }
  if (stack_vmo != ZX_HANDLE_INVALID) {
    if (zx_handle_close(stack_vmo) != ZX_OK) {
      state.SkipWithError("Failed to close stack_vmo");
      return false;
    }
    stack_vmo = ZX_HANDLE_INVALID;
  }
  if (vdso_vmo != ZX_HANDLE_INVALID) {
    if (zx_handle_close(vdso_vmo) != ZX_OK) {
      state.SkipWithError("Failed to close vdso_vmo");
      return false;
    }
    vdso_vmo = ZX_HANDLE_INVALID;
  }
  if (channel != ZX_HANDLE_INVALID) {
    if (zx_handle_close(channel) != ZX_OK) {
      state.SkipWithError("Failed to close channel");
      return false;
    }
    channel = ZX_HANDLE_INVALID;
  }
  if (channel_to_transfer != ZX_HANDLE_INVALID) {
    if (zx_handle_close(channel_to_transfer) != ZX_OK) {
      state.SkipWithError("Failed to close channel_to_transfer");
      return false;
    }
    channel_to_transfer = ZX_HANDLE_INVALID;
  }

  return true;
}

// This benchmark measures zx_create_process(). Note, the process is not started.
BENCHMARK_F(Process, Create)(benchmark::State& state) {
  zx_handle_t job = zx_job_default();
  while (state.KeepRunning()) {
    if (zx_process_create(job, pname, sizeof(pname), 0, &proc_handle, &vmar_handle) != ZX_OK) {
      state.SkipWithError("Failed to create process");
      return;
    }
    state.PauseTiming();
    if (!CloseHandles(state)) {
      return;
    }
    state.ResumeTiming();
  }
}

// This benchmark measures zx_start_process().
BENCHMARK_F(Process, Start)(benchmark::State& state) {
  zx_handle_t job = zx_job_default();
  while (state.KeepRunning()) {
    state.PauseTiming();
    if (zx_process_create(job, pname, sizeof(pname), 0, &proc_handle, &vmar_handle) != ZX_OK) {
      state.SkipWithError("Failed to create process");
      return;
    }
    if (!InitChildProcess(state)) {
      return;
    }
    state.ResumeTiming();
    if (zx_process_start(proc_handle, thread_handle, stack_base, sp, channel_to_transfer,
                         thread_exit_addr) != ZX_OK) {
      state.SkipWithError("Failed to start");
      return;
    }
    state.PauseTiming();
    channel_to_transfer = ZX_HANDLE_INVALID;
    if (zx_object_wait_one(thread_handle, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, NULL) != ZX_OK) {
      state.SkipWithError("Failed to wait on child");
      return;
    }
    if (!CloseHandles(state)) {
      return;
    }
    state.ResumeTiming();
  }
}

// This benchmark measures creating and starting a minimal process. Note, it does not wait for
// for the process to terminate.
BENCHMARK_F(Process, CreateStart)(benchmark::State& state) {
  zx_handle_t job = zx_job_default();
  while (state.KeepRunning()) {
    if (zx_process_create(job, pname, sizeof(pname), 0, &proc_handle, &vmar_handle) != ZX_OK) {
      state.SkipWithError("Failed to create process");
      return;
    }
    state.PauseTiming();
    if (!InitChildProcess(state)) {
      return;
    }
    state.ResumeTiming();
    if (zx_process_start(proc_handle, thread_handle, stack_base, sp, channel_to_transfer,
                         thread_exit_addr) != ZX_OK) {
      state.SkipWithError("Failed to start");
      return;
    }
    state.PauseTiming();
    channel_to_transfer = ZX_HANDLE_INVALID;
    if (zx_object_wait_one(thread_handle, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, NULL) != ZX_OK) {
      state.SkipWithError("Failed to wait on child");
      return;
    }
    if (!CloseHandles(state)) {
      return;
    }
    state.ResumeTiming();
  }
}

// This benchmark measures creating, starting, and waiting for completion of a minimal process.
BENCHMARK_F(Process, CreateStartWait)(benchmark::State& state) {
  zx_handle_t job = zx_job_default();
  while (state.KeepRunning()) {
    if (zx_process_create(job, pname, sizeof(pname), 0, &proc_handle, &vmar_handle) != ZX_OK) {
      state.SkipWithError("Failed to create process");
      return;
    }
    state.PauseTiming();
    if (!InitChildProcess(state)) {
      return;
    }
    state.ResumeTiming();
    if (zx_process_start(proc_handle, thread_handle, stack_base, sp, channel_to_transfer,
                         thread_exit_addr) != ZX_OK) {
      state.SkipWithError("Failed to start");
      return;
    }
    channel_to_transfer = ZX_HANDLE_INVALID;
    if (zx_object_wait_one(thread_handle, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, NULL) != ZX_OK) {
      state.SkipWithError("Failed to wait on child");
      return;
    }
    state.PauseTiming();
    if (!CloseHandles(state)) {
      return;
    }
    state.ResumeTiming();
  }
}

}  // namespace
