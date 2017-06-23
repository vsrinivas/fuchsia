// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/src/manager/namespace_builder.h"

#include <magenta/processargs.h>
#include <mxio/limits.h>
#include <mxio/util.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib/ftl/files/unique_fd.h"

namespace app {
namespace {

mx::channel CloneChannel(int fd) {
  mx_handle_t handle[MXIO_MAX_HANDLES];
  uint32_t type[MXIO_MAX_HANDLES];

  mx_status_t r = mxio_clone_fd(fd, 0, handle, type);
  if (r < 0 || r == 0)
    return mx::channel();

  if (type[0] != PA_MXIO_REMOTE) {
    for (int i = 0; i < r; ++i)
      mx_handle_close(handle[i]);
    return mx::channel();
  }

  // Close any extra handles.
  for (int i = 1; i < r; ++i)
    mx_handle_close(handle[i]);

  return mx::channel(handle[0]);
}

}  // namespace

NamespaceBuilder::NamespaceBuilder() = default;

NamespaceBuilder::~NamespaceBuilder() = default;

void NamespaceBuilder::AddRoot(mx::channel root) {
  PushDirectoryFromChannel("/", std::move(root));
}

void NamespaceBuilder::AddPackage(mx::channel package) {
  PushDirectoryFromChannel("/pkg", std::move(package));
}

void NamespaceBuilder::AddServices(mx::channel services) {
  PushDirectoryFromChannel("/svc", std::move(services));
}

void NamespaceBuilder::AddSandbox(const SandboxMetadata& sandbox) {
  if (sandbox.dev().empty())
    return;

  ftl::UniqueFD dir(open("/dev", O_DIRECTORY | O_RDWR));
  if (!dir.is_valid())
    return;
  const auto& dev = sandbox.dev();
  for (const auto& path : dev) {
    ftl::UniqueFD entry(openat(dir.get(), path.c_str(), O_DIRECTORY | O_RDWR));
    if (!entry.is_valid())
      continue;
    mx::channel handle = CloneChannel(entry.get());
    if (!handle)
      continue;
    PushDirectoryFromChannel("/dev/" + path, std::move(handle));
  }

  for (const auto& feature : sandbox.features()) {
    if (feature == "vulkan") {
      PushDirectoryFromPath("/dev/class/display", O_RDWR);
      PushDirectoryFromPath("/system/data/vulkan", O_RDONLY);
    }
  }
}

void NamespaceBuilder::PushDirectoryFromPath(std::string path, int oflags) {
  if (std::find(paths_.begin(), paths_.end(), path) != paths_.end())
    return;
  ftl::UniqueFD dir(open(path.c_str(), O_DIRECTORY | oflags));
  if (!dir.is_valid())
    return;
  mx::channel handle = CloneChannel(dir.get());
  if (!handle)
    return;
  PushDirectoryFromChannel(std::move(path), std::move(handle));
}

void NamespaceBuilder::PushDirectoryFromChannel(std::string path,
                                                mx::channel channel) {
  types_.push_back(PA_HND(PA_NS_DIR, types_.size()));
  handles_.push_back(channel.get());
  paths_.push_back(std::move(path));

  handle_pool_.push_back(std::move(channel));
}

mxio_flat_namespace_t* NamespaceBuilder::Build() {
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

void NamespaceBuilder::Release() {
  for (auto& handle : handle_pool_)
    (void)handle.release();
  handle_pool_.clear();
}

}  // namespace app
