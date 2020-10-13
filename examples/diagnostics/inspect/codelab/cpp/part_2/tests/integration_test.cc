// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/examples/inspect/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

constexpr char reverser_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_integration_tests#meta/"
    "inspect_cpp_codelab_part_2.cmx";

constexpr char fizzbuzz_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_integration_tests#meta/"
    "inspect_cpp_codelab_fizzbuzz.cmx";

class CodelabTest : public sys::testing::TestWithEnvironment {
 protected:
  // Options for each test.
  struct TestOptions {
    // If true, inject a FizzBuzz service implementation.
    bool include_fizzbuzz_service;
  };

  fuchsia::examples::inspect::ReverserPtr StartComponentAndConnect(TestOptions options) {
    // Create an environment for the test that simulates the "sys" realm.
    // We optionally inject the "FizzBuzz" service if requested.
    auto services = CreateServices();
    if (options.include_fizzbuzz_service) {
      services->AddServiceWithLaunchInfo({.url = fizzbuzz_url},
                                         fuchsia::examples::inspect::FizzBuzz::Name_);
    }
    environment_ = CreateNewEnclosingEnvironment("sys", std::move(services));

    // Start the Reverser component in the nested environment.
    fuchsia::io::DirectoryPtr directory_request;
    controller_ = environment_->CreateComponent(
        {.url = reverser_url, .directory_request = directory_request.NewRequest().TakeChannel()});

    // Connect to Reverser hosted by the new component.
    fuchsia::examples::inspect::ReverserPtr ret;
    sys::ServiceDirectory component_services(directory_request.Unbind());
    component_services.Connect(ret.NewRequest());

    bool ready = false;
    controller_.events().OnDirectoryReady = [&] { ready = true; };
    RunLoopUntil([&] { return ready; });

    return ret;
  }

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

TEST_F(CodelabTest, StartWithFizzBuzz) {
  auto ptr = StartComponentAndConnect({.include_fizzbuzz_service = true});

  bool error = false;
  ptr.set_error_handler([&](zx_status_t unused) { error = true; });

  bool done = false;
  std::string result;
  ptr->Reverse("hello", [&](std::string value) {
    result = std::move(value);
    done = true;
  });
  RunLoopUntil([&] { return done || error; });

  ASSERT_FALSE(error);
  EXPECT_EQ("olleh", result);
}

TEST_F(CodelabTest, StartWithoutFizzBuzz) {
  auto ptr = StartComponentAndConnect({.include_fizzbuzz_service = false});

  bool error = false;
  ptr.set_error_handler([&](zx_status_t unused) { error = true; });

  bool done = false;
  std::string result;
  ptr->Reverse("hello", [&](std::string value) {
    result = std::move(value);
    done = true;
  });
  RunLoopUntil([&] { return done || error; });

  ASSERT_FALSE(error);
  EXPECT_EQ("olleh", result);
}
