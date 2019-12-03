// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include "gmock/gmock.h"

using inspect::Inspector;

namespace {

class InspectServiceTest : public gtest::RealLoopFixture {
 public:
  InspectServiceTest()
      : executor_(dispatcher()),
        inspector_(),
        handler_(inspect::MakeTreeHandler(&inspector_, dispatcher())) {}

 protected:
  inspect::Node& root() { return inspector_.GetRoot(); }

  fuchsia::inspect::TreePtr Connect() {
    fuchsia::inspect::TreePtr ret;
    handler_(ret.NewRequest());
    return ret;
  }

  async::Executor executor_;

 private:
  Inspector inspector_;
  fidl::InterfaceRequestHandler<fuchsia::inspect::Tree> handler_;
};

TEST_F(InspectServiceTest, SingleTree) {
  inspect::ValueList values;
  root().CreateInt("val", 1, &values);

  auto ptr = Connect();
  ptr.set_error_handler(
      [](zx_status_t status) { ASSERT_TRUE(false) << "Error detected on connection"; });

  std::vector<std::string> names;
  bool done = false;
  fuchsia::inspect::TreeNameIteratorPtr name_iter;
  ptr->ListChildNames(name_iter.NewRequest());

  executor_.schedule_task(inspect::ReadAllChildNames(std::move(name_iter))
                              .and_then([&](std::vector<std::string>& promised_names) {
                                names = std::move(promised_names);
                                done = true;
                              }));

  RunLoopUntil([&] { return done; });

  EXPECT_TRUE(names.empty());
}

TEST_F(InspectServiceTest, ListChildNames) {
  inspect::ValueList values;
  root().CreateLazyNode(
      "a", []() { return fit::make_result_promise<Inspector>(fit::error()); }, &values);
  root().CreateLazyNode(
      "b", []() { return fit::make_result_promise<Inspector>(fit::error()); }, &values);

  auto ptr = Connect();
  ptr.set_error_handler(
      [](zx_status_t status) { ASSERT_TRUE(false) << "Error detected on connection"; });

  std::vector<std::string> names;
  bool done = false;
  fuchsia::inspect::TreeNameIteratorPtr name_iter;
  ptr->ListChildNames(name_iter.NewRequest());

  executor_.schedule_task(inspect::ReadAllChildNames(std::move(name_iter))
                              .and_then([&](std::vector<std::string>& promised_names) {
                                names = std::move(promised_names);
                                done = true;
                              }));

  RunLoopUntil([&] { return done; });

  EXPECT_EQ(names, std::vector<std::string>({"a-0", "b-1"}));
}

TEST_F(InspectServiceTest, OpenChild) {
  inspect::ValueList values;
  root().CreateInt("val", 20, &values);
  root().CreateLazyNode(
      "valid",
      []() {
        Inspector insp;
        insp.GetRoot().CreateInt("val", 10, &insp);
        return fit::make_ok_promise(insp);
      },
      &values);

  root().CreateLazyNode(
      "invalid", [] { return fit::make_result_promise<Inspector>(fit::error()); }, &values);

  auto ptr = Connect();
  ptr.set_error_handler(
      [](zx_status_t status) { ASSERT_TRUE(false) << "Error detected on connection"; });

  std::vector<std::string> names;
  bool list_done = false;
  fuchsia::inspect::TreeNameIteratorPtr name_iter;
  fit::result<fuchsia::inspect::TreeContent> root, child;
  ptr->ListChildNames(name_iter.NewRequest());

  executor_.schedule_task(inspect::ReadAllChildNames(std::move(name_iter))
                              .and_then([&](std::vector<std::string>& promised_names) {
                                names = std::move(promised_names);
                                list_done = true;
                              }));

  ptr->GetContent(
      [&](fuchsia::inspect::TreeContent content) { root = fit::ok(std::move(content)); });
  fuchsia::inspect::TreePtr child_ptr;
  ptr->OpenChild("valid-0", child_ptr.NewRequest());
  child_ptr->GetContent(
      [&](fuchsia::inspect::TreeContent content) { child = fit::ok(std::move(content)); });

  bool read_error_done = false;
  bool missing_error_done = false;
  fuchsia::inspect::TreePtr read_error_ptr, missing_error_ptr;
  read_error_ptr.set_error_handler([&](zx_status_t status) { read_error_done = true; });
  missing_error_ptr.set_error_handler([&](zx_status_t status) { missing_error_done = true; });
  ptr->OpenChild("invalid-1", read_error_ptr.NewRequest());
  ptr->OpenChild("missing", missing_error_ptr.NewRequest());
  read_error_ptr->GetContent([](fuchsia::inspect::TreeContent unused) {});
  missing_error_ptr->GetContent([](fuchsia::inspect::TreeContent unused) {});

  RunLoopUntil([&] {
    return list_done && root.is_ok() && child.is_ok() && read_error_done && missing_error_done;
  });

  EXPECT_EQ(names, std::vector<std::string>({"invalid-1", "valid-0"}));
  auto root_hierarchy = inspect::ReadFromVmo(root.take_value().buffer().vmo).take_value();
  ASSERT_EQ(1u, root_hierarchy.node().properties().size());
  EXPECT_EQ("val", root_hierarchy.node().properties()[0].name());
  auto child_hierarchy = inspect::ReadFromVmo(child.take_value().buffer().vmo).take_value();
  ASSERT_EQ(1u, child_hierarchy.node().properties().size());
  EXPECT_EQ("val", child_hierarchy.node().properties()[0].name());
}

}  // namespace
