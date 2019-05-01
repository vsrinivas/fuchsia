// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_LINUX_RUNNER_PORTS_H_
#define GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_LINUX_RUNNER_PORTS_H_

namespace linux_runner {

static constexpr uint32_t kStartupListenerPort = 7777;
static constexpr uint32_t kTremplinListenerPort = 7778;
static constexpr uint32_t kMaitredPort = 8888;
static constexpr uint32_t kGarconPort = 8889;
static constexpr uint32_t kTremplinPort = 8890;
static constexpr uint32_t kLogCollectorPort = 9999;

}  // namespace linux_runner

#endif  // GARNET_BIN_GUEST_PKG_BISCOTTI_GUEST_LINUX_RUNNER_PORTS_H_
