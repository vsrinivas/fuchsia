// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_HOST_NAME_H_
#define GARNET_BIN_MDNS_SERVICE_HOST_NAME_H_

#include <string>

namespace mdns {

// Determines whether we have a NIC with a valid address.
bool NetworkIsReady();

// Gets the host name, possibly deduped using the host address.
std::string GetHostName();

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_HOST_NAME_H_
