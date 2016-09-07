// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zip/create_unzipper.h"

#include "lib/zip/memory_io.h"
#include "third_party/zlib/contrib/minizip/unzip.h"

namespace zip {

UniqueUnzipper CreateUnzipper(std::vector<char>* buffer) {
  zlib_filefunc_def io = internal::kMemoryIO;
  io.opaque = buffer;
  return UniqueUnzipper(unzOpen2(nullptr, &io));
}

}  // namespace zip
