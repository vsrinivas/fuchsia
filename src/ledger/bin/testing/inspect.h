// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_INSPECT_H_
#define SRC_LEDGER_BIN_TESTING_INSPECT_H_

#include <lib/async-testing/test_loop.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "fuchsia/ledger/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/loop_controller.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/inspect_deprecated/deprecated/expose.h"
#include "src/lib/inspect_deprecated/hierarchy.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

inline constexpr absl::string_view kSystemUnderTestAttachmentPointPathComponent =
    "attachment_point";

// Given an |inspect_deprecated::Node| under which another |inspect_deprecated::Node| is available
// at |child_name|, binds |child| to the child node.
testing::AssertionResult OpenChild(inspect_deprecated::Node* parent, const std::string& child_name,
                                   fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child,
                                   LoopController* loop_controller);

// Given an |inspect_deprecated::Node| under which another |inspect_deprecated::Node| is available
// at |child_name|, binds |child| to the child node.
testing::AssertionResult OpenChild(inspect_deprecated::Node* parent, const std::string& child_name,
                                   fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child,
                                   async::TestLoop* test_loop);

// Given a |fidl::InterfaceHandle<fuchsia::inspect::deprecated::Inspect>| to an
// |inspect_deprecated::Node| under which another |inspect_deprecated::Node| is available at
// |child_name|, binds |child| to the child node.
testing::AssertionResult OpenChild(
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* parent,
    const std::string& child_name, fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child,
    async::TestLoop* test_loop);

// Given a |fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>*| under which the system under
// test's Inspect hierarchy is available, reads the exposed Inspect data of the system under test
// and assigned to |hierarchy| the |inspect_deprecated::ObjectHierarchy| of the read data.
testing::AssertionResult Inspect(
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* top_level,
    LoopController* loop_controller, inspect_deprecated::ObjectHierarchy* hierarchy);

// Given an |inspect_deprecated::Node| under which another |inspect_deprecated::Node| is available
// at |kSystemUnderTestAttachmentPointPathComponent|, reads the exposed Inspect data of the system
// under test and assigned to |hierarchy| the |inspect_deprecated::ObjectHierarchy| of the read
// data.
testing::AssertionResult Inspect(inspect_deprecated::Node* top_level_node,
                                 async::TestLoop* test_loop,
                                 inspect_deprecated::ObjectHierarchy* hierarchy);

testing::Matcher<const inspect_deprecated::ObjectHierarchy&> CommitMatches(
    const std::optional<const storage::CommitId>& commit_id,
    const std::set<storage::CommitId>& parents,
    const std::map<std::string, std::set<std::vector<uint8_t>>>& entries);

// Matches an |inspect_deprecated::ObjectHierarchy| node named according to |page_id| with heads
// |heads| and commits matching |commit_matchers|.
testing::Matcher<const inspect_deprecated::ObjectHierarchy&> PageMatches(
    const convert::ExtendedStringView& page_id,
    const std::set<const std::optional<const storage::CommitId>>& heads,
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        commit_matchers);

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
