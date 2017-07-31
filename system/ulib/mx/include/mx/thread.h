// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/object.h>
#include <mx/task.h>
#include <magenta/process.h>

namespace mx {
class process;

class thread : public task<thread> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_THREAD;

    constexpr thread() = default;

    explicit thread(mx_handle_t value) : task(value) {}

    explicit thread(handle&& h) : task(h.release()) {}

    thread(thread&& other) : task(other.release()) {}

    thread& operator=(thread&& other) {
        reset(other.release());
        return *this;
    }

    // Rather than creating a thread directly with this syscall, consider using
    // std::thread or thrd_create, which properly integrates with the
    // thread-local data structures in libc.
    static mx_status_t create(const process& process, const char* name,
                              uint32_t name_len, uint32_t flags,
                              thread* result);

    mx_status_t start(uintptr_t thread_entry, uintptr_t stack, uintptr_t arg1,
                      uintptr_t arg2) const {
        return mx_thread_start(get(), thread_entry, stack, arg1, arg2);
    }

    // TODO(abarth): mx_thread_read_state
    // TODO(abarth): mx_thread_write_state

    static inline const unowned<thread> self() {
        return unowned<thread>(mx_thread_self());
    }
};

using unowned_thread = const unowned<thread>;

} // namespace mx
