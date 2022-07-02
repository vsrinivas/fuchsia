// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_TESTING_MAYBE_STANDALONE_TEST_INCLUDE_LIB_MAYBE_STANDALONE_TEST_MAYBE_STANDALONE_H_
#define SRC_ZIRCON_TESTING_MAYBE_STANDALONE_TEST_INCLUDE_LIB_MAYBE_STANDALONE_TEST_MAYBE_STANDALONE_H_

#include <lib/zx/resource.h>

#include <optional>

// Forward declaration for <lib/boot-options/boot-options.h>.
struct BootOptions;

namespace maybe_standalone {

// This returns the invalid handle if not built standalone.
zx::unowned_resource GetRootResource();

// This returns nullptr if not built standalone.
const BootOptions* GetBootOptions();

}  // namespace maybe_standalone

#endif  // SRC_ZIRCON_TESTING_MAYBE_STANDALONE_TEST_INCLUDE_LIB_MAYBE_STANDALONE_TEST_MAYBE_STANDALONE_H_
