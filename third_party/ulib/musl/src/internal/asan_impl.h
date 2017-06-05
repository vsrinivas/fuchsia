// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

#if __has_feature(address_sanitizer)

#include <sanitizer/asan_interface.h>

void __asan_early_init(void) __attribute__((visibility("hidden")));

#endif // __has_feature(address_sanitizer)
