// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

template <typename T = void> class task : public object<T> {
public:
    constexpr task() = default;

    explicit task(mx_handle_t value) : object<T>(value) {}

    explicit task(handle&& h) : object<T>(h.release()) {}

    task(task&& other) : object<T>(other.release()) {}

    mx_status_t resume(uint32_t options) const {
        return mx_task_resume(object<T>::get(), options);
    }

    // TODO(abarth): mx_task_bind_exception_port

    mx_status_t kill() const { return mx_task_kill(object<T>::get()); }

    mx_status_t suspend() const { return mx_task_suspend(object<T>::get()); }
};

} // namespace mx
