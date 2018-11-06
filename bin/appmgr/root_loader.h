// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_ROOT_LOADER_H_
#define GARNET_BIN_APPMGR_ROOT_LOADER_H_

#include <fuchsia/sys/cpp/fidl.h>

#include "garnet/lib/loader/component_loader.h"
#include "lib/pkg_url/fuchsia_pkg_url.h"
#include "lib/fxl/macros.h"

namespace component {

// Component loader in the root realm. Loads packages from pkgfs.
class RootLoader final : public ComponentLoader {
 public:
  RootLoader();
  ~RootLoader() override;

 private:
  bool LoadComponentFromPkgfs(FuchsiaPkgUrl component_url,
                              LoadComponentCallback callback) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(RootLoader);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_ROOT_LOADER_H_
