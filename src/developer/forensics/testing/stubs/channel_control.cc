// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/channel_control.h"

#include <zircon/errors.h>

namespace forensics {
namespace stubs {

void ChannelControl::GetCurrent(GetCurrentCallback callback) {
  FX_CHECK(Current().has_value());
  callback(Current().value());
}

void ChannelControl::GetTarget(GetTargetCallback callback) {
  FX_CHECK(Target().has_value());
  callback(Target().value());
}

void ChannelControlReturnsEmptyChannel::GetCurrent(GetCurrentCallback callback) { callback(""); }

void ChannelControlReturnsEmptyChannel::GetTarget(GetTargetCallback callback) { callback(""); }

void ChannelControlClosesFirstConnection::GetCurrent(GetCurrentCallback callback) {
  if (first_call_) {
    first_call_ = false;
    CloseAllConnections();
    return;
  }

  FX_CHECK(Current().has_value());
  callback(Current().value());
}

void ChannelControlClosesFirstConnection::GetTarget(GetTargetCallback callback) {
  if (first_call_) {
    first_call_ = false;
    CloseAllConnections();
    return;
  }

  FX_CHECK(Target().has_value());
  callback(Target().value());
}

ChannelControlExpectsOneCall::~ChannelControlExpectsOneCall() {
  FX_CHECK(!first_call_) << "No call was made";
}

void ChannelControlExpectsOneCall::GetCurrent(GetCurrentCallback callback) {
  FX_CHECK(first_call_) << "Only one call to GetCurrent should be made";
  first_call_ = false;

  FX_CHECK(Current().has_value());
  callback(Current().value());
}

void ChannelControlExpectsOneCall::GetTarget(GetTargetCallback callback) {
  FX_CHECK(first_call_) << "Only one call to GetTarget should be made";
  first_call_ = false;

  FX_CHECK(Target().has_value());
  callback(Target().value());
}

}  // namespace stubs
}  // namespace forensics
