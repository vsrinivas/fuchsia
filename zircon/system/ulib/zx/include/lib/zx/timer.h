// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_TIMER_H_
#define LIB_ZX_TIMER_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

#include <zircon/types.h>

namespace zx {

class timer : public object<timer> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_TIMER;

    constexpr timer() = default;

    explicit timer(zx_handle_t value) : object(value) {}

    explicit timer(handle&& h) : object(h.release()) {}

    timer(timer&& other) : object(other.release()) {}

    timer& operator=(timer&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(uint32_t options, zx_clock_t clock_id, timer* result);

    zx_status_t set(zx::time deadline, zx::duration slack) const {
        return zx_timer_set(get(), deadline.get(), slack.get());
    }

    zx_status_t cancel() const {
        return zx_timer_cancel(get());
    }
};

using unowned_timer = unowned<timer>;

} // namespace zx

#endif  // LIB_ZX_TIMER_H_
