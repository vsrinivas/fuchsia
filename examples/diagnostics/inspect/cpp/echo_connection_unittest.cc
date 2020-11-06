// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_connection.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

namespace example {
namespace testing {

using namespace fuchsia::examples;
using namespace inspect::testing;

class EchoConnectionTest : public gtest::RealLoopFixture {
 public:
  EchoConnectionTest()
      : inspector_(),
        connection_(inspector_.GetRoot().CreateChild("connection"), stats_),
        echo_(),
        binding_(&connection_, echo_.NewRequest().TakeChannel()) {}

 protected:
  inspect::Inspector inspector_;
  std::shared_ptr<EchoConnectionStats> stats_;
  EchoConnection connection_;
  EchoPtr echo_;
  fidl::Binding<EchoConnection> binding_;
};

TEST_F(EchoConnectionTest, EchoString_MultipleRequests) {
  std::string message = "bogus";
  echo_->EchoString("Hello World!", [&](std::string retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("Hello World!", message);
  auto hierarchy_result = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  ASSERT_TRUE(hierarchy_result.is_ok());
  EXPECT_THAT(hierarchy_result.take_value(),
              ChildrenMatch(::testing::ElementsAre(
                  NodeMatches(AllOf(NameMatches("connection"),
                                    PropertyList(::testing::UnorderedElementsAre(
                                        UintIs("bytes_processed", 12), UintIs("requests", 1))))))));

  // Call the service again.
  echo_->EchoString("Hello Again!", [&](std::string retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("Hello Again!", message);
  hierarchy_result = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  ASSERT_TRUE(hierarchy_result.is_ok());
  EXPECT_THAT(hierarchy_result.take_value(),
              ChildrenMatch(::testing::ElementsAre(
                  NodeMatches(AllOf(NameMatches("connection"),
                                    PropertyList(::testing::UnorderedElementsAre(
                                        UintIs("bytes_processed", 24), UintIs("requests", 2))))))));
}

// Answer "" with ""
TEST_F(EchoConnectionTest, EchoString_Empty) {
  std::string message = "bogus";
  echo_->EchoString("", [&](::std::string retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("", message);
  auto hierarchy_result = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  ASSERT_TRUE(hierarchy_result.is_ok());
  EXPECT_THAT(
      hierarchy_result.take_value(),
      ChildrenMatch(::testing::ElementsAre(NodeMatches(AllOf(
          NameMatches("connection"), PropertyList(::testing::UnorderedElementsAre(
                                         UintIs("bytes_processed", 0), UintIs("requests", 1))))))));
}

}  // namespace testing
}  // namespace example
