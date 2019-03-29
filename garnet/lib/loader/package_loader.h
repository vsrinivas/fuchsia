// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_LOADER_PACKAGE_LOADER_H_
#define GARNET_LIB_LOADER_PACKAGE_LOADER_H_

#include <string>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/zx/vmo.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/io/fd.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace component {

// PackageLoader is an abstract base class for subclasses that wish to
// implement fuchsia::sys::Loader.
//
// It provides common facilities for loading a component from various sources.
// Subclasses should override |LoadUrl|, which implements the core behavior to
// load a component from a package in pkgfs.
class PackageLoader : public fuchsia::sys::Loader {
 public:
  PackageLoader();
  ~PackageLoader() override;

  // Tries to locate a resource at the given URL, and then invokes the callback
  // with the package and any associated resource data.
  void LoadUrl(std::string url, LoadUrlCallback callback) override;

  // Binds FIDL requests to Loader.
  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::Loader> request);

 private:
  bool LoadResource(const fxl::UniqueFD& dir, const std::string path,
                    fuchsia::sys::Package& package);

  fidl::BindingSet<fuchsia::sys::Loader> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PackageLoader);
};

}  // namespace component

#endif  // GARNET_LIB_LOADER_PACKAGE_LOADER_H_
