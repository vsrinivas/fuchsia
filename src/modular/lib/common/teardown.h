// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_COMMON_TEARDOWN_H_
#define SRC_MODULAR_LIB_COMMON_TEARDOWN_H_

#include <lib/zx/time.h>

namespace modular {

// Standard timeout for dispatcher teardown.
constexpr auto kBasicTimeout = zx::sec(3);

// Timeouts for teardown of composite objects need to be larger than the basic
// timeout, because they run through the teardown of their parts, at least
// partially sequentially.
//
// TODO(mesch): Obviously, this should adjust, be negotiated, or be set
// automatically as needed rather than hardcoded.

// fuchsia::modular::Agent + overhead.
constexpr auto kAgentRunnerTimeout = kBasicTimeout * 2;

// Stories + overhead.
constexpr auto kStoryProviderTimeout = kBasicTimeout * 2;

// Multiple parts as described.
constexpr auto kSessionmgrTimeout = kBasicTimeout * 3 + kAgentRunnerTimeout + kStoryProviderTimeout;

constexpr auto kSessionProviderTimeout = kBasicTimeout + kSessionmgrTimeout;

}  // namespace modular

#endif  // SRC_MODULAR_LIB_COMMON_TEARDOWN_H_
