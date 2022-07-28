// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <zircon/status.h>

#include <memory>

#include "src/sys/fuzzing/common/artifact.h"
#include "src/sys/fuzzing/common/async-socket.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/child-process.h"
#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/common/testing/registrar.h"

namespace fuzzing {

using fuchsia::fuzzer::ControllerProviderPtr;
using fuchsia::fuzzer::ControllerPtr;
using fuchsia::fuzzer::CoverageDataProvider;
using fuchsia::fuzzer::Registrar;

// Test fixtures.

const char* kFuzzerUrl = "fuchsia-pkg://fuchsia.com/realmfuzzer-integration-tests#meta/fake.cm";

// The |RealmFuzzerTest| fakes the registrar but uses the real realmfuzzer engine.
//
// TODO(fxbug.dev/71912): This could be converted to use RealmBuilder, at which point specific tests
// could provide individual components for the target adapter capability to be routed to. This would
// facilitate writing tests for the engine under specific scenarios, analogous to libFuzzer's tests
// under https://github.com/llvm/llvm-project/tree/main/compiler-rt/test/fuzzer.
class RealmFuzzerTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    context_ = ComponentContext::CreateWithExecutor(executor());
    process_ = std::make_unique<ChildProcess>(executor());
  }

  // Creates fake registry and coverage components, and spawns the engine.
  ZxPromise<ControllerPtr> Start() {
    registrar_ = std::make_unique<FakeRegistrar>(executor());
    return fpromise::make_promise([this]() -> ZxResult<> {
             fidl::InterfaceHandle<Registrar> registrar_handle = registrar_->NewBinding();
             fidl::InterfaceHandle<CoverageDataProvider> provider_handle;
             if (auto status = context_->Connect(provider_handle.NewRequest()); status != ZX_OK) {
               FX_LOGS(ERROR) << "Failed to connect to fuzz_coverage: "
                              << zx_status_get_string(status);
               return fpromise::error(status);
             }
             process_->AddChannel(registrar_handle.TakeChannel());
             process_->AddChannel(provider_handle.TakeChannel());
             process_->AddArgs({"bin/realmfuzzer_engine", kFuzzerUrl});
             return fpromise::ok();
           })
        .and_then(process_->SpawnAsync())
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
    return process_->Wait();
  }

  void TearDown() override {
    process_->Kill();
    AsyncTest::TearDown();
  }

 private:
  std::unique_ptr<ComponentContext> context_;
  std::unique_ptr<ChildProcess> process_;
  ControllerProviderPtr provider_;
  std::unique_ptr<FakeRegistrar> registrar_;
  Scope scope_;
};

// Integration tests.

TEST_F(RealmFuzzerTest, Crash) {
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
