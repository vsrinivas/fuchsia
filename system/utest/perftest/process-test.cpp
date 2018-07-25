// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <limits.h>
#include <launchpad/launchpad.h>
#include <perftest/perftest.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace {

constexpr char pname[] = "benchmark-process";
constexpr char tname[] = "benchmark-thread";

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

// ProcessFixture is a reusable test fixture for creating a minimal child process.
//
// When started, the child process simply calls zx_thread_exit.
//
// For each iteration, call the following methods in this order:
//     Create();
//     Init();
//     Start();
//     Wait();
//     Close();
class ProcessFixture {
public:
    ProcessFixture();

    // Creates an "empty" child process.
    void Create();

    // Initializes minimal process.
    void Init();

    // Starts the process.
    void Start();

    // Waits for the process to terminate.
    void Wait();

    // Closes handles and frees resources.
    void Close();

private:
    // Offset of the zx_thread_exit() syscall from the start of the vDSO.
    uintptr_t thread_exit_offset_ = 0;

    uintptr_t sp_ = 0;

    // Address in child process of zx_thread_exit() syscall.
    uintptr_t thread_exit_addr_ = 0;
    zx_handle_t proc_handle_ = ZX_HANDLE_INVALID;
    zx_handle_t vmar_handle_ = ZX_HANDLE_INVALID;
    zx_handle_t thread_handle_ = ZX_HANDLE_INVALID;
    zx_handle_t stack_vmo_ = ZX_HANDLE_INVALID;
    zx_handle_t vdso_vmo_ = ZX_HANDLE_INVALID;
    zx_handle_t channel_ = ZX_HANDLE_INVALID;
    zx_handle_t channel_to_transfer_ = ZX_HANDLE_INVALID;
};

ProcessFixture::ProcessFixture() {
    // The child process will simply call zx_thread_exit() so we need to know the address of the
    // syscall in the child's addres space. We'll compute that by finding its offset in the vDSO and
    // later adding the offset to the vDSO's base address.
    Dl_info dl_info;
    ZX_ASSERT(dladdr(reinterpret_cast<void*>(&zx_thread_exit), &dl_info) != 0);
    thread_exit_offset_ = (uintptr_t)dl_info.dli_saddr - (uintptr_t)dl_info.dli_fbase;
}

void ProcessFixture::Create() {
    ZX_ASSERT(zx_process_create(zx_job_default(), pname, sizeof(pname), 0, &proc_handle_,
                                &vmar_handle_) == ZX_OK);
}

void ProcessFixture::Init() {
    // Initialization of the child process is modeled after mini-process.

    // In order to make a syscall, the child needs to have the vDSO mapped.  Launchpad makes this
    // easy. Use launchpad to map the vDSO into the child process and compute the address of
    // zx_thread_exit(). Since launchpad takes ownership of the handles passed to
    // launchpad_create_with_process, duplicate them first so we can destroy the launchpad once the
    // vDSO is mapped.
    zx_handle_t lp_proc_handle = ZX_HANDLE_INVALID;
    ZX_ASSERT(zx_handle_duplicate(proc_handle_, ZX_RIGHT_SAME_RIGHTS, &lp_proc_handle) == ZX_OK);

    zx_handle_t lp_vmar_handle = ZX_HANDLE_INVALID;
    ZX_ASSERT(zx_handle_duplicate(vmar_handle_, ZX_RIGHT_SAME_RIGHTS, &lp_vmar_handle) == ZX_OK);

    launchpad_t* lp = NULL;
    ZX_ASSERT(launchpad_create_with_process(lp_proc_handle, lp_vmar_handle, &lp) == ZX_OK);

    ZX_ASSERT(launchpad_get_vdso_vmo(&vdso_vmo_) == ZX_OK);

    zx_vaddr_t vdso_base;
    ZX_ASSERT(launchpad_elf_load_extra(lp, vdso_vmo_, &vdso_base, NULL) == ZX_OK);

    launchpad_destroy(lp);
    thread_exit_addr_ = vdso_base + thread_exit_offset_;

    // The child process needs a stack for the vDSO code to use.
    constexpr uint32_t stack_perm =
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    constexpr uint64_t stack_size = PAGE_SIZE;
    ZX_ASSERT(zx_vmo_create(stack_size, 0, &stack_vmo_) == ZX_OK);
    uintptr_t stack_base;
    ZX_ASSERT(zx_vmar_map(vmar_handle_, 0, stack_vmo_, 0, stack_size,
                          stack_perm, &stack_base) == ZX_OK);
    sp_ = compute_stack_pointer(stack_base, stack_size);

    // The child process needs a thread.
    ZX_ASSERT(zx_thread_create(proc_handle_, tname, sizeof(tname), 0, &thread_handle_) == ZX_OK);

    // It will also need a channel to its parent even though it won't use it.
    ZX_ASSERT(zx_channel_create(0, &channel_, &channel_to_transfer_) == ZX_OK);
}

