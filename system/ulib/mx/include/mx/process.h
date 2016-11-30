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

    // TODO(MG-417): Remove default value for root_vmar after callers migrated
    static mx_status_t create(const job& job, const char* name, uint32_t name_len,
                              uint32_t flags, process* proc, vmar* root_vmar = nullptr);

    static inline const process& self() {
        return *reinterpret_cast<process*>(&__magenta_process_self);
    }

    mx_status_t start(const thread& thread_handle, uintptr_t entry,
                      uintptr_t stack, handle arg_handle, uintptr_t arg2) const;

    // TODO(MG-417): Remove these 3 functions after callers are migrated
    mx_status_t map_vm(const vmo& vmo_handle, uint64_t offset, size_t len,
                       uintptr_t* ptr, uint32_t flags) const {
        return mx_process_map_vm(get(), vmo_handle.get(), offset, len, ptr,
                                 flags);
    }

    mx_status_t unmap_vm(uintptr_t address, size_t len) const {
        return mx_process_unmap_vm(get(), address, len);
    }

    mx_status_t protect_vm(uintptr_t address, size_t len,
                           uint32_t prot) const {
        return mx_process_protect_vm(get(), address, len, prot);
    }
};

} // namespace mx
