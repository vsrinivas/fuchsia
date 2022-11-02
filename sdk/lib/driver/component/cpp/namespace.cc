// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver/component/cpp/namespace.h>

namespace driver {

zx::result<Namespace> Namespace::Create(
    fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry>& entries) {
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_create(&ns);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  Namespace self(ns, std::move(endpoints->client));
  for (auto& entry : entries) {
    std::string path(entry.path().data(), entry.path().size());
    status = fdio_ns_bind(ns, path.data(), entry.directory().TakeChannel().release());
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
  auto result = self.Open(
      "/svc",
      fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kRightWritable,
      endpoints->server.TakeChannel());
  if (result.is_error()) {
    return result.take_error();
  }

  return zx::ok(std::move(self));
}

zx::result<Namespace> Namespace::Create(
    std::vector<fuchsia_component_runner::ComponentNamespaceEntry>& entries) {
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_create(&ns);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  Namespace self(ns, std::move(endpoints->client));
  for (auto& entry : entries) {
    auto path = entry.path();
    auto directory = std::move(entry.directory());
    ZX_ASSERT(path.has_value());
    ZX_ASSERT(directory.has_value());
    status = fdio_ns_bind(ns, path.value().data(), directory.value().TakeChannel().release());
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
  auto result = self.Open(
      "/svc",
      fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kRightWritable,
      endpoints->server.TakeChannel());
  if (result.is_error()) {
    return result.take_error();
  }

  return zx::ok(std::move(self));
}

Namespace::Namespace(fdio_ns_t* ns, fidl::ClientEnd<fuchsia_io::Directory> svc_dir)
    : ns_(ns), svc_dir_(std::move(svc_dir)) {}

Namespace::~Namespace() {
  if (ns_ != nullptr) {
    fdio_ns_destroy(ns_);
  }
}

Namespace::Namespace(Namespace&& other) noexcept
    : ns_(other.ns_), svc_dir_(std::move(other.svc_dir_)) {
  other.ns_ = nullptr;
}

Namespace& Namespace::operator=(Namespace&& other) noexcept {
  this->~Namespace();
  ns_ = other.ns_;
  other.ns_ = nullptr;
  svc_dir_ = std::move(other.svc_dir_);
  return *this;
}

zx::result<> Namespace::Open(const char* path, fuchsia_io::wire::OpenFlags flags,
                             zx::channel server_end) const {
  zx_status_t status = fdio_ns_open(ns_, path, static_cast<uint32_t>(flags), server_end.release());
  return zx::make_result(status);
}

}  // namespace driver
