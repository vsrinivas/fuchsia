// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <memory>

#include "src/sys/fuzzing/common/artifact.h"
#include "src/sys/fuzzing/common/async-socket.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/common/testing/integration-test-base.h"
#include "src/sys/fuzzing/common/testing/registrar.h"

namespace fuzzing {

using fuchsia::fuzzer::ControllerProviderPtr;
using fuchsia::fuzzer::ControllerPtr;

// Test fixtures.

// The |FrameworkIntegrationTest| fakes the registrar but uses the real framework/engine.
//
// TODO(fxbug.dev/71912): This could be converted to use RealmBuilder, at which point specific tests
// could provide individual components for the target adapter capability to be routed to. This would
// facilitate writing tests for the engine under specific scenarios, analogous to libFuzzer's tests
// under https://github.com/llvm/llvm-project/tree/main/compiler-rt/test/fuzzer.
class FrameworkIntegrationTest : public IntegrationTestBase {
 protected:
  // Creates a fake registrar and spawns a process for the engine.
  ZxPromise<ControllerPtr> Start() {
    registrar_ = std::make_unique<FakeRegistrar>(executor());
    return fpromise::make_promise([this] {
             // Spawn a new process with the startup channel to a fake registrar.
             return IntegrationTestBase::Start("/pkg/bin/component_fuzzing_engine",
                                               registrar_->Bind());
           })
        .and_then(registrar_->TakeProvider())
        .and_then([this, consumer_fut = Future<>(), controller = ControllerPtr()](
                      Context& context,
                      ControllerProviderHandle& handle) mutable -> ZxResult<ControllerPtr> {
          // Connect a controller to the spawned process.
          if (!consumer_fut) {
            auto request = controller.NewRequest(executor()->dispatcher());
            Bridge<> bridge;
            provider_ = handle.Bind();
            provider_->Connect(std::move(request), bridge.completer.bind());
            consumer_fut = bridge.consumer.promise();
          }
          if (!consumer_fut(context)) {
            return fpromise::pending();
          }
          return fpromise::ok(std::move(controller));
        })
        .wrap_with(scope_);
  }

  ZxPromise<> Stop() {
    provider_->Stop();
    return IntegrationTestBase::AwaitTermination();
  }

 private:
  std::unique_ptr<FakeRegistrar> registrar_;
  ControllerProviderPtr provider_;
  Scope scope_;
};

// Integration tests.

TEST_F(FrameworkIntegrationTest, Crash) {
  ControllerPtr controller;
  FUZZING_EXPECT_OK(Start(), &controller);
  RunUntilIdle();

  Input input("FUZZ");
  ZxBridge<FuzzResult> bridge1;
  controller->Execute(AsyncSocketWrite(executor(), input.Duplicate()),
                      ZxBind<FuzzResult>(std::move(bridge1.completer)));
  FUZZING_EXPECT_OK(bridge1.consumer.promise(), FuzzResult::CRASH);

  Bridge<Status> bridge2;
  controller->GetStatus(bridge2.completer.bind());
  Status status;
  FUZZING_EXPECT_OK(bridge2.consumer.promise(), &status);

  RunUntilIdle();
  EXPECT_TRUE(status.has_elapsed());

  ZxBridge<FidlArtifact> bridge3;
  controller->GetResults([completer = std::move(bridge3.completer)](FuzzResult fuzz_result,
                                                                    FidlInput fidl_input) mutable {
    completer.complete_ok(MakeFidlArtifact(fuzz_result, std::move(fidl_input)));
  });
  auto task = bridge3.consumer.promise().and_then([this](FidlArtifact& fidl_artifact) {
    return AsyncSocketRead(executor(), std::move(fidl_artifact));
  });
  Artifact artifact;
  FUZZING_EXPECT_OK(task, &artifact);
  RunUntilIdle();
  EXPECT_EQ(artifact.fuzz_result(), FuzzResult::CRASH);
  EXPECT_EQ(artifact.input(), input);
}

}  // namespace fuzzing
