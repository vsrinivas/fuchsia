// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/utf-utils/internal/arm-neon.h>
#include <lib/utf-utils/internal/generic-simd.h>
#include <lib/utf-utils/internal/scalar.h>
#include <lib/utf-utils/internal/x86-avx2.h>
#include <lib/utf-utils/internal/x86-ssse3.h>
#include <lib/utf-utils/utf-utils.h>

#include <cstdint>
#include <cstring>

bool utfutils_is_valid_utf8(const char* data, size_t size) {
  // Function multiversioning (if ARM is supported in all compilers) or dynamic dispatch may be
  // useful here.

#ifdef __AVX2__
  return ::utfutils::internal::IsValidUtf8Simd<::utfutils::internal::x86::Avx2>(data, size);
#elif __SSE4_1__
  return ::utfutils::internal::IsValidUtf8Simd<::utfutils::internal::x86::Ssse3>(data, size);
#elif __ARM_NEON
  return ::utfutils::internal::IsValidUtf8Simd<::utfutils::internal::arm::Neon>(data, size);
#else
  // Default to scalar implementation for other architectures
  return ::utfutils::internal::IsValidUtf8Scalar(data, size);
#endif
}

bool utfutils_validate_and_copy_utf8(const char* src, char* dst, size_t size) {
#ifdef __AVX2__
  return ::utfutils::internal::ValidateAndCopyUtf8Simd<::utfutils::internal::x86::Avx2>(src, dst,
                                                                                        size);
#elif __SSE4_1__
  return ::utfutils::internal::ValidateAndCopyUtf8Simd<::utfutils::internal::x86::Ssse3>(src, dst,
                                                                                         size);
#elif __ARM_NEON
  return ::utfutils::internal::ValidateAndCopyUtf8Simd<::utfutils::internal::arm::Neon>(src, dst,
                                                                                        size);
#else
  // Default to scalar implementation for other architectures
  return ::utfutils::internal::ValidateAndCopyUtf8Scalar(src, dst, size);
#endif
}
