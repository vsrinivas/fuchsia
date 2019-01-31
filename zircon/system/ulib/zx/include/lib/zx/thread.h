// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_THREAD_H_
#define LIB_ZX_THREAD_H_

#include <lib/zx/object.h>
#include <lib/zx/task.h>
#include <zircon/process.h>

namespace zx {
class process;

class thread : public task<thread> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_THREAD;

    constexpr thread() = default;

    explicit thread(zx_handle_t value)
        : task(value) {}

    explicit thread(handle&& h)
        : task(h.release()) {}

    thread(thread&& other)
        : task(other.release()) {}

    thread& operator=(thread&& other) {
        reset(other.release());
        return *this;
    }

    // Rather than creating a thread directly with this syscall, consider using
    // std::thread or thrd_create, which properly integrates with the
    // thread-local data structures in libc.
    static zx_status_t create(const process& process, const char* name,
                              uint32_t name_len, uint32_t flags,
                              thread* result);

    // The first variant maps exactly to the syscall and can be used for
    // launching threads in remote processes. The second variant is for
    // conveniently launching threads in the current process.
    zx_status_t start(uintptr_t thread_entry, uintptr_t stack, uintptr_t arg1,
                      uintptr_t arg2) const {
        return zx_thread_start(get(), thread_entry, stack, arg1, arg2);
    }
    zx_status_t start(void (*thread_entry)(uintptr_t arg1, uintptr_t arg2),
                      void* stack, uintptr_t arg1, uintptr_t arg2) {
        return zx_thread_start(get(),
                               reinterpret_cast<uintptr_t>(thread_entry),
                               reinterpret_cast<uintptr_t>(stack), arg1, arg2);
    }

    zx_status_t read_state(uint32_t kind, void* buffer, size_t len) const {
        return zx_thread_read_state(get(), kind, buffer, len);
    }
    zx_status_t write_state(uint32_t kind, const void* buffer, size_t len) {
        return zx_thread_write_state(get(), kind, buffer, len);
    }

    static inline unowned<thread> self() {
        return unowned<thread>(zx_thread_self());
    }
};

using unowned_thread = unowned<thread>;

} // namespace zx

#endif  // LIB_ZX_THREAD_H_
