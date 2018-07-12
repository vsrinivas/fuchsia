// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HTTP_HTTP_SERVICE_IMPL_H_
#define GARNET_BIN_HTTP_HTTP_SERVICE_IMPL_H_

#include <list>
#include <queue>

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include "garnet/bin/http/http_url_loader_impl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"

namespace http {

class HttpServiceImpl : public ::fuchsia::net::oldhttp::HttpService,
                        public URLLoaderImpl::Coordinator {
 public:
  HttpServiceImpl(async_dispatcher_t* dispatcher);
  ~HttpServiceImpl() override;

  void AddBinding(fidl::InterfaceRequest<::fuchsia::net::oldhttp::HttpService> request);

  // HttpService methods:
  void CreateURLLoader(fidl::InterfaceRequest<::fuchsia::net::oldhttp::URLLoader> request) override;

 private:
  class UrlLoaderContainer;

  // URLLoaderImpl::Coordinator:
  void RequestNetworkSlot(
      fit::function<void(fit::closure)> slot_request) override;

  void OnSlotReturned();

  async_dispatcher_t* const dispatcher_;
  size_t available_slots_;
  fidl::BindingSet<::fuchsia::net::oldhttp::HttpService> bindings_;
  std::list<UrlLoaderContainer> loaders_;
  std::queue<fit::function<void(fit::closure)>> slot_requests_;
};

}  // namespace http

#endif  // GARNET_BIN_HTTP_HTTP_SERVICE_IMPL_H_
