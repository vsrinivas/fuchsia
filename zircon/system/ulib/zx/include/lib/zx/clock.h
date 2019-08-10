// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_CLOCK_H_
#define LIB_ZX_CLOCK_H_

#include <lib/zx/time.h>

namespace zx {

class clock final {
public:
    clock() = delete;

    template <zx_clock_t kClockId>
    static zx_status_t get(basic_time<kClockId>* result) {
        return zx_clock_get(kClockId, result->get_address());
    }

    static time get_monotonic() {
        return time(zx_clock_get_monotonic());
    }
};

}  // namespace zx

#endif  // LIB_ZX_CLOCK_H_
