// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_DEVICE_H_
#define GARNET_BIN_HWSTRESS_DEVICE_H_

#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <string>
#include <string_view>

namespace hwstress {

// Open the given path as a FIDL channel.
zx::status<zx::channel> OpenDeviceChannel(std::string_view path);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_DEVICE_H_
