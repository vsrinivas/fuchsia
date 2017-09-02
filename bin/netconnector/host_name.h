// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace netconnector {

// Determines whether we have a NIC with a valid address.
bool NetworkIsReady();

// Gets the host name, possibly deduped using the host address.
std::string GetHostName();

}  // namespace netconnector
