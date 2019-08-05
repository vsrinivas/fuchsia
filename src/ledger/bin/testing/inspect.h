// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_INSPECT_H_
#define SRC_LEDGER_BIN_TESTING_INSPECT_H_

#include <lib/async-testing/test_loop.h>
#include <lib/inspect_deprecated/deprecated/expose.h>
#include <lib/inspect_deprecated/hierarchy.h>
#include <lib/inspect_deprecated/inspect.h>

#include <optional>
#include <vector>

#include "fuchsia/ledger/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/testing/loop_controller.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

inline constexpr fxl::StringView kSystemUnderTestAttachmentPointPathComponent = "attachment_point";

// Given an |inspect_deprecated::Node| under which another |inspect_deprecated::Node| is available
// at |child_name|, binds |child| to the child node.
testing::AssertionResult OpenChild(inspect_deprecated::Node* parent, const std::string& child_name,
                                   fidl::InterfacePtr<fuchsia::inspect::Inspect>* child,
                                   LoopController* loop_controller);

// Given an |inspect_deprecated::Node| under which another |inspect_deprecated::Node| is available
// at |kSystemUnderTestAttachmentPointPathComponent|, reads the exposed Inspect data of the system
// under test and assigned to |hierarchy| the |inspect_deprecated::ObjectHierarchy| of the read
// data.
testing::AssertionResult Inspect(inspect_deprecated::Node* top_level_node,
                                 LoopController* loop_controller,
                                 inspect_deprecated::ObjectHierarchy* hierarchy);

// Given an |inspect_deprecated::Node| under which another |inspect_deprecated::Node| is available
// at |kSystemUnderTestAttachmentPointPathComponent|, reads the exposed Inspect data of the system
// under test and assigned to |hierarchy| the |inspect_deprecated::ObjectHierarchy| of the read
// data.
testing::AssertionResult Inspect(inspect_deprecated::Node* top_level_node,
                                 async::TestLoop* test_loop,
                                 inspect_deprecated::ObjectHierarchy* hierarchy);

// Matches an |inspect_deprecated::ObjectHierarchy| node named according to |page_id|.
testing::Matcher<const inspect_deprecated::ObjectHierarchy&> PageMatches(
    fuchsia::ledger::PageId page_id);

// Matches an |inspect_deprecated::ObjectHierarchy| node named according to |ledger_name| under
// which is a node named according to |kPagesInspectPathComponent| under which are nodes that match
// |page_matchers|.
testing::Matcher<const inspect_deprecated::ObjectHierarchy&> LedgerMatches(
    const convert::ExtendedStringView& ledger_name,
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>& page_matchers);

// Matches an |inspect_deprecated::ObjectHierarchy| node named according to |repository_name| under
// which is a node named according to |kLedgersInspectPathComponent| under which are nodes that
// match |ledger_matchers|. If |repository_name| is empty, the returned matcher will match a node
// describing a repository with any name, supporting "the test knows that the system under test
// created a repository but does not yet know the name of the created repository" use cases.
testing::Matcher<const inspect_deprecated::ObjectHierarchy&> RepositoryMatches(
    std::optional<convert::ExtendedStringView> repository_name,
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        ledger_matchers);

// Matches an |inspect_deprecated::ObjectHierarchy| node named |kRepositoriesInspectPathComponent|
// that has children matching |repository_matchers|. The verb tense in the name is deliberate and
// indicates that this matches *a single node*.
testing::Matcher<const inspect_deprecated::ObjectHierarchy&> RepositoriesAggregateMatches(
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        repository_matchers);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_INSPECT_H_
