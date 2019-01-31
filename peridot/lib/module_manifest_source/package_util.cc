// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/package_util.h"

#include <lib/fxl/strings/string_printf.h>

namespace modular {

std::string GetModuleManifestPathFromPackage(fxl::StringView package_name,
                                             fxl::StringView package_version) {
  return fxl::StringPrintf("/pkgfs/packages/%s/%s/meta/module.json",
                           package_name.data(), package_version.data());
}

}  // namespace modular
