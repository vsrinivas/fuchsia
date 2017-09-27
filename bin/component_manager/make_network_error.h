// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_COMPONENT_MANAGER_MAKE_NETWORK_ERROR_H_
#define PERIDOT_BIN_COMPONENT_MANAGER_MAKE_NETWORK_ERROR_H_

#include "lib/network/fidl/network_error.fidl.h"

namespace component {

network::NetworkErrorPtr MakeNetworkError(int code,
                                          const std::string& description);

}  // namespace component

#endif  // PERIDOT_BIN_COMPONENT_MANAGER_MAKE_NETWORK_ERROR_H_
