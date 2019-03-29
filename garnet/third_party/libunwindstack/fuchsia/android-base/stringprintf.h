// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_ANDROID_BASE_STRINGPRINTF_H_
#define GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_ANDROID_BASE_STRINGPRINTF_H_

#include "src/lib/fxl/strings/string_printf.h"

// This just forwards android::base::StringPrintf to the FXL implementation.

namespace android {
namespace base {

using fxl::StringPrintf;

}  // namespace base
}  // namespace android

#endif  // GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_ANDROID_BASE_STRINGPRINTF_H_
