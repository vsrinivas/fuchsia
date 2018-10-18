// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_LOADER_COMPONENT_LOADER_H_
#define GARNET_LIB_LOADER_COMPONENT_LOADER_H_

#include <string>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/zx/vmo.h>
#include "garnet/lib/pkg_url/fuchsia_pkg_url.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/macros.h"

namespace component {

// ComponentLoader is an abstract base class for subclasses that wish to
// implement fuchsia::sys::Loader.
//
// It provides common facilities for loading a component from various sources.
// Subclasses should override |LoadComponentFromPkgFs|, which controls the
// core behavior to load a component from a package in pkgfs.
class ComponentLoader : public fuchsia::sys::Loader {
 public:
  ComponentLoader();
  ~ComponentLoader() override;

  // Tries to locate a component at the given URL, and then invokes the callback
  // on said component.
  void LoadComponent(fidl::StringPtr url,
                     LoadComponentCallback callback) override;

  // Binds FIDL requests to Loader.
  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::Loader> request);

 protected:
  static bool LoadPackage(const FuchsiaPkgUrl& component_url,
                          LoadComponentCallback callback);

 private:
  // Abstract method to load a component from pkgfs, given the component url.
  virtual bool LoadComponentFromPkgfs(FuchsiaPkgUrl resolved_url,
                                      LoadComponentCallback callback) = 0;


  bool LoadComponentFromPackage(const std::string& package_name,
                                LoadComponentCallback callback);
  bool LoadComponentWithProcess(fxl::UniqueFD fd, const std::string& path,
                                LoadComponentCallback callback);

  fidl::BindingSet<fuchsia::sys::Loader> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentLoader);
};

}  // namespace component

#endif  // GARNET_LIB_LOADER_COMPONENT_LOADER_H_
