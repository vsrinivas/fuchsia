// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTESTS_UTILS_PROFILE_H_
#define RUNTESTS_UTILS_PROFILE_H_

#include <inttypes.h>
#include <stddef.h>

namespace runtests {

// Returns true if raw profiles |src| and |dst| are structurally compatible.
bool ProfilesCompatible(const uint8_t* src, uint8_t* dst, size_t size);

// Merges raw profiles |src| and |dst| into |dst|.
//
// Note that this function does not check whether the profiles are compatible.
uint8_t* MergeProfiles(const uint8_t* src, uint8_t* dst, size_t size);

}  // namespace runtests

#endif  // RUNTESTS_UTILS_PROFILE_H_
