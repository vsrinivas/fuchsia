// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/inspect.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fuchsia/ledger/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/loop_controller.h"
#include "src/ledger/bin/testing/loop_controller_test_loop.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/inspect_deprecated/deprecated/expose.h"
#include "src/lib/inspect_deprecated/hierarchy.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/lib/inspect_deprecated/reader.h"
#include "src/lib/inspect_deprecated/testing/inspect.h"

namespace ledger {
namespace {

using ::inspect_deprecated::testing::ByteVectorPropertyIs;
using ::inspect_deprecated::testing::ChildrenMatch;
using ::inspect_deprecated::testing::NameMatches;
using ::inspect_deprecated::testing::NodeMatches;
using ::inspect_deprecated::testing::PropertyList;
using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

template <typename P>
testing::AssertionResult OpenChild(P parent, const std::string& child_name,
                                   fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child,
                                   LoopController* loop_controller) {
  bool success;
  std::unique_ptr<CallbackWaiter> waiter = loop_controller->NewWaiter();
  (*parent)->OpenChild(child_name, child->NewRequest(),
                       callback::Capture(waiter->GetCallback(), &success));
  if (!waiter->RunUntilCalled()) {
    return ::testing::AssertionFailure() << "RunUntilCalled not successful!";
  }
  if (!success) {
    return ::testing::AssertionFailure() << "OpenChild not successful!";
  }
  return ::testing::AssertionSuccess();
}

}  // namespace

testing::AssertionResult OpenChild(inspect_deprecated::Node* parent, const std::string& child_name,
                                   fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child,
                                   LoopController* loop_controller) {
  std::shared_ptr<component::Object> parent_object = parent->object_dir().object();
  return OpenChild(&parent_object, child_name, child, loop_controller);
}

testing::AssertionResult OpenChild(inspect_deprecated::Node* parent, const std::string& child_name,
                                   fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child,
                                   async::TestLoop* test_loop) {
  std::shared_ptr<component::Object> parent_object = parent->object_dir().object();
  LoopControllerTestLoop loop_controller(test_loop);
  return OpenChild(&parent_object, child_name, child, &loop_controller);
}

testing::AssertionResult OpenChild(
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* parent,
    const std::string& child_name, fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child,
    async::TestLoop* test_loop) {
  LoopControllerTestLoop loop_controller(test_loop);
  return OpenChild(parent, child_name, child, &loop_controller);
}

testing::AssertionResult Inspect(fuchsia::inspect::deprecated::InspectPtr* top_level,
                                 LoopController* loop_controller,
                                 inspect_deprecated::ObjectHierarchy* hierarchy) {
  fidl::InterfaceHandle<fuchsia::inspect::deprecated::Inspect> handle = top_level->Unbind();
  inspect_deprecated::ObjectReader object_reader =
      inspect_deprecated::ObjectReader(std::move(handle));
  async::Executor executor(loop_controller->dispatcher());
  fit::result<inspect_deprecated::ObjectHierarchy> hierarchy_result;
  std::unique_ptr<CallbackWaiter> waiter = loop_controller->NewWaiter();
  auto hierarchy_promise =
      inspect_deprecated::ReadFromFidl(object_reader)
          .then([&](fit::result<inspect_deprecated::ObjectHierarchy>& then_hierarchy_result) {
            hierarchy_result = std::move(then_hierarchy_result);
            waiter->GetCallback()();
          });
  executor.schedule_task(std::move(hierarchy_promise));
  if (!waiter->RunUntilCalled()) {
    return ::testing::AssertionFailure() << "RunUntilCalled not successful!";
  }
  if (!hierarchy_result.is_ok()) {
    return ::testing::AssertionFailure() << "Hierarchy result not okay!";
  }
  *hierarchy = hierarchy_result.take_value();
  top_level->Bind(object_reader.TakeChannel());
  return ::testing::AssertionSuccess();
}

testing::AssertionResult Inspect(inspect_deprecated::Node* top_level_node,
                                 async::TestLoop* test_loop,
                                 inspect_deprecated::ObjectHierarchy* hierarchy) {
  LoopControllerTestLoop loop_controller(test_loop);
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> inspect_ptr;
  testing::AssertionResult open_child_result =
      OpenChild(top_level_node, kSystemUnderTestAttachmentPointPathComponent.ToString(),
                &inspect_ptr, &loop_controller);
  if (!open_child_result) {
    return open_child_result;
  }
  return Inspect(&inspect_ptr, &loop_controller, hierarchy);
}

::testing::Matcher<const inspect_deprecated::ObjectHierarchy&> CommitMatches(
    const std::optional<const storage::CommitId>& commit_id,
    const std::set<storage::CommitId>& parents,
    const std::map<std::string, std::set<std::vector<uint8_t>>>& entries) {
  std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>> parent_matchers;
  parent_matchers.reserve(parents.size());
  for (const storage::CommitId& parent : parents) {
    parent_matchers.push_back(NodeMatches(NameMatches(CommitIdToDisplayName(parent))));
  }
  std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>> entry_matchers;
  for (const auto& [key, values] : entries) {
    std::vector<testing::Matcher<const inspect_deprecated::hierarchy::Property&>> value_matchers;
    for (const auto& value : values) {
      value_matchers.emplace_back(
          ByteVectorPropertyIs(kValueInspectPathComponent.ToString(), value));
    }
    entry_matchers.emplace_back(NodeMatches(AllOf(
        NameMatches(KeyToDisplayName(key)), PropertyList(Contains(AnyOfArray(value_matchers))))));
  }
  const testing::Matcher<const inspect_deprecated::ObjectHierarchy&> children_matcher =
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches(kParentsInspectPathComponent.ToString())),
                ChildrenMatch(UnorderedElementsAreArray(parent_matchers))),
          AllOf(NodeMatches(NameMatches(kEntriesInspectPathComponent.ToString())),
                ChildrenMatch(UnorderedElementsAreArray(entry_matchers)))));
  if (commit_id) {
    return AllOf(NodeMatches(NameMatches(CommitIdToDisplayName(commit_id.value()))),
                 children_matcher);
  } else {
    return children_matcher;
  }
}

