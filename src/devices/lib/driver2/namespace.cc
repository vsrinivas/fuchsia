// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/namespace.h"

#include <zircon/device/vfs.h>

zx::status<Namespace> Namespace::Create(
    fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry>& entries) {
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_create(&ns);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  Namespace self(ns);
  for (auto& entry : entries) {
    std::string path(entry.path().data(), entry.path().size());
    status = fdio_ns_bind(ns, path.data(), entry.directory().TakeChannel().release());
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
  return zx::ok(std::move(self));
}

Namespace::Namespace(fdio_ns_t* ns) : ns_(ns) {}

Namespace::~Namespace() {
  if (ns_ != nullptr) {
    fdio_ns_destroy(ns_);
  }
}

Namespace::Namespace(Namespace&& other) noexcept : ns_(other.ns_) { other.ns_ = nullptr; }

Namespace& Namespace::operator=(Namespace&& other) noexcept {
  this->~Namespace();
  ns_ = other.ns_;
  other.ns_ = nullptr;
  return *this;
}

zx::status<> Namespace::Connect(std::string_view path, zx::channel server_end) const {
  zx_status_t status =
      fdio_ns_connect(ns_, path.data(), ZX_FS_RIGHT_READABLE, server_end.release());
  return zx::make_status(status);
}
