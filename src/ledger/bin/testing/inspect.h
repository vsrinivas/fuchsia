// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_INSPECT_H_
#define SRC_LEDGER_BIN_TESTING_INSPECT_H_

#include <lib/async-testing/test_loop.h>
#include <lib/inspect/deprecated/expose.h>
#include <lib/inspect/hierarchy.h>
#include <lib/inspect/inspect.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

inline constexpr fxl::StringView kSystemUnderTestAttachmentPointPathComponent =
    "attachment_point";

// Given an |inspect::Node| under which another |inspect::Node| is available at
// |kSystemUnderTestAttachmentPointPathComponent|, reads the exposed Inspect
// data of the system under test and assigned to |hierarchy| the
// |inspect::ObjectHierarchy| of the read data.
testing::AssertionResult Inspect(inspect::Node* top_level_node,
                                 async::TestLoop* test_loop,
                                 inspect::ObjectHierarchy* hierarchy);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_INSPECT_H_
