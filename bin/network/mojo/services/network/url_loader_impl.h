// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_URL_LOADER_IMPL_H_
#define MOJO_SERVICES_NETWORK_URL_LOADER_IMPL_H_

#include "base/memory/scoped_ptr.h"
#include "mojo/services/network/interfaces/url_loader.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"

#include "mojo/services/network/url.h"

namespace mojo {

class URLLoaderImpl : public URLLoader {
 public:
  URLLoaderImpl(InterfaceRequest<URLLoader> request);
  ~URLLoaderImpl() override;

  // Called when the associated NetworkContext is going away.
  void Cleanup();

 private:
  template<typename T> class HTTPClient;

  // URLLoader methods:
  void Start(URLRequestPtr request,
             const Callback<void(URLResponsePtr)>& callback) override;
  void FollowRedirect(const Callback<void(URLResponsePtr)>& callback) override;
  void QueryStatus(const Callback<void(URLLoaderStatusPtr)>& callback) override;

  void OnConnectionError();
  void SendError(int error_code);
  void FollowRedirectInternal();
  void SendResponse(URLResponsePtr response);
  void StartInternal(URLRequestPtr request);

  Callback<void(URLResponsePtr)> callback_;
  // bool auto_follow_redirects_;
  URLLoaderStatusPtr last_status_;
  Binding<URLLoader> binding_;
};

}  // namespace mojo

#endif  // MOJO_SERVICES_NETWORK_URL_LOADER_IMPL_H_
