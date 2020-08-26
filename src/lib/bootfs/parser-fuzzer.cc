// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <zircon/boot/bootfs.h>

#include "src/lib/bootfs/parser.h"

namespace {

extern "C" int LLVMFuzzerTestOneInput(void* Data, size_t Size) {
  zx::vmo vmo;
  zx::vmo::create(Size, 0, &vmo);
  vmo.write(Data, 0, Size);
  bootfs::Parser parser;
  parser.Init(zx::unowned_vmo(vmo));
  parser.Parse([](const zbi_bootfs_dirent_t* entry) { return ZX_OK; });
  return 0;
}

}  // namespace