void ProcessFixture::Start() {
    ZX_ASSERT(zx_process_start(proc_handle_, thread_handle_,
                               thread_exit_addr_, sp_, channel_to_transfer_,
                               0) == ZX_OK);
    channel_to_transfer_ = ZX_HANDLE_INVALID;
}

void ProcessFixture::Wait() {
    ZX_ASSERT(zx_object_wait_one(thread_handle_, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, NULL) ==
              ZX_OK);
}

void ProcessFixture::Close() {
    if (proc_handle_ != ZX_HANDLE_INVALID) {
        ZX_ASSERT(zx_handle_close(proc_handle_) == ZX_OK);
        proc_handle_ = ZX_HANDLE_INVALID;
    }
    if (vmar_handle_ != ZX_HANDLE_INVALID) {
        ZX_ASSERT(zx_handle_close(vmar_handle_) == ZX_OK);
        vmar_handle_ = ZX_HANDLE_INVALID;
    }
    if (thread_handle_ != ZX_HANDLE_INVALID) {
        ZX_ASSERT(zx_handle_close(thread_handle_) == ZX_OK);
        thread_handle_ = ZX_HANDLE_INVALID;
    }
    if (stack_vmo_ != ZX_HANDLE_INVALID) {
        ZX_ASSERT(zx_handle_close(stack_vmo_) == ZX_OK);
        stack_vmo_ = ZX_HANDLE_INVALID;
    }
    if (vdso_vmo_ != ZX_HANDLE_INVALID) {
        ZX_ASSERT(zx_handle_close(vdso_vmo_) == ZX_OK);
        vdso_vmo_ = ZX_HANDLE_INVALID;
    }
    if (channel_ != ZX_HANDLE_INVALID) {
        ZX_ASSERT(zx_handle_close(channel_) == ZX_OK);
        channel_ = ZX_HANDLE_INVALID;
    }
    if (channel_to_transfer_ != ZX_HANDLE_INVALID) {
        ZX_ASSERT(zx_handle_close(channel_to_transfer_) == ZX_OK);
        channel_to_transfer_ = ZX_HANDLE_INVALID;
    }
}

// This benchmark measures creating, starting, and waiting for completion of a
// minimal process.
bool StartTest(perftest::RepeatState* state) {
    state->DeclareStep("create");
    state->DeclareStep("init");
    state->DeclareStep("start");
    state->DeclareStep("wait");
    state->DeclareStep("close");

    ProcessFixture proc;
    while (state->KeepRunning()) {
        proc.Create();
        state->NextStep();
        proc.Init();
        state->NextStep();
        proc.Start();
        state->NextStep();
        proc.Wait();
        state->NextStep();
        proc.Close();
    }
    return true;
}

void RegisterTests() {
    perftest::RegisterTest("Process/Start", StartTest);
}
PERFTEST_CTOR(RegisterTests);

} // namespace
