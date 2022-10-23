// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_HWSTRESS_DEVICE_H_
#define SRC_ZIRCON_BIN_HWSTRESS_DEVICE_H_

#include <lib/zx/channel.h>
#include <lib/zx/result.h>

#include <string>
#include <string_view>

namespace hwstress {

// Open the given path as a FIDL channel.
zx::result<zx::channel> OpenDeviceChannel(std::string_view path);

}  // namespace hwstress

#endif  // SRC_ZIRCON_BIN_HWSTRESS_DEVICE_H_
