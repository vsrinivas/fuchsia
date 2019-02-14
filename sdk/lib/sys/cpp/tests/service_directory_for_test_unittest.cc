// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/service_directory_for_test.h>

#include "echo_server.h"

#include <lib/fdio/directory.h>
#include <lib/gtest/real_loop_fixture.h>

#include "gtest/gtest.h"

namespace {

class ServiceDirectoryForTest_Tests : public gtest::RealLoopFixture {
 protected:
  void ConnectToService(sys::ServiceDirectory* svc,
                        fidl::examples::echo::EchoPtr& echo) {
    svc->Connect(echo.NewRequest());
  }

  EchoImpl echo_impl_;
};

TEST_F(ServiceDirectoryForTest_Tests, TestInjectedService) {
  auto svc = sys::testing::ServiceDirectoryForTest::Create();

  ASSERT_EQ(ZX_OK, svc->AddService(echo_impl_.GetHandler(dispatcher())));

  fidl::examples::echo::EchoPtr echo;

  ConnectToService(svc.get(), echo);

  std::string result;
  echo->EchoString("hello",
                   [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);
}

}  // namespace