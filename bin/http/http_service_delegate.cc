// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/http/http_service_delegate.h"

namespace http {

namespace oldhttp = ::fuchsia::net::oldhttp;

HttpServiceDelegate::HttpServiceDelegate(async_dispatcher_t* dispatcher)
    : context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
      http_provider_(dispatcher) {
  FXL_DCHECK(dispatcher),
      context_->outgoing().AddPublicService<oldhttp::HttpService>(
          [this](fidl::InterfaceRequest<oldhttp::HttpService> request) {
            http_provider_.AddBinding(std::move(request));
          });
}

HttpServiceDelegate::~HttpServiceDelegate() {}

}  // namespace http
