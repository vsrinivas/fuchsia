// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_NAMESPACE_BUILDER_H_
#define GARNET_BIN_APPMGR_NAMESPACE_BUILDER_H_

#include <lib/fdio/namespace.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include <vector>

#include "garnet/bin/appmgr/realm.h"
#include "garnet/lib/cmx/sandbox.h"
#include "lib/fxl/macros.h"

#include <fuchsia/sys/cpp/fidl.h>

namespace component {

class NamespaceBuilder {
 public:
  NamespaceBuilder();
  ~NamespaceBuilder();

  void AddFlatNamespace(fuchsia::sys::FlatNamespacePtr flat_namespace);
  void AddPackage(zx::channel package);
  void AddDirectoryIfNotPresent(const std::string& path, zx::channel directory);
  void AddServices(zx::channel services);

  // A factory function that returns a new directory that /hub points to.
  using HubDirectoryFactory = fit::function<zx::channel()>;
  void AddSandbox(const SandboxMetadata& sandbox,
                  const HubDirectoryFactory& hub_directory_factory);

  // This function grants access to a number of directories to processes that
  // lack a sandbox policy. Once every application has a proper sandbox policy
  // we should be able to remove this function.
  void AddDeprecatedDefaultDirectories();

  // Returns an fdio_flat_namespace_t representing the built namespace.
  //
  // The returned fdio_flat_namespace_t has ownership of the zx::channel objects
  // added to the namespace but does not have ownership of the memory for the
  // fdio_flat_namespace_t or the memory pointed to by its |handle|, |type|, or
  // |path| properties. The NamespaceBuilder will free that memory in its
  // destructor.
  //
  // Build() can be called only once for each builder. None of the "add" methods
  // can be called after Build().
  fdio_flat_namespace_t* Build();

  // Similar to Build() but returns a FIDL struct with ownership of all
  // zx:channel that are part of this namespace.
  fuchsia::sys::FlatNamespace BuildForRunner();

 private:
  void PushDirectoryFromPath(std::string path);
  void PushDirectoryFromPathAs(std::string src_path, std::string dst_path);
  void PushDirectoryFromPathIfNotPresent(std::string path);
  void PushDirectoryFromChannel(std::string path, zx::channel channel);
  void Release();

  std::vector<uint32_t> types_;
  std::vector<zx_handle_t> handles_;
  std::vector<std::string> paths_;

  std::vector<zx::channel> handle_pool_;
  std::vector<const char*> path_data_;
  fdio_flat_namespace_t flat_ns_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NamespaceBuilder);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_NAMESPACE_BUILDER_H_
