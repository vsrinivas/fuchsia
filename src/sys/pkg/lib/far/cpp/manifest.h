// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_PKG_LIB_FAR_CPP_MANIFEST_H_
#define SRC_SYS_PKG_LIB_FAR_CPP_MANIFEST_H_

#include <string_view>

namespace archive {
class ArchiveWriter;

bool ReadManifest(std::string_view path, ArchiveWriter* writer);

}  // namespace archive

#endif  // SRC_SYS_PKG_LIB_FAR_CPP_MANIFEST_H_
