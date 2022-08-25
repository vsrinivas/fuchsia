// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UTF_UTILS_INTERNAL_SCALAR_H_
#define LIB_UTF_UTILS_INTERNAL_SCALAR_H_

#include <lib/stdcompat/bit.h>

#include <cstdint>
#include <cstring>

namespace utfutils {
namespace internal {

bool IsValidUtf8Scalar(const char* data, size_t size);

bool ValidateAndCopyUtf8Scalar(const char* src, char* dst, size_t size);

}  // namespace internal
}  // namespace utfutils

#endif  // LIB_UTF_UTILS_INTERNAL_SCALAR_H_
