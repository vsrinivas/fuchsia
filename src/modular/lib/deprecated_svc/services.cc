// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/deprecated_svc/services.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>

namespace component {

void ConnectToService(const fidl::InterfaceHandle<fuchsia::io::Directory>& directory,
                      zx::channel request, const std::string& service_path) {
  fdio_service_connect_at(directory.channel().get(), service_path.c_str(), request.release());
}

Services::Services() = default;

Services::~Services() = default;

Services::Services(Services&& other) : directory_(std::move(other.directory_)) {}

Services& Services::operator=(Services&& other) {
  directory_ = std::move(other.directory_);
  return *this;
}

fidl::InterfaceRequest<fuchsia::io::Directory> Services::NewRequest() {
  return directory_.NewRequest();
}

void Services::Bind(fidl::InterfaceHandle<fuchsia::io::Directory> directory) {
  directory_ = std::move(directory);
}

}  // namespace component
