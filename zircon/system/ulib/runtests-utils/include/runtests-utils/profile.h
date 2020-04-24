// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTESTS_UTILS_PROFILE_H_
#define RUNTESTS_UTILS_PROFILE_H_

#include <cinttypes>
#include <cstddef>
#include <string_view>

namespace runtests {

constexpr char kProfileSink[] = "llvm-profile";

// Returns true if raw profiles |src| and |dst| are structurally compatible.
bool ProfilesCompatible(const uint8_t* dst, const uint8_t* src, size_t size);

// Merges raw profiles |src| and |dst| into |dst|.
//
// Note that this function does not check whether the profiles are compatible.
uint8_t* MergeProfiles(uint8_t* dst, const uint8_t* src, size_t size);

}  // namespace runtests

#endif  // RUNTESTS_UTILS_PROFILE_H_
