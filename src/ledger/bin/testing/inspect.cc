// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/inspect.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async_promise/executor.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/deprecated/expose.h>
#include <lib/inspect_deprecated/hierarchy.h>
#include <lib/inspect_deprecated/inspect.h>
#include <lib/inspect_deprecated/reader.h>
#include <lib/inspect_deprecated/testing/inspect.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fuchsia/ledger/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/loop_controller.h"
#include "src/ledger/bin/testing/loop_controller_test_loop.h"

namespace ledger {
namespace {

using ::inspect_deprecated::testing::ChildrenMatch;
using ::inspect_deprecated::testing::NameMatches;
using ::inspect_deprecated::testing::NodeMatches;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;

}  // namespace

testing::AssertionResult OpenChild(inspect_deprecated::Node* parent, const std::string& child_name,
                                   fidl::InterfacePtr<fuchsia::inspect::Inspect>* child,
                                   LoopController* loop_controller) {
  bool success;
  std::unique_ptr<CallbackWaiter> waiter = loop_controller->NewWaiter();
  parent->object_dir().object()->OpenChild(child_name, child->NewRequest(),
                                           callback::Capture(waiter->GetCallback(), &success));
  if (!waiter->RunUntilCalled()) {
    return ::testing::AssertionFailure() << "RunUntilCalled not successful!";
  }
  if (!success) {
    return ::testing::AssertionFailure() << "OpenChild not successful!";
  }
  return ::testing::AssertionSuccess();
}

testing::AssertionResult Inspect(inspect_deprecated::Node* top_level_node,
                                 LoopController* loop_controller,
                                 inspect_deprecated::ObjectHierarchy* hierarchy) {
  fidl::InterfacePtr<fuchsia::inspect::Inspect> inspect_ptr;
  testing::AssertionResult open_child_result =
      OpenChild(top_level_node, kSystemUnderTestAttachmentPointPathComponent.ToString(),
                &inspect_ptr, loop_controller);
  if (!open_child_result) {
    return open_child_result;
  }
  async::Executor executor(loop_controller->dispatcher());
  fit::result<inspect_deprecated::ObjectHierarchy> hierarchy_result;
  std::unique_ptr<CallbackWaiter> waiter = loop_controller->NewWaiter();
  auto hierarchy_promise =
      inspect_deprecated::ReadFromFidl(inspect_deprecated::ObjectReader(inspect_ptr.Unbind()))
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
  return ::testing::AssertionSuccess();
}

testing::AssertionResult Inspect(inspect_deprecated::Node* top_level_node,
                                 async::TestLoop* test_loop,
                                 inspect_deprecated::ObjectHierarchy* hierarchy) {
  LoopControllerTestLoop loop_controller(test_loop);
  return Inspect(top_level_node, &loop_controller, hierarchy);
}

::testing::Matcher<const inspect_deprecated::ObjectHierarchy&> PageMatches(
    fuchsia::ledger::PageId page_id) {
  return NodeMatches(
      NameMatches(PageIdToDisplayName(convert::ExtendedStringView(page_id.id).ToString())));
}

::testing::Matcher<const inspect_deprecated::ObjectHierarchy&> LedgerMatches(
    const convert::ExtendedStringView& ledger_name,
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        page_matchers) {
  return AllOf(NodeMatches(NameMatches(ledger_name.ToString())),
               ChildrenMatch(ElementsAre(
                   AllOf(NodeMatches(NameMatches(kPagesInspectPathComponent.ToString())),
                         ChildrenMatch(ElementsAreArray(page_matchers))))));
}

testing::Matcher<const inspect_deprecated::ObjectHierarchy&> RepositoryMatches(
    std::optional<convert::ExtendedStringView> repository_name,
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        ledger_matchers) {
  auto children_match = ChildrenMatch(
      ElementsAre(AllOf(NodeMatches(NameMatches(kLedgersInspectPathComponent.ToString())),
                        ChildrenMatch(ElementsAreArray(ledger_matchers)))));
  if (repository_name) {
    return AllOf(NodeMatches(NameMatches(repository_name.value().ToString())), children_match);
  }
  return children_match;
}

testing::Matcher<const inspect_deprecated::ObjectHierarchy&> RepositoriesAggregateMatches(
    const std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>&
        repository_matchers) {
  return AllOf(NodeMatches(NameMatches(kRepositoriesInspectPathComponent.ToString())),
               ChildrenMatch(ElementsAreArray(repository_matchers)));
}

}  // namespace ledger
