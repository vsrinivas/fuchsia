// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VIRTUALIZATION_TESTING_GUEST_CID_H_
#define LIB_VIRTUALIZATION_TESTING_GUEST_CID_H_

#include <fuchsia/virtualization/cpp/fidl.h>

namespace guest {
namespace testing {

// Only one guest is supported per (soon to be deprecated, see fxr/97355) host vsock, and it will
// always use the default guest CID.
constexpr uint32_t kGuestCid = fuchsia::virtualization::DEFAULT_GUEST_CID;

}  // namespace testing
}  // namespace guest

#endif  // LIB_VIRTUALIZATION_TESTING_GUEST_CID_H_
