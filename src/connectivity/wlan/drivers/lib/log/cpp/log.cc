// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/drivers/log.h>

namespace wlan::drivers {

// static
void Log::SetFilter(uint32_t filter) { getInstance().filter_ = filter; }

}  // namespace wlan::drivers
