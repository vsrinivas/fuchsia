// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_ROOT_LOADER_H_
#define GARNET_BIN_APPMGR_ROOT_LOADER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/zx/vmo.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/macros.h"

namespace component {

// Class to load components at their provided URLs.
class RootLoader : public fuchsia::sys::Loader {
 public:
  explicit RootLoader();
  ~RootLoader() override;

  // Tries to locate a component at the given URL, and then invokes the callback
  // on said component.
  void LoadComponent(fidl::StringPtr url,
                     LoadComponentCallback callback) override;

  // Binds FIDL requests to Loader.
  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::Loader> request);

 private:
  fidl::BindingSet<fuchsia::sys::Loader> bindings_;

  // package_name: Name of the package this cmx resides in. E.g. sysmgr.
  // path: Absolute path to the cmx. E.g. /pkgfs/packages/sysmgr/0/sysmgr.cmx.
  bool LoadComponentFromComponentManifest(std::string& package_name,
                                          std::string& path,
                                          LoadComponentCallback callback);

  // package_name: Name of the package this cmx resides in. E.g. sysmgr.
  bool LoadComponentFromPackage(std::string& package_name,
                                LoadComponentCallback callback);

  // target_path: Absolute path of the target cmx or package. E.g.
  // /pkgfs/packages/sysmgr/0/sysmgr.cmx, or /pkgfs/packages/sysmgr/0.
  // pkg_path: Absolute path to the package root. E.g. /pkgfs/packages/sysmgr/0.
  bool LoadComponentFromPkgfs(std::string& target_path, std::string& pkg_path,
                              LoadComponentCallback callback);

  bool LoadComponentWithProcess(fxl::UniqueFD& fd, std::string& path,
                                LoadComponentCallback callback);

  FXL_DISALLOW_COPY_AND_ASSIGN(RootLoader);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_ROOT_LOADER_H_
