// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_URL_LOADER_IMPL_H_
#define GARNET_BIN_NETWORK_URL_LOADER_IMPL_H_

#include <network/cpp/fidl.h>

#include "lib/fxl/functional/closure.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/url/gurl.h"

namespace network {

class URLLoaderImpl : public URLLoader {
 public:
  // Coordinates requests to limit the number of concurrent active requests.
  class Coordinator {
   public:
    virtual ~Coordinator() {}
    virtual void RequestNetworkSlot(
        std::function<void(fxl::Closure)> slot_request) = 0;
  };

  URLLoaderImpl(Coordinator* coordinator);
  ~URLLoaderImpl() override;

 private:
  template <typename T>
  class HTTPClient;

  using Callback = std::function<void(network::URLResponse)>;

  // URLLoader methods:
  void Start(URLRequest request, Callback callback) override;
  void FollowRedirect(Callback callback) override;
  void QueryStatus(QueryStatusCallback callback) override;

  void SendError(int error_code);
  void FollowRedirectInternal();
  void SendResponse(URLResponse response);
  void StartInternal(URLRequest request);

  Coordinator* coordinator_;
  Callback callback_;
  ResponseBodyMode response_body_mode_;
  // bool auto_follow_redirects_;
  url::GURL current_url_;
  URLLoaderStatus last_status_;
};

}  // namespace network

#endif  // GARNET_BIN_NETWORK_URL_LOADER_IMPL_H_
