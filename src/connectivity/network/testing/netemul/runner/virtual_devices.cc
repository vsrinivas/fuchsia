// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_devices.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <fs/service.h>

#include "src/lib/fxl/strings/split_string.h"

namespace netemul {

VirtualDevices::VirtualDevices()
    : vdev_vfs_(async_get_default_dispatcher()), dir_(fbl::AdoptRef(new fs::PseudoDir())) {}

void VirtualDevices::AddEntry(std::string path, fidl::InterfacePtr<DevProxy> dev) {
  auto components = fxl::SplitString(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                                     fxl::SplitResult::kSplitWantNonEmpty);
  if (components.empty()) {
    FX_LOGS(ERROR) << "Invalid device mount path '" << path << "'";
    return;
  }

  auto filename = *(components.end() - 1);
  components.pop_back();
  fbl::RefPtr<fs::PseudoDir> dir = GetDirectory(std::move(components));

  dev.set_error_handler([this, path](zx_status_t status) mutable {
    // when we get an error to the device server channel, we
    // must remove it from vfs.
    // NOTE(brunodalbo) When we call RemoveEntry, this lambda will get destroyed because `dev` is
    // owned by the `fs::Service` that will get deallocated as a result of RemoveEntry. That means
    // that `path` will also get deallocated, which can cause a use after free bug in RemoveEntry (a
    // previous version of Remove Entry received `const string&` as opposed to string). Moving
    // `path` into `RemoveEntry`, transfers ownership and avoids the use after free without having
    // to copy `path`.
    RemoveEntry(std::move(path));
  });

  auto status = dir->AddEntry(
      filename, fbl::AdoptRef(new fs::Service([dev = std::move(dev), path](zx::channel chann) {
        if (!dev.is_bound()) {
          return ZX_ERR_PEER_CLOSED;
        }
        dev->ServeDevice(std::move(chann));
        return ZX_OK;
      })));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Can't add device entry " << path << ": " << zx_status_get_string(status);
  }
}

void VirtualDevices::RemoveEntry(std::string path) {
  auto components = fxl::SplitString(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                                     fxl::SplitResult::kSplitWantNonEmpty);
  if (components.empty()) {
    FX_LOGS(ERROR) << "Invalid device mount path '" << path << "'";
    return;
  }

  auto filename = *(components.end() - 1);
  components.pop_back();
  fbl::RefPtr<fs::PseudoDir> dir = GetDirectory(std::move(components));

  auto status = dir->RemoveEntry(filename);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Can't remove device entry " << path << ": " << zx_status_get_string(status);
  }
}

fbl::RefPtr<fs::PseudoDir> VirtualDevices::GetDirectory(std::vector<std::string_view> parts) {
  fbl::RefPtr<fs::PseudoDir> dir = dir_;

  if (parts.empty()) {
    // The root directory already exists.
    return dir;
  }

  for (auto i = parts.begin(); i != parts.end(); i++) {
    fbl::RefPtr<fs::Vnode> node;
    if (dir->Lookup(*i, &node) == ZX_OK) {
      dir.reset(reinterpret_cast<fs::PseudoDir*>(node.get()));
    } else {
      auto ndir = fbl::AdoptRef(new fs::PseudoDir());
      auto status = dir->AddEntry(*i, ndir);
      FX_CHECK(status == ZX_OK) << "Error creating mount path: " << *i << ": "
                                << zx_status_get_string(status);
      dir = ndir;
    }
  }

  return dir;
}

zx::channel VirtualDevices::OpenAsDirectory(std::string path) {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK) {
    return zx::channel();
  }

  auto parts = fxl::SplitString(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                                fxl::SplitResult::kSplitWantNonEmpty);
  if (vdev_vfs_.ServeDirectory(GetDirectory(std::move(parts)), std::move(h1)) != ZX_OK) {
    return zx::channel();
  }
  return h2;
}

}  // namespace netemul
