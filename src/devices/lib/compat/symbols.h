// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_COMPAT_SYMBOLS_H_
#define SRC_DEVICES_LIB_COMPAT_SYMBOLS_H_

#include <stdint.h>

namespace compat {

struct device_proto_ops_t {
  void* ops;
  uint32_t id;
};

struct device_t {
  device_proto_ops_t proto_ops;
  const char* name;
  void* context;
};

constexpr device_t kDefaultDevice = {
    .proto_ops =
        {
            .ops = nullptr,
            .id = 0,
        },
    .name = "compat-device",
    .context = nullptr,
};

// The symbol for the compat device: device_t.
constexpr char kDeviceSymbol[] = "fuchsia.compat.device/Device";

}  // namespace compat

#endif  // SRC_DEVICES_LIB_COMPAT_SYMBOLS_H_
