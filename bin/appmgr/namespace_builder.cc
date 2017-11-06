// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/namespace_builder.h"

#include <zircon/processargs.h>
#include <fdio/limits.h>
#include <fdio/util.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib/fxl/files/unique_fd.h"

namespace app {
namespace {

zx::channel CloneChannel(int fd) {
  zx_handle_t handle[FDIO_MAX_HANDLES];
  uint32_t type[FDIO_MAX_HANDLES];

  zx_status_t r = fdio_clone_fd(fd, 0, handle, type);
  if (r < 0 || r == 0)
    return zx::channel();

  if (type[0] != PA_FDIO_REMOTE) {
    for (int i = 0; i < r; ++i)
      zx_handle_close(handle[i]);
    return zx::channel();
  }

  // Close any extra handles.
  for (int i = 1; i < r; ++i)
    zx_handle_close(handle[i]);

  return zx::channel(handle[0]);
}

}  // namespace

NamespaceBuilder::NamespaceBuilder() = default;

NamespaceBuilder::~NamespaceBuilder() = default;

void NamespaceBuilder::AddFlatNamespace(FlatNamespacePtr ns) {
  if (!ns.is_null() && ns->paths.size() == ns->directories.size()) {
    for (size_t i = 0; i < ns->paths.size(); ++i) {
      AddDirectoryIfNotPresent(ns->paths[i], std::move(ns->directories[i]));
    }
  }
}

void NamespaceBuilder::AddRoot() {
  PushDirectoryFromPath("/", O_RDWR);
}

void NamespaceBuilder::AddPackage(zx::channel package) {
  PushDirectoryFromChannel("/pkg", std::move(package));
}

void NamespaceBuilder::AddDirectoryIfNotPresent(const std::string& path,
                                                zx::channel directory) {
  if (std::find(paths_.begin(), paths_.end(), path) != paths_.end())
    return;
  PushDirectoryFromChannel(path, std::move(directory));
}

void NamespaceBuilder::AddServices(zx::channel services) {
  PushDirectoryFromChannel("/svc", std::move(services));
}

void NamespaceBuilder::AddDev() {
  PushDirectoryFromPath("/dev", O_RDWR);
}

void NamespaceBuilder::AddSandbox(const SandboxMetadata& sandbox) {
  if (sandbox.dev().empty())
    return;

  fxl::UniqueFD dir(open("/dev", O_DIRECTORY | O_RDWR));
  if (!dir.is_valid())
    return;
  const auto& dev = sandbox.dev();
  for (const auto& path : dev) {
    fxl::UniqueFD entry(openat(dir.get(), path.c_str(), O_DIRECTORY | O_RDWR));
    if (!entry.is_valid())
      continue;
    zx::channel handle = CloneChannel(entry.get());
    if (!handle)
      continue;
    PushDirectoryFromChannel("/dev/" + path, std::move(handle));
  }

  for (const auto& feature : sandbox.features()) {
    if (feature == "vulkan") {
      PushDirectoryFromPath("/dev/class/display", O_RDWR);
      PushDirectoryFromPath("/system/data/vulkan", O_RDONLY);
    } else if (feature == "root-ssl-certificates") {
      PushDirectoryFromPath("/system/data/boringssl", O_RDONLY);
      PushDirectoryFromPathAs("/system/data/boringssl", "/etc/ssl", O_RDONLY);
    }
  }
}

void NamespaceBuilder::PushDirectoryFromPath(std::string path, int oflags) {
  PushDirectoryFromPathAs(path, path, oflags);
}

void NamespaceBuilder::PushDirectoryFromPathAs(std::string src_path,
                                               std::string dst_path ,
                                               int oflags) {
if (std::find(paths_.begin(), paths_.end(), dst_path) != paths_.end())
    return;
  fxl::UniqueFD dir(open(src_path.c_str(), O_DIRECTORY | oflags));
  if (!dir.is_valid())
    return;
  zx::channel handle = CloneChannel(dir.get());
  if (!handle)
    return;
  PushDirectoryFromChannel(std::move(dst_path), std::move(handle));
}

void NamespaceBuilder::PushDirectoryFromChannel(std::string path,
                                                zx::channel channel) {
  FXL_DCHECK(std::find(paths_.begin(), paths_.end(), path) == paths_.end());
  types_.push_back(PA_HND(PA_NS_DIR, types_.size()));
  handles_.push_back(channel.get());
  paths_.push_back(std::move(path));

  handle_pool_.push_back(std::move(channel));
}

fdio_flat_namespace_t* NamespaceBuilder::Build() {
  path_data_.resize(paths_.size());
  for (size_t i = 0; i < paths_.size(); ++i)
    path_data_[i] = paths_[i].c_str();

  flat_ns_.count = types_.size();
  flat_ns_.handle = handles_.data();
  flat_ns_.type = types_.data();
  flat_ns_.path = path_data_.data();
  Release();
  return &flat_ns_;
}

FlatNamespacePtr NamespaceBuilder::BuildForRunner() {
  auto flat_namespace = FlatNamespace::New();
  for (auto& path : paths_) {
    flat_namespace->paths.push_back(std::move(path));
  }
  for (auto& handle : handle_pool_) {
    flat_namespace->directories.push_back(std::move(handle));
  }
  return flat_namespace;
}

void NamespaceBuilder::Release() {
  for (auto& handle : handle_pool_)
    (void)handle.release();
  handle_pool_.clear();
}

}  // namespace app
