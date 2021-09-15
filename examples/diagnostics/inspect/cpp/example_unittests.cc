// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/real_loop_fixture.h>

#include "echo_connection.h"

// [START test_imports]
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include <gtest/gtest.h>

using namespace inspect::testing;
// [END test_imports]

namespace example {

class EchoConnectionTest : public gtest::RealLoopFixture {
 public:
  EchoConnectionTest()
      : inspector_(),
        stats_{std::make_shared<EchoConnectionStats>(EchoConnectionStats{
            inspector_.GetRoot().CreateUint("bytes_processed", 0),
            inspector_.GetRoot().CreateUint("total_requests", 0),
        })},
        connection_(stats_),
        echo_(),
        binding_(&connection_, echo_.NewRequest().TakeChannel()) {}

 protected:
  inspect::Inspector inspector_;
  std::shared_ptr<EchoConnectionStats> stats_;
  EchoConnection connection_;
  fidl::examples::routing::echo::EchoPtr echo_;
  fidl::Binding<EchoConnection> binding_;
};

TEST_F(EchoConnectionTest, EchoServerWritesStats) {
  // Invoke the echo server
  ::fidl::StringPtr message;
  echo_->EchoString("Hello World!", [&](::fidl::StringPtr retval) { message = retval; });
  echo_->EchoString("Hello World!", [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();

  // [START inspect_test]
  // Validate the contents of the tree match
  auto hierarchy_result = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  ASSERT_TRUE(hierarchy_result.is_ok());
  EXPECT_THAT(hierarchy_result.take_value(),
              NodeMatches(AllOf(PropertyList(::testing::UnorderedElementsAre(
                  UintIs("bytes_processed", 24), UintIs("total_requests", 2))))));
  // [END inspect_test]
}

}  // namespace example
