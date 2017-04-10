// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

namespace wlan {

// Header is the method header that is prepended to method calls over the channel.
// This will be removed when FIDL2 is available.
struct Header {
    uint64_t len;
    uint64_t txn_id;
    uint32_t flags;
    uint32_t ordinal;
} __PACKED;

}  // namespace wlan
