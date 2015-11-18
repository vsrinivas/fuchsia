// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/services/network/network_service_impl.h"

#include "mojo/services/network/url_loader_impl.h"
#include "mojo/public/cpp/application/application_connection.h"

namespace mojo {

NetworkServiceImpl::NetworkServiceImpl(InterfaceRequest<NetworkService> request,
                                       ApplicationConnection* connection)
    : binding_(this, request.Pass()) {
}

NetworkServiceImpl::~NetworkServiceImpl() {
}

void NetworkServiceImpl::CreateURLLoader(InterfaceRequest<URLLoader> loader) {
  new URLLoaderImpl(loader.Pass());
}

}  // namespace mojo
