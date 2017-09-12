// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/handle.h>
#include <zx/object.h>

namespace zx {

template <typename T = void> class task : public object<T> {
public:
    constexpr task() = default;

    explicit task(zx_handle_t value) : object<T>(value) {}

    explicit task(handle&& h) : object<T>(h.release()) {}

    task(task&& other) : object<T>(other.release()) {}

    zx_status_t resume(uint32_t options) const {
        return zx_task_resume(object<T>::get(), options);
    }

    // TODO(abarth): zx_task_bind_exception_port

    zx_status_t kill() const { return zx_task_kill(object<T>::get()); }

    zx_status_t suspend() const { return zx_task_suspend(object<T>::get()); }
};

} // namespace zx
