// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/status.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"

// This file provides a common::Status template specialization for hci::Status
//
// EXAMPLES:
//
//   // 1. Status containing success:
//   hci::Status status;
//
//   // 2. Status containing a host-internal error:
//   hci::Status status(common::HostError::kTimedOut);
//
//   // 3. Status containing HCI status code:
//   hci::Status status(hci::Status::kHardwareFailure);
//
//   // 4. Status containing HCI success status code is converted to #1:
//   hci::Status status(hci::StatusCode::kSuccess);
//   status.is_success() -> true
//   status.is_protocol_error() -> false

namespace btlib {
namespace common {

template <>
struct ProtocolErrorTraits<hci::StatusCode> {
  static std::string ToString(hci::StatusCode ecode);
};

}  // namespace common

namespace hci {

class Status : public common::Status<StatusCode> {
 public:
  explicit Status(common::HostError ecode = common::HostError::kNoError);
  explicit Status(hci::StatusCode proto_code);
};

using StatusCallback = std::function<void(const Status& status)>;

}  // namespace hci
}  // namespace btlib
