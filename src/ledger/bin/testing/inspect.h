// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_INSPECT_H_
#define SRC_LEDGER_BIN_TESTING_INSPECT_H_

#include <lib/async-testing/test_loop.h>
#include <lib/inspect_deprecated/deprecated/expose.h>
#include <lib/inspect_deprecated/hierarchy.h>
#include <lib/inspect_deprecated/inspect.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

inline constexpr fxl::StringView kSystemUnderTestAttachmentPointPathComponent = "attachment_point";

// Given an |inspect_deprecated::Node| under which another |inspect_deprecated::Node| is available
// at |kSystemUnderTestAttachmentPointPathComponent|, reads the exposed Inspect data of the system
// under test and assigned to |hierarchy| the |inspect_deprecated::ObjectHierarchy| of the read
// data.
testing::AssertionResult Inspect(inspect_deprecated::Node* top_level_node,
                                 async::TestLoop* test_loop,
                                 inspect_deprecated::ObjectHierarchy* hierarchy);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_INSPECT_H_