::testing::Matcher<const inspect_deprecated::ObjectHierarchy&> PageMatches(
    const convert::ExtendedStringView& page_id,
    const std::set<const std::optional<const storage::CommitId>>& heads,
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        commit_matchers) {
  std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>> head_matchers;
  head_matchers.reserve(heads.size());
  for (const std::optional<const storage::CommitId>& head : heads) {
    if (head) {
      head_matchers.push_back(NodeMatches(NameMatches(CommitIdToDisplayName(head.value()))));
    } else {
      head_matchers.push_back(_);
    }
  }
  return AllOf(NodeMatches(NameMatches(PageIdToDisplayName(page_id.ToString()))),
               ChildrenMatch(UnorderedElementsAre(
                   AllOf(NodeMatches(NameMatches(kCommitsInspectPathComponent.ToString())),
                         ChildrenMatch(UnorderedElementsAreArray(commit_matchers))),
                   AllOf(NodeMatches(NameMatches(kHeadsInspectPathComponent.ToString())),
                         ChildrenMatch(UnorderedElementsAreArray(head_matchers))))));
}

::testing::Matcher<const inspect_deprecated::ObjectHierarchy&> LedgerMatches(
    const convert::ExtendedStringView& ledger_name,
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        page_matchers) {
  return AllOf(NodeMatches(NameMatches(ledger_name.ToString())),
               ChildrenMatch(UnorderedElementsAre(
                   AllOf(NodeMatches(NameMatches(kPagesInspectPathComponent.ToString())),
                         ChildrenMatch(UnorderedElementsAreArray(page_matchers))))));
}

testing::Matcher<const inspect_deprecated::ObjectHierarchy&> RepositoryMatches(
    std::optional<convert::ExtendedStringView> repository_name,
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        ledger_matchers) {
  auto children_match = ChildrenMatch(
      UnorderedElementsAre(AllOf(NodeMatches(NameMatches(kLedgersInspectPathComponent.ToString())),
                                 ChildrenMatch(UnorderedElementsAreArray(ledger_matchers)))));
  if (repository_name) {
    return AllOf(NodeMatches(NameMatches(repository_name.value().ToString())), children_match);
  }
  return children_match;
}

testing::Matcher<const inspect_deprecated::ObjectHierarchy&> RepositoriesAggregateMatches(
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        repository_matchers) {
  return AllOf(NodeMatches(NameMatches(kRepositoriesInspectPathComponent.ToString())),
               ChildrenMatch(UnorderedElementsAreArray(repository_matchers)));
}

}  // namespace ledger
