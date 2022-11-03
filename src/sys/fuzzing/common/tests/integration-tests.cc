// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/tests/integration-tests.h"

#include "src/sys/fuzzing/common/artifact.h"
#include "src/sys/fuzzing/common/async-socket.h"
#include "src/sys/fuzzing/common/engine.h"
#include "src/sys/fuzzing/common/testing/component-context.h"

namespace fuzzing {

using ::fuchsia::fuzzer::FUZZ_MODE;

void EngineIntegrationTest::SetUp() {
  AsyncTest::SetUp();
  context_ = ComponentContextForTest::Create(executor());
  engine_ = std::make_unique<ChildProcess>(executor());
}

ZxPromise<ControllerPtr> EngineIntegrationTest::Start() {
  registrar_ = std::make_unique<FakeRegistrar>(executor());
  return fpromise::make_promise([this]() -> ZxResult<> {
           fidl::InterfaceHandle<Registrar> registrar = registrar_->NewBinding();
           engine_->Reset();
           engine_->AddArg(program_binary());
           engine_->AddArg(component_url());
           for (const auto& arg : extra_args()) {
             engine_->AddArg(arg);
           }
           engine_->AddArg(FUZZ_MODE);
           engine_->AddChannel(ComponentContextForTest::kRegistrarId, registrar.TakeChannel());
           engine_->AddChannel(ComponentContextForTest::kCoverageId, fuzz_coverage());
           return fpromise::ok();
         })
      .and_then([this]() { return AsZxResult(engine_->Spawn()); })
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
      .and_then(
          [this, consumer_fut = Future<zx_status_t>()](
              Context& context, ControllerPtr& controller) mutable -> ZxResult<ControllerPtr> {
            if (!consumer_fut) {
              Bridge<zx_status_t> bridge;
              Options options;
              set_options(options);
              controller->Configure(std::move(options), bridge.completer.bind());
              consumer_fut = bridge.consumer.promise();
            }
            if (!consumer_fut(context)) {
              return fpromise::pending();
            }
            if (consumer_fut.is_error()) {
              return fpromise::error(ZX_ERR_CANCELED);
            }
            if (auto status = consumer_fut.take_value(); status != ZX_OK) {
              return fpromise::error(status);
            }
            return fpromise::ok(std::move(controller));
          })
      .wrap_with(scope_);
}

void EngineIntegrationTest::TearDown() {
  Schedule(engine_->Kill());
  RunUntilIdle();
  AsyncTest::TearDown();
}

void EngineIntegrationTest::Crash() {
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
