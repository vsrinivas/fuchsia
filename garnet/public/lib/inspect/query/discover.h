// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_QUERY_DISCOVER_H_
#define LIB_INSPECT_QUERY_DISCOVER_H_

#include <vector>

#include "location.h"

namespace inspect {

// Synchronously find all inspect locations on the file system under the given
// path.
std::vector<Location> SyncFindPaths(const std::string& path);

// Synchronously find all inspect locations at any of the given globbed paths.
std::vector<Location> SyncSearchGlobs(const std::vector<std::string>& globs);

}  // namespace inspect

#endif  // LIB_INSPECT_QUERY_DISCOVER_H_
