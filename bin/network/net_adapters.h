// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_NET_ADAPTERS_H_
#define GARNET_BIN_NETWORK_NET_ADAPTERS_H_

#include "lib/network/fidl/network_error.fidl.h"

namespace network {

// Creates a new network error object from a net error code.
NetworkErrorPtr MakeNetworkError(int error_code);

}  // namespace network

#endif  // GARNET_BIN_NETWORK_NET_ADAPTERS_H_
