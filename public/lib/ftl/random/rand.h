// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FTL_RANDOM_RAND_H_
#define FTL_RANDOM_RAND_H_

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace ftl {

// Returns a random number in range [0, UINT64_MAX]
uint64_t RandUint64();

bool RandBytes(unsigned char* output, size_t output_length);

}  // namespace ftl

#endif //FTL_RANDOM_RAND_H_
