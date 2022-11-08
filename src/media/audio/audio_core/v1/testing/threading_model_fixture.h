// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_THREADING_MODEL_FIXTURE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_THREADING_MODEL_FIXTURE_H_

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/compiler.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/audio_core/shared/process_config.h"
#include "src/media/audio/audio_core/v1/context.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_core_clock_factory.h"
#include "src/media/audio/audio_core/v1/testing/fake_plug_detector.h"
#include "src/media/audio/audio_core/v1/threading_model.h"

namespace media::audio::testing {

// Implements a |ThreadingModel| on top of the |async::TestLoop| to enable easily writing unit tests
// against components that depend on |ThreadingModel|.
class TestThreadingModel : public ThreadingModel {
 public:
  explicit TestThreadingModel(async::TestLoop* test_loop) : loop_(test_loop) {}
  ~TestThreadingModel() override = default;

  // |ThreadingModel|
  ExecutionDomain& FidlDomain() override { return fidl_holder_.domain; }
  ExecutionDomain& IoDomain() override { return io_holder_.domain; }
  OwnedDomainPtr AcquireMixDomain(const std::string& name) override {
    return OwnedDomainPtr(&mix_holder_.domain, [](ExecutionDomain*) {});
  }
  void Quit() override { loop_->Quit(); }
  // Note we should never call this on the |TestThreadingModel|. Execution should instead be
  // controlled using the |async::TestLoop| used to construct this |ThreadingModel|.
  void RunAndJoinAllThreads() override {
    FX_CHECK(false) << "RunAndJoinAllThreads not supported on TestThreadingModel.";
  }

 private:
  struct DomainHolder {
    DomainHolder(async::TestLoop* test_loop, const std::string& name)
        : loop(test_loop->StartNewLoop()),
          executor(loop->dispatcher()),
          domain{loop->dispatcher(), &executor, name} {}

    std::unique_ptr<async::LoopInterface> loop;
    async::Executor executor;
    ExecutionDomain domain;
  };

  async::TestLoop* loop_;
  DomainHolder fidl_holder_{loop_, "fidl"};
  DomainHolder io_holder_{loop_, "io"};
  DomainHolder mix_holder_{loop_, "mix"};
};

// A test fixture that provides a |ThreadingModel| on top of an |async::TestLoop|. We inherit from
// |gtest::TestLoopFixture| to make it simple to convert tests that are already using test loops.
//
// Ex:
//   TEST_F(MyTest, Foo) {
//     UnderTest bar(&threading_model());
//     bar.ScheduleSomeWork();
//     RunLoopUntilIdle();
//
//     AssertScheduledWorkCompleted(&bar);
//   }
class ThreadingModelFixture : public gtest::TestLoopFixture {
 public:
  enum ClockFactoryMode {
    WithSyntheticClocks,
    WithRealClocks,
  };

  explicit ThreadingModelFixture(ProcessConfig config,
                                 ClockFactoryMode mode = WithSyntheticClocks) {
    auto threading_model = std::make_unique<TestThreadingModel>(&test_loop());
    auto plug_detector = std::make_unique<testing::FakePlugDetector>();
    fake_plug_detector_ = plug_detector.get();
    context_ =
        Context::Create(std::move(threading_model), component_context_provider_.TakeContext(),
                        std::move(plug_detector), std::move(config),
                        mode == WithRealClocks ? std::make_shared<AudioCoreClockFactory>()
                                               : std::make_shared<FakeAudioCoreClockFactory>());
  }

  // The default constructor provides a reasonable valid default configuration.
  explicit ThreadingModelFixture(ClockFactoryMode mode = WithSyntheticClocks)
      : ThreadingModelFixture(
            ProcessConfig::Builder()
                .AddDeviceProfile(
                    {std::nullopt, DeviceConfig::OutputDeviceProfile(
                                       /* eligible_for_loopback */ true,
                                       StreamUsageSetFromRenderUsages(kFidlRenderUsages))})
                .SetDefaultVolumeCurve(
                    VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
                .Build(),
            mode) {}

 protected:
  // This threading model will be backed by an |async::TestLoop|. Control the loop using the methods
  // in |gtest::TestLoopFixture|.
  ThreadingModel& threading_model() { return context_->threading_model(); }

  Context& context() { return *context_; }

  FakePlugDetector* fake_plug_detector() const { return fake_plug_detector_; }

 private:
  void Init(ProcessConfig config) {}
  TestThreadingModel threading_model_{&test_loop()};
  sys::testing::ComponentContextProvider component_context_provider_;
  testing::FakePlugDetector* fake_plug_detector_;
  std::unique_ptr<Context> context_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_THREADING_MODEL_FIXTURE_H_
