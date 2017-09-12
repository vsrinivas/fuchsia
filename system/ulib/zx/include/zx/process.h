// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/object.h>
#include <zx/task.h>
#include <zx/vmar.h>
#include <zx/vmo.h>
#include <zircon/process.h>

namespace zx {
class job;
class thread;

class process : public task<process> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_PROCESS;

    constexpr process() = default;

    explicit process(zx_handle_t value) : task(value) {}

    explicit process(handle&& h) : task(h.release()) {}

    process(process&& other) : task(other.release()) {}

    process& operator=(process&& other) {
        reset(other.release());
        return *this;
    }

    // Rather than creating a process directly with this syscall,
    // consider using the launchpad library, which properly sets up
    // the many details of creating a process beyond simply creating
    // the kernel structure.
    static zx_status_t create(const job& job, const char* name, uint32_t name_len,
                              uint32_t flags, process* proc, vmar* root_vmar);

    zx_status_t start(const thread& thread_handle, uintptr_t entry,
                      uintptr_t stack, handle arg_handle, uintptr_t arg2) const;

    static inline const unowned<process> self() {
        return unowned<process>(zx_process_self());
    }
};

using unowned_process = const unowned<process>;

} // namespace zx
