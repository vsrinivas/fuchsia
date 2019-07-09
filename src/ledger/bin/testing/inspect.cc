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

#include <functional>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace ledger {

testing::AssertionResult Inspect(inspect_deprecated::Node* top_level_node,
                                 async::TestLoop* test_loop,
                                 inspect_deprecated::ObjectHierarchy* hierarchy) {
  bool callback_called;
  bool success;
  fidl::InterfaceHandle<fuchsia::inspect::Inspect> inspect_handle;
  top_level_node->object_dir().object()->OpenChild(
      kSystemUnderTestAttachmentPointPathComponent.ToString(), inspect_handle.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &success));
  test_loop->RunUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure() << "Callback passed to OpenChild not called!";
  }
  if (!success) {
    return ::testing::AssertionFailure() << "OpenChild not successful!";
  }

  async::Executor executor(test_loop->dispatcher());
  fit::result<inspect_deprecated::ObjectHierarchy> hierarchy_result;
  auto hierarchy_promise =
      inspect_deprecated::ReadFromFidl(inspect_deprecated::ObjectReader(std::move(inspect_handle)))
          .then([&](fit::result<inspect_deprecated::ObjectHierarchy>& then_hierarchy_result) {
            hierarchy_result = std::move(then_hierarchy_result);
          });
  executor.schedule_task(std::move(hierarchy_promise));
  test_loop->RunUntilIdle();
  if (!hierarchy_result.is_ok()) {
    return ::testing::AssertionFailure() << "Hierarchy result not okay!";
  }
  *hierarchy = hierarchy_result.take_value();
  return ::testing::AssertionSuccess();
}

}  // namespace ledger
