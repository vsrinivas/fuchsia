// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback_agent/tests/stub_channel_provider.h"

namespace fuchsia {
namespace feedback {

void StubUpdateInfo::GetChannel(GetChannelCallback callback) { callback(channel_); }

void StubUpdateInfoClosesConnection::GetChannel(GetChannelCallback callback) {
  CloseAllConnections();
}

void StubUpdateInfoNeverReturns::GetChannel(GetChannelCallback callback) {}

}  // namespace feedback
}  // namespace fuchsia
