// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

template <typename T = void> class task : public handle<T> {
public:
    task() = default;

    explicit task(mx_handle_t value) : handle<T>(value) {}

    explicit task(handle<void>&& h) : handle<T>(h.release()) {}

    task(task&& other) : handle<T>(other.release()) {}

    mx_status_t resume(uint32_t options) const {
        return mx_task_resume(handle<T>::get(), options);
    }

    mx_status_t kill() const { return mx_task_kill(handle<T>::get()); }
};

} // namespace mx
