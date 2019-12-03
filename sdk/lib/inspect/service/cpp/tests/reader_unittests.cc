// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include "gmock/gmock.h"

using inspect::Hierarchy;
using inspect::Inspector;
using ::testing::AllOf;
using ::testing::UnorderedElementsAre;
using namespace inspect::testing;

namespace {

class InspectReaderTest : public gtest::RealLoopFixture {
 public:
  InspectReaderTest()
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

TEST_F(InspectReaderTest, ReadHierarchy) {
  inspect::ValueList values;
  root().CreateInt("val", 1, &values);
  root().CreateLazyNode(
      "test",
      [] {
        Inspector insp;
        insp.GetRoot().CreateInt("val2", 2, &insp);
        insp.GetRoot().CreateLazyValues(
            "tempvals",
            [] {
              Inspector insp;
              insp.GetRoot().CreateInt("val3", 3, &insp);
              return fit::make_ok_promise(std::move(insp));
            },
            &insp);
        return fit::make_ok_promise(std::move(insp));
      },
      &values);
  root().CreateLazyNode(
      "next",
      [] {
        Inspector insp;
        insp.GetRoot().CreateInt("val4", 4, &insp);
        return fit::make_ok_promise(std::move(insp));
      },
      &values);
  root().CreateLazyNode(
      "node_error", [] { return fit::make_result_promise<Inspector>(fit::error()); }, &values);
  root().CreateLazyNode(
      "values_error", [] { return fit::make_result_promise<Inspector>(fit::error()); }, &values);

  fit::result<Hierarchy> hierarchy;
  bool done = false;

  executor_.schedule_task(
      inspect::ReadFromTree(Connect()).then([&](fit::result<Hierarchy>& result) {
        hierarchy = std::move(result);
        done = true;
      }));

  RunLoopUntil([&] { return done; });

  ASSERT_TRUE(hierarchy.is_ok());

  EXPECT_THAT(
      hierarchy.value(),
      AllOf(NodeMatches(NameMatches("root")),
            ChildrenMatch(UnorderedElementsAre(
                AllOf(NodeMatches(
                    AllOf(NameMatches("test"),
                          PropertyList(UnorderedElementsAre(IntIs("val2", 2), IntIs("val3", 3)))))),
                AllOf(NodeMatches(AllOf(NameMatches("next"),
                                        PropertyList(UnorderedElementsAre(IntIs("val4", 4))))))))));
}

}  // namespace
