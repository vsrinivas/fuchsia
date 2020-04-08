// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/channel_provider.h"

#include <zircon/errors.h>

namespace feedback {
namespace stubs {

void ChannelProvider::GetCurrent(GetCurrentCallback callback) { callback(channel_); }

void ChannelProviderReturnsEmptyChannel::GetCurrent(GetCurrentCallback callback) { callback(""); }

}  // namespace stubs
}  // namespace feedback
