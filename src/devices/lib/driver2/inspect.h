// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_INSPECT_H_
#define SRC_DEVICES_LIB_DRIVER2_INSPECT_H_

#include <lib/inspect/cpp/inspect.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace driver {

zx::status<zx::vmo> ExposeInspector(const inspect::Inspector& inspector,
                                    const fbl::RefPtr<fs::PseudoDir>& dir);

}

#endif  // SRC_DEVICES_LIB_DRIVER2_INSPECT_H_
