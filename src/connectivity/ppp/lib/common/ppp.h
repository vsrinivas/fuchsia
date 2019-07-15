// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ppp {

enum class Protocol {
  Ipv4 = 0x0021,
  Ipv6 = 0x0057,
  Ipv4Control = 0x8021,
  Ipv6Control = 0x8057,
  LinkControl = 0xc021,
  PasswordAuthentication = 0xc023,
  LinkQualityReport = 0xc025,
  ChallengeHandshakeAuthentication = 0xc223,
};

}  // namespace ppp
