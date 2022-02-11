// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_COMPAT_SYMBOLS_H_
#define SRC_DEVICES_LIB_COMPAT_SYMBOLS_H_

namespace compat {

// Name of the DFv1 device.
constexpr char kName[] = "compat-name";
// Context for the DFv1 device.
constexpr char kContext[] = "compat-context";
// Ops of the DFv1 device.
constexpr char kOps[] = "compat-ops";

struct compat_device_proto_ops_t {
  void* ops;
  uint32_t id;
};

constexpr char kProtoOps[] = "compat-proto-ops";

}  // namespace compat

#endif  // SRC_DEVICES_LIB_COMPAT_SYMBOLS_H_
