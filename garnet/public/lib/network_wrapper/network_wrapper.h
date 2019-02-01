// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_H_
#define LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_H_

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/callback/cancellable.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace network_wrapper {

// Abstraction for the network service. It will reconnect to the network service
// application in case of disconnection, as well as handle 307 and 308
// redirections.
class NetworkWrapper {
 public:
  NetworkWrapper() {}
  virtual ~NetworkWrapper() {}

  // Starts a url network request.
  virtual fxl::RefPtr<callback::Cancellable> Request(
      fit::function<::fuchsia::net::oldhttp::URLRequest()> request_factory,
      fit::function<void(::fuchsia::net::oldhttp::URLResponse)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(NetworkWrapper);
};

}  // namespace network_wrapper

#endif  // LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_H_
