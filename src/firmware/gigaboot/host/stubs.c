// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stub implementations for files we're not yet ready to bring into the
// host-side test build.

#include <stdbool.h>
#include <stddef.h>

size_t image_getsize(void* image, size_t sz) { return 0; }

bool image_is_valid(void* image, size_t sz) { return false; }
