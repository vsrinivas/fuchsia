// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_THIRD_PARTY_ULIB_MUSL_SRC_INTERNAL_HWASAN_IMPL_H_
#define ZIRCON_THIRD_PARTY_ULIB_MUSL_SRC_INTERNAL_HWASAN_IMPL_H_

#include <zircon/compiler.h>
#include <zircon/sanitizer.h>

#if __has_feature(hwaddress_sanitizer)
// Expose the hwasan interface.
#include <sanitizer/hwasan_interface.h>
#endif  // __has_feature(hwaddress_sanitizer)

#endif  // ZIRCON_THIRD_PARTY_ULIB_MUSL_SRC_INTERNAL_HWASAN_IMPL_H_
