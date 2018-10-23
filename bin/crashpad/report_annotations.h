// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CRASHPAD_REPORT_ANNOTATIONS_H_
#define GARNET_BIN_CRASHPAD_REPORT_ANNOTATIONS_H_

#include <map>
#include <string>

namespace fuchsia {
namespace crash {

// Most annotations are shared between userspace and kernel crashes.
// Add additional arguments to this function for values that differ between the
// two, e.g., the package name can be extracted from the crashing process in
// userspace, but it's just "kernel" in kernel space.
std::map<std::string, std::string> MakeAnnotations(
    const std::string& package_name);

}  // namespace crash
}  // namespace fuchsia

#endif  // GARNET_BIN_CRASHPAD_REPORT_ANNOTATIONS_H_
