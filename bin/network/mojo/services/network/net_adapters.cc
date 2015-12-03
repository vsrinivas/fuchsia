// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/services/network/net_adapters.h"

#include "mojo/services/network/network_error.h"

namespace mojo {

mojo::NetworkErrorPtr MakeNetworkError(int error_code) {
  mojo::NetworkErrorPtr error = NetworkError::New();
  error->code = error_code;
  return error.Pass();
}

} // namespace mojo

