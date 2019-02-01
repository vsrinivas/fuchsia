// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_ATT_STATUS_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_ATT_STATUS_H_

#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/common/status.h"

namespace btlib {
namespace common {

template <>
struct ProtocolErrorTraits<att::ErrorCode> {
  static std::string ToString(att::ErrorCode ecode);
};

}  // namespace common

namespace att {

using Status = common::Status<ErrorCode>;

// Copyable callback for reporting a Status.
using StatusCallback = fit::function<void(att::Status)>;

}  // namespace att
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_ATT_STATUS_H_
