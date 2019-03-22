// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

// Collection of utilities meant for easier handling components.

namespace debug_ipc {

// Struct meant to hold an over-arching description of a component.
// This struct is meant to grow over time.
struct ComponentDescription {
  std::string package_name;
  std::string component_name;
};

// Will attempt to extract component names from a package url.
// Url has the following format:
//
// fuchsia-pkg://fuchsia.com/<package_name>#meta/<component_name>.cmx
//
// Returns false if the url doesn't contain that format.
// |out| is untouched in that case.
bool ExtractComponentFromPackageUrl(const std::string& url,
                                    ComponentDescription* out);

}  // namespace debug_ipc
