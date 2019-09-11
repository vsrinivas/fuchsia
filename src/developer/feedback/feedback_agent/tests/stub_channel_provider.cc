// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/tests/stub_channel_provider.h"

namespace feedback {

void StubChannelProvider::GetCurrent(GetCurrentCallback callback) { callback(channel_); }

void StubChannelProviderClosesConnection::GetCurrent(GetCurrentCallback callback) {
  CloseAllConnections();
}

void StubChannelProviderNeverReturns::GetCurrent(GetCurrentCallback callback) {}

}  // namespace feedback
