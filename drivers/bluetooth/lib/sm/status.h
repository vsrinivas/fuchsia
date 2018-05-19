// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/common/status.h"
#include "garnet/drivers/bluetooth/lib/sm/smp.h"

namespace btlib {
namespace common {

template <>
struct ProtocolErrorTraits<sm::ErrorCode> {
  static std::string ToString(sm::ErrorCode ecode);
};

}  // namespace common

namespace sm {

using Status = common::Status<ErrorCode>;

using StatusCallback = fit::function<void(Status)>;

}  // namespace sm
}  // namespace btlib
