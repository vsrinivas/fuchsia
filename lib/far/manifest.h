// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_LIB_FAR_MANIFEST_H_
#define APPLICATION_LIB_FAR_MANIFEST_H_

#include "lib/ftl/strings/string_view.h"

namespace archive {
class ArchiveWriter;

bool ReadManifest(ftl::StringView path, ArchiveWriter* writer);

}  // namespace archive

#endif  // APPLICATION_LIB_FAR_MANIFEST_H_
