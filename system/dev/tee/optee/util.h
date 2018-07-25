// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <stddef.h>

namespace optee {

// UuidView
//
// Helper class for a non-owning view of a UUID. It's lifetime is only valid for the duration of
// the provided data pointer being valid.
class UuidView {
public:
    explicit UuidView(const uint8_t* data, size_t size);

    void ToUint64Pair(uint64_t* out_hi, uint64_t* out_low) const;

private:
    static constexpr size_t kUuidSize = 16;

    const uint8_t* ptr_;
};

} // namespace optee
