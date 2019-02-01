// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HTTP_HTTP_URL_LOADER_IMPL_H_
#define GARNET_BIN_HTTP_HTTP_URL_LOADER_IMPL_H_

#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include <lib/fit/function.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/url/gurl.h"

namespace http {

class URLLoaderImpl : public ::fuchsia::net::oldhttp::URLLoader {
 public:
  // Coordinates requests to limit the number of concurrent active requests.
  class Coordinator {
   public:
    virtual ~Coordinator() {}
    virtual void RequestNetworkSlot(
        fit::function<void(fit::closure)> slot_request) = 0;
  };

  URLLoaderImpl(Coordinator* coordinator);
  ~URLLoaderImpl() override;

 private:
  template <typename T>
  class HTTPClient;

  using Callback = fit::function<void(::fuchsia::net::oldhttp::URLResponse)>;

  // URLLoader methods:
  void Start(::fuchsia::net::oldhttp::URLRequest request,
             StartCallback callback) override;
  void FollowRedirect(FollowRedirectCallback callback) override;
  void QueryStatus(QueryStatusCallback callback) override;

  void SendError(int error_code);
  void FollowRedirectInternal();
  void SendResponse(::fuchsia::net::oldhttp::URLResponse response);
  void StartInternal(::fuchsia::net::oldhttp::URLRequest request);

  Coordinator* coordinator_;
  Callback callback_;
  ::fuchsia::net::oldhttp::ResponseBodyMode response_body_mode_;
  // bool auto_follow_redirects_;
  url::GURL current_url_;
  ::fuchsia::net::oldhttp::URLLoaderStatus last_status_;
};

}  // namespace http

#endif  // GARNET_BIN_HTTP_HTTP_URL_LOADER_IMPL_H_
