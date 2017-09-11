// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_MANAGER_NAMESPACE_BUILDER_H_
#define APPLICATION_SRC_MANAGER_NAMESPACE_BUILDER_H_

#include <mx/channel.h>
#include <mxio/namespace.h>

#include <vector>

#include "lib/app/fidl/flat_namespace.fidl.h"
#include "garnet/bin/appmgr/sandbox_metadata.h"
#include "lib/ftl/macros.h"

namespace app {

class NamespaceBuilder {
 public:
  NamespaceBuilder();
  ~NamespaceBuilder();

  void AddFlatNamespace(FlatNamespacePtr flat_namespace);
  void AddRoot();
  void AddPackage(mx::channel package);
  void AddDirectoryIfNotPresent(const std::string& path, mx::channel directory);
  void AddServices(mx::channel services);
  void AddSandbox(const SandboxMetadata& sandbox);

  // Returns an mxio_flat_namespace_t representing the built namespace.
  //
  // The returned mxio_flat_namespace_t has ownership of the mx::channel objects
  // added to the namespace but does not have ownership of the memory for the
  // mxio_flat_namespace_t or the memory pointed to by its |handle|, |type|, or
  // |path| properties. The NamespaceBuilder will free that memory in its
  // destructor.
  //
  // Build() can be called only once for each builder. None of the "add" methods
  // can be called after Build().
  mxio_flat_namespace_t* Build();

  // Similar to Build() but returns a FIDL struct with ownership of all
  // mx:channel that are part of this namespace.
  FlatNamespacePtr BuildForRunner();

 private:
  void PushDirectoryFromPath(std::string path, int oflags);
  void PushDirectoryFromChannel(std::string path, mx::channel channel);
  void Release();

  std::vector<uint32_t> types_;
  std::vector<mx_handle_t> handles_;
  std::vector<std::string> paths_;

  std::vector<mx::channel> handle_pool_;
  std::vector<const char*> path_data_;
  mxio_flat_namespace_t flat_ns_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NamespaceBuilder);
};

}  // namespace app

#endif  // APPLICATION_SRC_MANAGER_NAMESPACE_BUILDER_H_
