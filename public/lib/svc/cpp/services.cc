// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/svc/cpp/services.h"

#include <mxio/util.h>

#include "lib/ftl/logging.h"

namespace app {

Services::Services() = default;

Services::~Services() = default;

Services::Services(Services&& other)
    : directory_(std::move(other.directory_)) {}

Services& Services::operator=(Services&& other) {
  directory_ = std::move(other.directory_);
  return *this;
}

mx::channel Services::NewRequest() {
  mx::channel request;
  FTL_CHECK(mx::channel::create(0, &request, &directory_) == MX_OK);
  return request;
}

void Services::Bind(mx::channel directory) {
  directory_ = std::move(directory);
}

void Services::ConnectToService(const std::string& service_name,
                                mx::channel request) {
  mxio_service_connect_at(directory_.get(), service_name.c_str(),
                          request.release());
}

}  // namespace app
