// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_LIB_VIEW_FRAMEWORK_SKIA_SKIA_VMO_DATA_H_
#define APPS_MOZART_EXAMPLES_LIB_VIEW_FRAMEWORK_SKIA_SKIA_VMO_DATA_H_

#include <magenta/process.h>
#include <utility>

#include "third_party/skia/include/core/SkData.h"

namespace mojo {
namespace ui {

// Makes an |SkData| object backed by a virtual memory object.
// Does not take ownership of the handle.
sk_sp<SkData> MakeSkDataFromVMO(mx_handle_t vmo);

}  // namespace ui
}  // namespace mojo

#endif  // APPS_MOZART_EXAMPLES_LIB_VIEW_FRAMEWORK_SKIA_SKIA_VMO_DATA_H_
