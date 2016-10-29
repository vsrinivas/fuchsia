// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/task.h>

namespace mx {
class process;

class thread : public task<thread> {
public:
    thread() = default;

    explicit thread(handle<void>&& h) : task<thread>(h.release()) {}

    thread(thread&& other) : task<thread>(other.release()) {}

    thread& operator=(thread&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(const process& process, const char* name,
                              uint32_t name_len, uint32_t flags,
                              thread* result);

    mx_status_t start(uintptr_t thread_entry, uintptr_t stack, uintptr_t arg1,
                      uintptr_t arg2) const {
        return mx_thread_start(get(), thread_entry, stack, arg1, arg2);
    }

    mx_status_t arch_prctl(uint32_t op, uintptr_t* value_ptr) const {
        return mx_thread_arch_prctl(get(), op, value_ptr);
    }

    // TODO(abarth): mx_thread_read_state
    // TODO(abarth): mx_thread_write_state
};

} // namespace mx
