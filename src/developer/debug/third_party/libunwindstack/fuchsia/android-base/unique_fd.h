// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_ANDROID_BASE_UNIQUE_FD_H_
#define GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_ANDROID_BASE_UNIQUE_FD_H_

#include "src/lib/files/unique_fd.h"

// This just forwards android::base::UniqueFD to the FXL implementation.

namespace android {
namespace base {

using unique_fd = fbl::unique_fd;

}  // namespace base
}  // namespace android

#endif  // GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_ANDROID_BASE_UNIQUE_FD_H_
