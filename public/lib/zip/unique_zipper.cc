// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zip/unique_zipper.h"

#include "third_party/zlib/contrib/minizip/zip.h"

namespace zip {

void UniqueZipperTraits::Free(void* file) {
  zipClose(file, nullptr);
}

}  // namespace zip
