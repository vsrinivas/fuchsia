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
#include <lib/inspect/deprecated/expose.h>
#include <lib/inspect/hierarchy.h>
#include <lib/inspect/inspect.h>
#include <lib/inspect/reader.h>

#include <functional>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace ledger {

testing::AssertionResult Inspect(inspect::Node* top_level_node, async::TestLoop* test_loop,
                                 inspect::ObjectHierarchy* hierarchy) {
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
  fit::result<inspect::ObjectHierarchy> hierarchy_result;
  auto hierarchy_promise =
      inspect::ReadFromFidl(inspect::ObjectReader(std::move(inspect_handle)))
          .then([&](fit::result<inspect::ObjectHierarchy>& then_hierarchy_result) {
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
