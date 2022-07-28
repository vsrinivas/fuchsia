// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/controller-provider.h"

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/controller.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/common/testing/registrar.h"
#include "src/sys/fuzzing/common/testing/runner.h"

namespace fuzzing {

using ::fuchsia::fuzzer::ControllerProviderPtr;
using ::fuchsia::fuzzer::ControllerPtr;

// Test fixtures

const char* kFakeFuzzerUrl = "fuchsia-pkg://fuchsia.com/fuzzing-common-tests#meta/fake.cm";

// This is just a small helper type to work around the madness of constness and string literals with
// modifiable command lines.
class FakeCmdline {
 public:
  explicit FakeCmdline(std::initializer_list<const char*> literals) : argc_(0) {
    for (const auto& literal : literals) {
      chars_.emplace_back(literal, literal + strlen(literal) + 1);
      ptrs_.push_back(&chars_[argc_++][0]);
    }
    argv_ = &ptrs_[0];
  }
  ~FakeCmdline() = default;
  int argc() { return argc_; }
  char** argv() { return argv_; }

 private:
  int argc_;
  char** argv_;
  std::vector<std::vector<char>> chars_;
  std::vector<char*> ptrs_;
};

class ControllerProviderTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    registrar_ = std::make_unique<FakeRegistrar>(executor());
    provider_ = std::make_unique<ControllerProviderImpl>(executor());
    runner_ = FakeRunner::MakePtr(executor());
    provider_->SetRunner(runner_);
  }

  ControllerProviderPtr GetProvider() {
    FakeCmdline cmdline{"some-bin", kFakeFuzzerUrl};
    int argc = cmdline.argc();
    char** argv = cmdline.argv();
    EXPECT_EQ(provider_->Initialize(&argc, &argv), ZX_OK);
    ControllerProviderPtr provider;
    auto task = provider_->Serve(registrar_->NewBinding().TakeChannel())
                    .or_else([] { return fpromise::error(ZX_ERR_CANCELED); })
                    .and_then(registrar_->TakeProvider())
                    .and_then([this, &provider](ControllerProviderHandle& handle) -> ZxResult<> {
                      provider.Bind(std::move(handle), executor()->dispatcher());
                      return fpromise::ok();
                    });
    FUZZING_EXPECT_OK(std::move(task));
    RunUntilIdle();
    return provider;
  }

  auto runner() const { return std::static_pointer_cast<FakeRunner>(runner_); }

 private:
  std::unique_ptr<FakeRegistrar> registrar_;
  std::unique_ptr<ControllerProviderImpl> provider_;
  RunnerPtr runner_;
};

// Unit tests

TEST_F(ControllerProviderTest, Initialize) {
  ControllerProviderImpl provider(executor());
  FakeCmdline cmdline{"some-bin", kFakeFuzzerUrl, "some-args"};
  int argc = cmdline.argc();
  char** argv = cmdline.argv();
  argc = 1;
  EXPECT_EQ(provider.Initialize(&argc, &argv), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(argc, 1);
  EXPECT_STREQ(argv[0], "some-bin");
  argc = 3;
  EXPECT_EQ(provider.Initialize(&argc, &argv), ZX_OK);
  EXPECT_EQ(argc, 2);
  EXPECT_STREQ(argv[0], "some-bin");
  EXPECT_STREQ(argv[1], "some-args");
}

TEST_F(ControllerProviderTest, PublishAndConnect) {
  auto provider = GetProvider();

  // Should be able to connect...
  ControllerPtr ptr1;
  Bridge<> bridge1;
  provider->Connect(ptr1.NewRequest(), bridge1.completer.bind());
  FUZZING_EXPECT_OK(bridge1.consumer.promise());
  RunUntilIdle();

  // ...and reconnect.
  ControllerPtr ptr2;
  Bridge<> bridge2;
  provider->Connect(ptr2.NewRequest(), bridge2.completer.bind());
  FUZZING_EXPECT_OK(bridge2.consumer.promise());
  RunUntilIdle();
}

TEST_F(ControllerProviderTest, Stop) {
  auto provider = GetProvider();

  FUZZING_EXPECT_OK(runner()->AwaitStop());
  provider->Stop();
  RunUntilIdle();
}

}  // namespace fuzzing
