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
    ControllerProviderPtr provider;
    auto task = provider_->Serve(kFakeFuzzerUrl, registrar_->NewBinding().TakeChannel())
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

TEST_F(ControllerProviderTest, Connect) {
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
