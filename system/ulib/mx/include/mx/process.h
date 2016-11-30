// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/task.h>
#include <mx/vmar.h>
#include <mx/vmo.h>

namespace mx {
class job;
class thread;

class process : public task<process> {
public:
    process() = default;

    explicit process(mx_handle_t value) : task(value) {}

    explicit process(handle<void>&& h) : task(h.release()) {}

    process(process&& other) : task(other.release()) {}

    process& operator=(process&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(const job& job, const char* name, uint32_t name_len,
                              uint32_t flags, process* proc, vmar* root_vmar);

    static inline const process& self() {
        return *reinterpret_cast<process*>(&__magenta_process_self);
    }

    mx_status_t start(const thread& thread_handle, uintptr_t entry,
                      uintptr_t stack, handle arg_handle, uintptr_t arg2) const;
};

} // namespace mx
