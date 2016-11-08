// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETWORK_URL_LOADER_IMPL_H_
#define APPS_NETWORK_URL_LOADER_IMPL_H_

#include "apps/network/services/url_loader.fidl.h"

#include "lib/fidl/cpp/bindings/binding.h"

namespace network {

class URLLoaderImpl : public URLLoader {
 public:
  URLLoaderImpl(fidl::InterfaceRequest<URLLoader> request);
  ~URLLoaderImpl() override;

  // Called when the associated NetworkContext is going away.
  void Cleanup();

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

  Callback callback_;
  // bool auto_follow_redirects_;
  URLLoaderStatusPtr last_status_;
  fidl::Binding<URLLoader> binding_;
};

}  // namespace network

#endif  // APPS_NETWORK_URL_LOADER_IMPL_H_
