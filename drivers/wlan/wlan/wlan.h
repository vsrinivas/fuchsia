// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace wlan {

enum class PortKey : uint64_t {
    kDevice,
    kService,
    kMlme,
};

template <typename T>
constexpr uint64_t to_u64(T t) {
    return static_cast<uint64_t>(t);
}

}  // namespace wlan
