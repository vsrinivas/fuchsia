// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <src/lib/fxl/strings/substitute.h>

#include "lib/sys/component/cpp/testing/realm_builder_types.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace {

using namespace component_testing;
using ::testing::_;
using ::testing::Contains;
using ::testing::UnorderedElementsAre;
using namespace inspect::testing;

class SysInspectTest : public gtest::RealLoopFixture {
 protected:
  SysInspectTest() : executor_(dispatcher()) {}

  void SetUp() override {
    auto builder = RealmBuilder::Create();

    builder.AddChild("test_component", "#meta/sys_inspect_cpp.cm",
                     ChildOptions{.startup_mode = StartupMode::EAGER});

    builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.logger.LogSink"}},
                           .source = ParentRef(),
                           .targets = {ChildRef{"test_component"}}});

    builder.AddRoute(Route{.capabilities = {Directory{"parent-diagnostics"}},
                           .source = ChildRef{"test_component"},
                           .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(builder.Build(dispatcher()));
  }

  void TearDown() override { realm_.reset(); }

  async::Executor& executor() { return executor_; }

  zx_status_t GetInspectTree(fuchsia::inspect::TreePtr* ptr) {
    return realm_->Connect("parent-diagnostics/fuchsia.inspect.Tree",
                           ptr->NewRequest(dispatcher()).TakeChannel());
  }

 private:
  std::unique_ptr<RealmRoot> realm_;
  async::Executor executor_;
  std::unique_ptr<sys::ComponentContext> component_context_;
};

TEST_F(SysInspectTest, ReadHierarchy) {
  bool done = false;
  fpromise::result<inspect::Hierarchy> result;
  fuchsia::inspect::TreePtr ptr;
  ASSERT_EQ(ZX_OK, GetInspectTree(&ptr));
  executor().schedule_task(
      inspect::ReadFromTree(std::move(ptr)).then([&](fpromise::result<inspect::Hierarchy>& res) {
        result = std::move(res);
        done = true;
      }));

  RunLoopUntil([&] { return done; });

  EXPECT_THAT(
      result.value(),
      AllOf(NodeMatches(AllOf(NameMatches("root"), PropertyList(UnorderedElementsAre(
                                                       IntIs("val1", 1), IntIs("val2", 2),
                                                       IntIs("val3", 3), IntIs("val4", 4))))),
            ChildrenMatch(Contains(NodeMatches(AllOf(
                NameMatches("child"), PropertyList(UnorderedElementsAre(IntIs("val", 0)))))))));
}

TEST_F(SysInspectTest, ReadHealth) {
  bool done = false;
  fpromise::result<inspect::Hierarchy> result;
  fuchsia::inspect::TreePtr ptr;
  ASSERT_EQ(ZX_OK, GetInspectTree(&ptr));
  executor().schedule_task(
      inspect::ReadFromTree(std::move(ptr)).then([&](fpromise::result<inspect::Hierarchy>& res) {
        result = std::move(res);
        done = true;
      }));

  RunLoopUntil([&] { return done; });

  EXPECT_THAT(result.value(),
              ChildrenMatch(Contains(NodeMatches(
                  AllOf(NameMatches("fuchsia.inspect.Health"),
                        PropertyList(UnorderedElementsAre(StringIs("status", "OK"),
                                                          IntIs("start_timestamp_nanos", _))))))));
}

}  // namespace
