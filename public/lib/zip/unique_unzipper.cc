// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zip/unique_unzipper.h"

#include "third_party/zlib/contrib/minizip/unzip.h"

namespace zip {

void UniqueUnzipperTraits::Free(void* file) {
  unzClose(file);
}

}  // namespace zip
