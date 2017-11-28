// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace i915 {

class MmioSpace {
public:
    MmioSpace(uintptr_t base);

    uint32_t Read32(uint32_t offset);
    void Write32(uint32_t offset, uint32_t val);

    void Write64(uint32_t offset, uint64_t val);

private:
    uintptr_t base_;
};

} // namespace i915
