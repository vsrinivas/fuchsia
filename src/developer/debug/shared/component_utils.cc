// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/component_utils.h"

namespace debug_ipc {

namespace {

const char kUrlPackagePreamble[] = "fuchsia-pkg://fuchsia.com/";
const char kUrlComponentPreamble[] = "#meta/";

}  // namespace

bool ExtractComponentFromPackageUrl(const std::string& url,
                                    ComponentDescription* out) {
  // We check if we're matching a pkg url
  // Pattern:
  //
  // fuchsia-pkg://fuchsia.com/<PKG>#meta/<COMPONENT>.cmx
  size_t package_preamble_start = url.find(kUrlPackagePreamble);
  if (package_preamble_start == std::string::npos)
    return false;

  // Account for last \0.
  size_t package_start =
      package_preamble_start + sizeof(kUrlPackagePreamble) - 1;
  size_t package_end = url.find(kUrlComponentPreamble, package_start);
  if (package_end == std::string::npos)
    return false;

  // Account for last \0.
  size_t component_start = package_end + sizeof(kUrlComponentPreamble) - 1;
  size_t component_end = url.find(".cmx", component_start);
  if (component_end == std::string::npos)
    return false;

  out->package_name = url.substr(package_start, package_end - package_start);
  out->component_name =
      url.substr(component_start, component_end - component_start);
  return true;
}

}  // namespace debug_ipc
