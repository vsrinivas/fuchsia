// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/svc/cpp/services.h"

#include <lib/fdio/util.h>

#include "lib/fxl/logging.h"

namespace component {

void ConnectToService(const zx::channel& directory, zx::channel request,
                      const std::string& service_path) {
  fdio_service_connect_at(directory.get(), service_path.c_str(),
                          request.release());
}

Services::Services() = default;

Services::~Services() = default;

Services::Services(Services&& other)
    : directory_(std::move(other.directory_)) {}

Services& Services::operator=(Services&& other) {
  directory_ = std::move(other.directory_);
  return *this;
}

zx::channel Services::NewRequest() {
  zx::channel request;
  FXL_CHECK(zx::channel::create(0, &request, &directory_) == ZX_OK);
  return request;
}

void Services::Bind(zx::channel directory) {
  directory_ = std::move(directory);
}

}  // namespace component
