// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/status.h"
#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"

// This file provides a common::Status template specialization for sdp::Status
//
// EXAMPLES:
//
//   // 1. Status containing success:
//   sdp::Status status;
//
//   // 2. Status containing a host-internal error:
//   sdp::Status status(common::HostError::kTimedOut);
//
//   // 3. Status containing SDP status code:
//   sdp::Status status(sdp::ErrorCode::kInvalidSize);

namespace btlib {
namespace common {

template <>
struct ProtocolErrorTraits<btlib::sdp::ErrorCode> {
  static std::string ToString(btlib::sdp::ErrorCode ecode);
};

}  // namespace common

namespace sdp {

std::string ErrorCodeToString(ErrorCode code);

class Status : public common::Status<ErrorCode> {
 public:
  explicit Status(common::HostError ecode = common::HostError::kNoError);
  explicit Status(sdp::ErrorCode proto_code);
};

}  // namespace sdp
}  // namespace btlib
