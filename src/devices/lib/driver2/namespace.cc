// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/namespace.h"

namespace driver {

namespace internal {

zx::status<> DirectoryOpenFunc(zx::unowned_channel dir, fidl::StringView path, zx::channel remote) {
  constexpr fuchsia_io::wire::OpenFlags flags =
      fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kRightWritable;
  fidl::UnownedClientEnd<fuchsia_io::Directory> dir_end(dir);
  fidl::ServerEnd<fuchsia_io::Node> node_end(std::move(remote));
  fidl::WireResult<fuchsia_io::Directory::Open> result =
      fidl::WireCall<fuchsia_io::Directory>(dir_end)->Open(flags, 0755u, path, std::move(node_end));
  return zx::make_status(result.status());
}

}  // namespace internal

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

zx::status<> Namespace::Connect(std::string_view path, zx::channel server_end,
                                fuchsia_io::wire::OpenFlags flags) const {
  zx_status_t status =
      fdio_ns_connect(ns_, path.data(), static_cast<uint32_t>(flags), server_end.release());
  return zx::make_status(status);
}

}  // namespace driver
