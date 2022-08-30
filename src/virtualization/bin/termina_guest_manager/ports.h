// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_PORTS_H_
#define SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_PORTS_H_

namespace termina_guest_manager {

static constexpr uint32_t kStartupListenerPort = 7777;
static constexpr uint32_t kTremplinListenerPort = 7778;
static constexpr uint32_t kCrashListenerPort = 7779;
static constexpr uint32_t kMaitredPort = 8888;
static constexpr uint32_t kGarconPort = 8889;
static constexpr uint32_t kTremplinPort = 8890;
static constexpr uint32_t kLogCollectorPort = 9999;

}  // namespace termina_guest_manager

#endif  // SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_PORTS_H_
