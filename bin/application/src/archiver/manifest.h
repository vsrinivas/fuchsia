// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_ARCHIVER_MANIFEST_H_
#define APPLICATION_SRC_ARCHIVER_MANIFEST_H_

namespace archive {
class ArchiveWriter;

bool ReadManifest(const char* path, ArchiveWriter* writer);

}  // namespace archive

#endif  // APPLICATION_SRC_ARCHIVER_MANIFEST_H_
