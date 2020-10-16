// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fd-tests.h"
#include "memory-tests.h"
#include "stdio-tests.h"
#include "tests.h"
#include "view-tests.h"

#ifdef __Fuchsia__
#include "vmo-tests.h"
#endif

#if !defined(SRC_STORAGE_TYPE)
#error preprocessor variable `SRC_STORAGE_TYPE` must be defined
#elif !defined(DEST_STORAGE_TYPE)
#error preprocessor variable `DEST_STORAGE_TYPE` must be defined
#endif

#define PASTE(a, b) PASTE_1(a, b)
#define PASTE_1(a, b) a##b
#define TEST_TRAITS(x) PASTE(x, TestTraits)

namespace {

TEST_COPYING(ZbitlViewCopyTests, TEST_TRAITS(SRC_STORAGE_TYPE), SRC_STORAGE_TYPE,
             TEST_TRAITS(DEST_STORAGE_TYPE), DEST_STORAGE_TYPE)

}  // namespace
