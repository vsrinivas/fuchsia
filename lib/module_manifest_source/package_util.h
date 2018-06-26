// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_SOURCE_PACKAGE_UTIL_H_
#define PERIDOT_LIB_MODULE_MANIFEST_SOURCE_PACKAGE_UTIL_H_

#include <string>

#include "lib/fxl/strings/string_view.h"

namespace modular {

std::string GetModuleManifestPathFromPackage(fxl::StringView package_name,
                                             fxl::StringView package_version);

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_SOURCE_PACKAGE_UTIL_H_
