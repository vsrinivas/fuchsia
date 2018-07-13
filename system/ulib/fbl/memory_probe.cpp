// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/memory_probe.h>

#include <limits.h>
#include <stdio.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace {

enum class ProbeOperation {
    kRead,
    kWrite
};

#if __has_feature(address_sanitizer)
[[clang::no_sanitize("address")]]
#endif
void except_thread_func(uintptr_t op, uintptr_t address) {
    volatile char* ch_address = reinterpret_cast<char*>(address);

    char ch = *ch_address;
    if (static_cast<ProbeOperation>(op) == ProbeOperation::kWrite)
        *ch_address = ch;

    zx_thread_exit();
}

bool do_probe(ProbeOperation op, const void* addr) {
    // This function starts a new thread to perform the read/write test, and catches any exceptions
    // in this thread to see if it failed or not.
    zx::thread thread;
    zx_status_t status = zx::thread::create(*zx::process::self(), "memory_probe", 12u, 0u, &thread);
    if (status != ZX_OK)
        return false;

    alignas(16) static uint8_t thread_stack[128];
    void* stack = thread_stack + sizeof(thread_stack);

    zx::port port;
    status = zx::port::create(0, &port);
    if (status != ZX_OK)
        return false;

    // Cause the port to be signaled with kThreadKey when the background thread crashes or teminates without crashing.
    constexpr uint64_t kThreadKey = 0x42;
    if (thread.wait_async(port, kThreadKey, ZX_THREAD_TERMINATED, ZX_WAIT_ASYNC_ONCE) != ZX_OK) {
        return false;
    }
    if (zx_task_bind_exception_port(thread.get(), port.get(), kThreadKey, 0) != ZX_OK) {
        return false;
    }

    thread.start(&except_thread_func, stack, static_cast<uintptr_t>(op), reinterpret_cast<uintptr_t>(addr));

    // Wait for crash or thread completion.
    zx_port_packet_t packet;
    if (port.wait(zx::time::infinite(), &packet) == ZX_OK) {
        if (ZX_PKT_IS_EXCEPTION(packet.type)) {
            // Thread crashed so the operation failed. The thread is now in a suspended state and
            // needs to be explicitly terminated.
            thread.kill();
            return false;
        }
        if (ZX_PKT_IS_SIGNAL_ONE(packet.type)) {
            if (packet.key == kThreadKey && (packet.signal.observed & ZX_THREAD_TERMINATED)) {
                // Thread terminated normally so the memory is readable/writable.
                return true;
            }
        }
    }

    return false;
}

} // namespace

bool probe_for_read(const void* addr) {
    return do_probe(ProbeOperation::kRead, addr);
}

bool probe_for_write(void* addr) {
    return do_probe(ProbeOperation::kWrite, addr);
}
