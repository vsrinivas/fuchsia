// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/component_manager/make_network_error.h"

namespace component {

network::NetworkErrorPtr MakeNetworkError(int code,
                                          const std::string& description) {
  auto error = network::NetworkError::New();
  error->code = code;
  error->description = description;
  return error;
}
}  // namespace component
