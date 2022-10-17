// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples.calculator/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

// This is an [Integration
// Test](https://fuchsia.dev/fuchsia-src/contribute/testing/scope?hl=en#integration-tests) as this
// test package contains a test component (the code in this file is the test component) and depends
// on the Calculator Server implementation (there is no mocked server, it is the actual server in
// ../server).  Take a look at the BUILD.gn file and the component manifest in
// ./meta/calc_integration_test.cml to get a sense of how the test is structured.

// The simplest form of a test with the synchronous client
TEST(CalcIntegrationTest, TestCalcSync) {
  zx::result client_end = component::Connect<fuchsia_examples_calculator::Calculator>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Calculator| protocol: "
                   << client_end.status_string();
    FAIL();
  }

  // Create a fidl::SyncClient
  fidl::SyncClient client{std::move(*client_end)};

  fidl::Result<fuchsia_examples_calculator::Calculator::Add> result =
      client->Add({{.a = 4.5, .b = 3.2}});

  if (!result.is_ok()) {
    // If the call failed, log the error, and quit the program.
    // Production code should do more graceful error handling depending
    // on the situation.
    FX_LOGS(ERROR) << "Calc Add() failed: " << result.error_value() << std::endl;
    FAIL();
  }
  FX_LOGS(INFO) << "Calculator client got response " << result->sum() << std::endl;

  ASSERT_EQ(result->sum(), 7.7);
}

// The simplest form of a test with the asynchronous client
TEST(CalcIntegrationTest, TestCalcAsync) {
  zx::result client_end = component::Connect<fuchsia_examples_calculator::Calculator>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Calculator| protocol: "
                   << client_end.status_string();
    FAIL();
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create a fidl::Client
  fidl::Client client(std::move(*client_end), dispatcher);

  // Make an Add() call, building the request object inline and the result handler lambda
  client->Add({{.a = 4, .b = 2}})
      .ThenExactlyOnce([&](fidl::Result<fuchsia_examples_calculator::Calculator::Add>& result) {
        if (!result.is_ok()) {
          FX_LOGS(ERROR) << "Failure receiving response to Add" << result.error_value();
          FAIL();
        }
        FX_LOGS(INFO) << "Calculator client got response " << result->sum() << std::endl;
        // Shut the dispatcher loop down so the test can exit cleanly
        loop.Quit();
        ASSERT_EQ(result->sum(), 6);
      });
  loop.Run();
  loop.ResetQuit();
}

// A TestLoopFixture class to abstract out setup/teardown
class CalcTestFixture : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    // Set the logging tags for filtering
    syslog::SetTags({"calculator_tests"});
    // Setup the client with the helper function
    GetClient();
  }

  void TearDown() override { TestLoopFixture::TearDown(); }

 protected:
  // A helper function that manages the client connection and binding to simplify the test
  // structure.  Mirrors the client implementation in ../client, but uses the synchronous interface
  void GetClient(void) {
    zx::result client_end = component::Connect<fuchsia_examples_calculator::Calculator>();
    if (!client_end.is_ok()) {
      FX_LOGS(ERROR) << "Synchronous error when connecting to the |Calculator| protocol: "
                     << client_end.status_string();
      FAIL() << "Failed setting up client endpoint.";
    }
    client.Bind(std::move(*client_end));
    ASSERT_TRUE(client.is_valid());
  }

 protected:
  fidl::SyncClient<fuchsia_examples_calculator::Calculator> client;
};

TEST_F(CalcTestFixture, AddIntegrationTest) {
  fidl::Result<fuchsia_examples_calculator::Calculator::Add> result =
      client->Add({{.a = 4.5, .b = 3.2}});
  if (!result.is_ok()) {
    FX_LOGS(ERROR) << "Calc Add() failed: " << result.error_value() << std::endl;
    FAIL();
  }
  ASSERT_EQ(result->sum(), 7.7);
}

TEST_F(CalcTestFixture, SubtractIntegrationTest) {
  fidl::Result<fuchsia_examples_calculator::Calculator::Subtract> result =
      client->Subtract({{.a = 7.7, .b = 3.2}});
  if (!result.is_ok()) {
    FX_LOGS(ERROR) << "Calc Subtract() failed: " << result.error_value() << std::endl;
    FAIL();
  }
  ASSERT_EQ(result->difference(), 4.5);
}

TEST_F(CalcTestFixture, MultiplyIntegrationTest) {
  fidl::Result<fuchsia_examples_calculator::Calculator::Multiply> result =
      client->Multiply({{.a = 1.5, .b = 2.0}});
  if (!result.is_ok()) {
    FX_LOGS(ERROR) << "Calc Multiply() failed: " << result.error_value() << std::endl;
    FAIL();
  }
  ASSERT_EQ(result->product(), 3.0);
}

TEST_F(CalcTestFixture, DivideIntegrationTest) {
  fidl::Result<fuchsia_examples_calculator::Calculator::Divide> result =
      client->Divide({{.dividend = 2.0, .divisor = 4.0}});
  if (!result.is_ok()) {
    FX_LOGS(ERROR) << "Calc Divide() failed: " << result.error_value() << std::endl;
    FAIL();
  }
  ASSERT_EQ(result->quotient(), 0.5);
}

TEST_F(CalcTestFixture, PowIntegrationTest) {
  fidl::Result<fuchsia_examples_calculator::Calculator::Pow> result =
      client->Pow({{.base = 3.0, .exponent = 4.0}});
  if (!result.is_ok()) {
    FX_LOGS(ERROR) << "Calc Pow() failed: " << result.error_value() << std::endl;
    FAIL();
  }
  ASSERT_EQ(result->power(), 81);
}
