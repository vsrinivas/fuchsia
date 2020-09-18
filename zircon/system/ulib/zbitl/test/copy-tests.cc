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
#define FUCHSIA_ONLY(...) __VA_ARGS__
#else
#define FUCHSIA_ONLY(...)
#endif

// clang-format off
#define FOR_ALL_SRC_TYPES(macro, ...)                           \
  macro(__VA_ARGS__, StringTestTraits, String)                  \
  macro(__VA_ARGS__, ByteViewTestTraits, ByteView)              \
  macro(__VA_ARGS__, FblByteArrayTestTraits, ByteArray)         \
  macro(__VA_ARGS__, FdTestTraits, Fd)                          \
  macro(__VA_ARGS__, StdioTestTraits, Stdio)                    \
  FUCHSIA_ONLY(                                                 \
    macro(__VA_ARGS__, VmoTestTraits, Vmo)                      \
    macro(__VA_ARGS__, UnownedVmoTestTraits, UnownedVmo)        \
    macro(__VA_ARGS__, MapOwnedVmoTestTraits, MapOwnedVmo)      \
    macro(__VA_ARGS__, MapUnownedVmoTestTraits, MapUnownedVmo))

// Recall that only writable storage types may be copy destinations.
#define FOR_ALL_DEST_TYPES(macro, ...)                          \
  macro(__VA_ARGS__, FblByteArrayTestTraits, ByteArray)         \
  macro(__VA_ARGS__, FdTestTraits, Fd)                          \
  macro(__VA_ARGS__, StdioTestTraits, Stdio)                    \
  FUCHSIA_ONLY(                                                 \
    macro(__VA_ARGS__, VmoTestTraits, Vmo)                      \
    macro(__VA_ARGS__, UnownedVmoTestTraits, UnownedVmo)        \
    macro(__VA_ARGS__, MapOwnedVmoTestTraits, MapOwnedVmo)      \
    macro(__VA_ARGS__, MapUnownedVmoTestTraits, MapUnownedVmo))
// clang-format on

namespace {

FOR_ALL_SRC_TYPES(FOR_ALL_DEST_TYPES, TEST_COPYING, ZbitlViewCopyTests)

}  // namespace
