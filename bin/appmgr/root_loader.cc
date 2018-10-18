// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/root_loader.h"

#include <fcntl.h>
#include <trace/event.h>

#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"

namespace component {

RootLoader::RootLoader() = default;
RootLoader::~RootLoader() = default;

bool RootLoader::LoadComponentFromPkgfs(FuchsiaPkgUrl component_url,
                                        LoadComponentCallback callback) {
  const std::string& pkg_path = component_url.pkgfs_dir_path();
  TRACE_DURATION("appmgr", "RootLoader::LoadComponentFromPkgfs",
                 "component_url", component_url.ToString(), "pkg_path",
                 pkg_path);
  return LoadPackage(component_url, callback);
}

}  // namespace component
