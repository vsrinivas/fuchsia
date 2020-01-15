// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_MANIFEST_H_
#define GARNET_LIB_FAR_MANIFEST_H_

#include "src/lib/fxl/strings/string_view.h"

namespace archive {
class ArchiveWriter;

bool ReadManifest(fxl::StringView path, ArchiveWriter* writer);

}  // namespace archive

#endif  // GARNET_LIB_FAR_MANIFEST_H_
