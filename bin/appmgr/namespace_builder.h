// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_NAMESPACE_BUILDER_H_
#define GARNET_BIN_APPMGR_NAMESPACE_BUILDER_H_

#include <zx/channel.h>
#include <fdio/namespace.h>

#include <vector>

#include "lib/app/fidl/flat_namespace.fidl.h"
#include "garnet/bin/appmgr/sandbox_metadata.h"
#include "lib/fxl/macros.h"

namespace app {

class NamespaceBuilder {
 public:
  NamespaceBuilder();
  ~NamespaceBuilder();

  void AddFlatNamespace(FlatNamespacePtr flat_namespace);
  void AddRoot();
  void AddPackage(zx::channel package);
  void AddDirectoryIfNotPresent(const std::string& path, zx::channel directory);
  void AddServices(zx::channel services);
  void AddDev();
  void AddSandbox(const SandboxMetadata& sandbox);

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
  FlatNamespacePtr BuildForRunner();

 private:
  void PushDirectoryFromPath(std::string path, int oflags);
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

}  // namespace app

#endif  // GARNET_BIN_APPMGR_NAMESPACE_BUILDER_H_
