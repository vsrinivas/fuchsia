// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_SKIA_SKIA_VMO_DATA_H_
#define APPS_MOZART_LIB_SKIA_SKIA_VMO_DATA_H_

#include <zx/vmo.h>

#include "third_party/skia/include/core/SkData.h"

namespace mozart {

// Makes an |SkData| object backed by a virtual memory object which is mapped
// read only.  Does not take ownership of the handle.
// Returns nullptr on failure.
sk_sp<SkData> MakeSkDataFromVMO(const zx::vmo& vmo);

}  // namespace mozart

#endif  // APPS_MOZART_LIB_SKIA_SKIA_VMO_DATA_H_
