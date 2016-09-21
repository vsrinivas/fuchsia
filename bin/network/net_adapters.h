// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_NET_ADAPTERS_H_
#define MOJO_SERVICES_NETWORK_NET_ADAPTERS_H_

#include "mojo/public/interfaces/network/network_error.mojom.h"

namespace mojo {

// Creates a new Mojo network error object from a net error code.
NetworkErrorPtr MakeNetworkError(int error_code);

} // namespace mojo

#endif  // MOJO_SERVICES_NETWORK_NET_ADAPTERS_H_
