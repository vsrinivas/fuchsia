// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_devices.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace netemul {

VirtualDevices::VirtualDevices()
    : vdev_vfs_(async_get_default_dispatcher()), dir_(fbl::MakeRefCounted<fs::PseudoDir>()) {}

void VirtualDevices::AddEntry(std::string path, fidl::InterfacePtr<DevProxy> dev) {
  auto components = fxl::SplitString(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                                     fxl::SplitResult::kSplitWantNonEmpty);
  if (components.empty()) {
    FX_LOGS(ERROR) << "Invalid device mount path '" << path << "'";
    return;
  }

  fbl::String filename = *(components.end() - 1);
  components.pop_back();
  fbl::RefPtr<fs::PseudoDir> dir = GetDirectory(components);

  // This lambda outlives the caller; it must take ownership of |path| to avoid use-after-free.
  dev.set_error_handler([this, path = std::move(path)](zx_status_t status) mutable {
    // The RemoveEntry call will destroy this lambda; we can't rely on |path| being kept alive by
    // capture alone, so we move it into our stack frame here, which should survive past the
    // destruction of the lambda.
    std::string pathOnStack = std::move(path);

    // When we get an error to the device server channel, we must remove it from vfs.
    RemoveEntry(pathOnStack);
  });

  auto status = dir->AddEntry(
      filename, fbl::MakeRefCounted<fs::Service>([dev = std::move(dev)](zx::channel chan) {
        if (!dev.is_bound()) {
          return ZX_ERR_PEER_CLOSED;
        }
        dev->ServeDevice(std::move(chan));
        return ZX_OK;
      }));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Can't add device entry " << path << ": " << zx_status_get_string(status);
  }
}

void VirtualDevices::RemoveEntry(const std::string& path) {
  auto components = fxl::SplitString(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                                     fxl::SplitResult::kSplitWantNonEmpty);
  if (components.empty()) {
    FX_LOGS(ERROR) << "Invalid device mount path '" << path << "'";
    return;
  }

  std::string_view filename = *(components.end() - 1);
  components.pop_back();
  fbl::RefPtr<fs::PseudoDir> dir = GetDirectory(components);

  auto status = dir->RemoveEntry(filename);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Can't remove device entry " << path << ": " << zx_status_get_string(status);
  }
}

fbl::RefPtr<fs::PseudoDir> VirtualDevices::GetDirectory(
    const std::vector<std::string_view>& parts) {
  fbl::RefPtr<fs::PseudoDir> dir = dir_;

  if (parts.empty()) {
    // The root directory already exists.
    return dir;
  }

  for (const auto& part : parts) {
    fbl::RefPtr<fs::Vnode> node;
    if (dir->Lookup(part, &node) == ZX_OK) {
      dir.reset(reinterpret_cast<fs::PseudoDir*>(node.get()));
    } else {
      auto ndir = fbl::MakeRefCounted<fs::PseudoDir>();
      auto status = dir->AddEntry(part, ndir);
      FX_CHECK(status == ZX_OK) << "Error creating mount path: " << part << ": "
                                << zx_status_get_string(status);
      dir = ndir;
    }
  }

  return dir;
}

zx::status<fidl::ClientEnd<fuchsia_io::Directory>> VirtualDevices::OpenAsDirectory(
    const std::string& path) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return zx::error_status(endpoints.status_value());
  }
  auto [client, server] = std::move(endpoints.value());

  auto parts = fxl::SplitString(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                                fxl::SplitResult::kSplitWantNonEmpty);
  zx_status_t status = vdev_vfs_.ServeDirectory(GetDirectory(parts), std::move(server));
  if (status != ZX_OK) {
    return zx::error_status(status);
  }
  return zx::ok(std::move(client));
}

}  // namespace netemul
