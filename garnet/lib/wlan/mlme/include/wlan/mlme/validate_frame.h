// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/span.h>

namespace wlan {

bool ValidateFrame(const char* context_msg, Span<const uint8_t> data);

} // namespace wlan
