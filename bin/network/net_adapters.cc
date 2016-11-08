// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/network/net_adapters.h"
#include "apps/network/net_errors.h"

namespace network {

NetworkErrorPtr MakeNetworkError(int error_code) {
  NetworkErrorPtr error = NetworkError::New();
  error->code = error_code;
  if (error_code <= 0)
    error->description = network::ErrorToString(error_code);
  return error;
}

}  // namespace network
