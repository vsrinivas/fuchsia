// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_URL_LOADER_IMPL_H_
#define GARNET_BIN_NETWORK_URL_LOADER_IMPL_H_

#include "lib/network/fidl/url_loader.fidl.h"

#include "lib/fidl/cpp/bindings/binding.h"
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

  using Callback = std::function<void(network::URLResponsePtr)>;

  // URLLoader methods:
  void Start(URLRequestPtr request, const Callback& callback) override;
  void FollowRedirect(const Callback& callback) override;
  void QueryStatus(const QueryStatusCallback& callback) override;

  void SendError(int error_code);
  void FollowRedirectInternal();
  void SendResponse(URLResponsePtr response);
  void StartInternal(URLRequestPtr request);

  Coordinator* coordinator_;
  Callback callback_;
  bool buffer_response_;
  // bool auto_follow_redirects_;
  url::GURL current_url_;
  URLLoaderStatusPtr last_status_;
};

}  // namespace network

#endif  // GARNET_BIN_NETWORK_URL_LOADER_IMPL_H_
