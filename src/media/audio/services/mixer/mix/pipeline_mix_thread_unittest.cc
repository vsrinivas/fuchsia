// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/pipeline_mix_thread.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/consumer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/consumer_stage_wrapper.h"
#include "src/media/audio/services/mixer/mix/testing/pipeline_mix_thread_without_loop.h"

namespace media_audio {
namespace {

using StartCommand = ConsumerStage::StartCommand;
using StopCommand = ConsumerStage::StopCommand;
using ::fuchsia_audio::SampleType;
using ::fuchsia_audio_mixer::PipelineDirection;
using ::testing::ElementsAre;
using ::testing::FieldsAre;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});

}  // namespace

class PipelineMixThreadRunMixJobsTest : public ::testing::Test {
 public:
  static constexpr auto kPeriod = zx::msec(100);
  static constexpr int64_t kPeriodFrames = 4800;

  PipelineMixThreadRunMixJobsTest() {
    // Since these tests don't call RunLoop, stop the timer so that `realm_->AdvanceTo` doesn't wait
    // for this timer to be blocked in `SleepUntil`.
    timer_->Stop();
  }

  SyntheticClockRealm& realm() { return *realm_; }
  std::shared_ptr<const Clock> mono_clock() { return mono_clock_; }
  PipelineMixThread& thread() { return *thread_; }

  zx::time RunMixJobs(zx::time mono_start_time, zx::time mono_now) TA_REQ(thread().checker()) {
    return thread().RunMixJobs(mono_start_time, mono_now);
  }

 private:
  std::shared_ptr<GlobalTaskQueue> task_queue_ = std::make_shared<GlobalTaskQueue>();
  std::shared_ptr<SyntheticClockRealm> realm_ = SyntheticClockRealm::Create();
  std::shared_ptr<Timer> timer_ = realm_->CreateTimer();
  std::shared_ptr<const Clock> mono_clock_ =
      realm_->CreateClock("mono_clock", Clock::kMonotonicDomain, false);

  std::shared_ptr<PipelineMixThread> thread_ = CreatePipelineMixThreadWithoutLoop({
      .id = 1,
      .name = "TestThread",
      .mix_period = kPeriod,
      .cpu_per_period = kPeriod / 2,
      .global_task_queue = task_queue_,
      .timer = timer_,
      .mono_clock = mono_clock_,
  });
};

TEST_F(PipelineMixThreadRunMixJobsTest, RunAfterDeadline) {
  ScopedThreadChecker checker(thread().checker());

  // pt0 is the presentation time consumed by c.consumer->RunMixJob(ctx, 0, kPeriod). Since we
  // consume one period ahead, this is start of the second mix period.
  const auto pt0 = zx::time(0) + kPeriod;
  ConsumerStageWrapper c(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                         UnreadableClock(mono_clock()));
  c.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  thread().AddConsumer(c.consumer);
  thread().NotifyConsumerStarting(c.consumer);
  thread().AddClock(mono_clock());

  // Try various cases where we try to run the first mix job past its deadline.
  realm().AdvanceTo(zx::time(0) + kPeriod);
  EXPECT_EQ(RunMixJobs(zx::time(0), realm().now()), zx::time(0) + 2 * kPeriod);
  EXPECT_THAT(c.writer->packets(), ElementsAre());

  realm().AdvanceTo(zx::time(0) + 2 * kPeriod - zx::nsec(1));
  EXPECT_EQ(RunMixJobs(zx::time(0), realm().now()), zx::time(0) + 2 * kPeriod);
  EXPECT_THAT(c.writer->packets(), ElementsAre());

  realm().AdvanceTo(zx::time(0) + 2 * kPeriod);
  EXPECT_EQ(RunMixJobs(zx::time(0), realm().now()), zx::time(0) + 3 * kPeriod);
  EXPECT_THAT(c.writer->packets(), ElementsAre());
}

TEST_F(PipelineMixThreadRunMixJobsTest, OneConsumerUnstarted) {
  ScopedThreadChecker checker(thread().checker());

  ConsumerStageWrapper c(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                         UnreadableClock(mono_clock()));
  thread().AddConsumer(c.consumer);
  thread().NotifyConsumerStarting(c.consumer);
  thread().AddClock(mono_clock());

  // Not started and has no queued StartCommand.
  realm().AdvanceTo(zx::time(0));
  EXPECT_EQ(RunMixJobs(zx::time(0), realm().now()), zx::time::infinite());
  EXPECT_THAT(c.writer->packets(), ElementsAre());
}

TEST_F(PipelineMixThreadRunMixJobsTest, OneConsumerStartCommandQueued) {
  ScopedThreadChecker checker(thread().checker());

  // pt0 is the presentation time consumed by c.consumer->RunMixJob(ctx, 0, kPeriod). Since we
  // consume one period ahead, this is start of the second mix period.
  const auto pt0 = zx::time(0) + kPeriod;
  ConsumerStageWrapper c(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                         UnreadableClock(mono_clock()));

  // The consumer starts after the first mix job.
  c.command_queue->push(StartCommand{
      .start_presentation_time = pt0 + 3 * kPeriod,
      .start_frame = 3 * kPeriodFrames,
  });
  thread().AddConsumer(c.consumer);
  thread().NotifyConsumerStarting(c.consumer);
  thread().AddClock(mono_clock());

  // Not started, but there's a queued StartCommand. The actual timeline should be:
  //
  //  t=0*kPeriod: start of first mix job
  //  t=1*kPeriod: end of first mix job,
  //  t=3*kPeriod: start of second mix job, StartCommand takes effect now
  //
  // Since RunMixJobs returns the time of the next job, it seems like RunMixJob should return
  // `t=3*kPeriod`, but it does not: RunMixJobs conservatively assumes that the consumer's clock
  // might be adjusted to run up to +1000 PPM faster than the system monotonic clock, meaning the
  // second mix job might start as early as `1*kPeriod + 2*kPeriod*1000/1001`.
  realm().AdvanceTo(zx::time(0));
  EXPECT_EQ(RunMixJobs(realm().now(), realm().now()),
            zx::time(0) + 1 * kPeriod + 2 * kPeriod * 1000 / 1001);
  EXPECT_THAT(c.writer->packets(), ElementsAre());

  // Advance to when the consumer starts. This job should write silence (the packet queue is empty).
  realm().AdvanceTo(zx::time(0) + 3 * kPeriod);
  EXPECT_EQ(RunMixJobs(realm().now(), realm().now()), zx::time(0) + 4 * kPeriod);
  EXPECT_THAT(c.writer->packets(), ElementsAre(FieldsAre(/*is_silent=*/true, 3 * kPeriodFrames,
                                                         kPeriodFrames, nullptr)));
}

TEST_F(PipelineMixThreadRunMixJobsTest, OneConsumerStarted) {
  ScopedThreadChecker checker(thread().checker());

  // pt0 is the presentation time consumed by c.consumer->RunMixJob(ctx, 0, kPeriod). Since we
  // consume one period ahead, this is start of the second mix period.
  const auto pt0 = zx::time(0) + kPeriod;
  ConsumerStageWrapper c(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                         UnreadableClock(mono_clock()));
  c.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  auto payload0 = c.PushPacket(Fixed(0), kPeriodFrames);
  auto payload1 = c.PushPacket(Fixed(kPeriodFrames), kPeriodFrames);
  thread().AddConsumer(c.consumer);
  thread().NotifyConsumerStarting(c.consumer);
  thread().AddClock(mono_clock());

  // First job writes the first packet.
  realm().AdvanceTo(zx::time(0));
  EXPECT_EQ(RunMixJobs(realm().now(), realm().now()), zx::time(0) + kPeriod);
  EXPECT_THAT(c.writer->packets(),
              ElementsAre(FieldsAre(false, 0, kPeriodFrames, payload0->data())));
  c.writer->packets().clear();

  // Second job writes the second packet.
  realm().AdvanceTo(zx::time(0) + kPeriod);
  EXPECT_EQ(RunMixJobs(realm().now(), realm().now()), zx::time(0) + 2 * kPeriod);
  EXPECT_THAT(c.writer->packets(),
              ElementsAre(FieldsAre(false, kPeriodFrames, kPeriodFrames, payload1->data())));
}

TEST_F(PipelineMixThreadRunMixJobsTest, OneConsumerStartedNonMonotonicClock) {
  ScopedThreadChecker checker(thread().checker());

  // The reference clock runs -1000 PPM slower than the system monotonic clock.
  auto clock = realm().CreateClock("ref_clock", Clock::kExternalDomain, true);
  clock->SetRate(-1000);

  // The consumer's period length is scaled by the consumer's clock rate.
  constexpr auto kConsumerPeriod = kPeriod * 999 / 1000;
  constexpr auto kConsumerPeriodFrames = kPeriodFrames * 999 / 1000;

  // For this test to be useful, kPeriod should be large enough that the consumer's period is at
  // least one frame smaller than the system monotonic period.
  static_assert(kConsumerPeriodFrames < kPeriodFrames);

  // pt0 is the presentation time consumed by c.consumer->RunMixJob(ctx, 0, kConsumerPeriod). Since
  // we consume one period ahead, this is start of the second mix period.
  const auto pt0 = zx::time(0) + kConsumerPeriod;
  ConsumerStageWrapper c(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                         UnreadableClock(clock));
  c.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  thread().AddConsumer(c.consumer);
  thread().NotifyConsumerStarting(c.consumer);
  thread().AddClock(clock);

  // Since kConsumerPeriod contains 4795.2 frames, every 5th mix job should write an extra frame.
  std::vector<std::shared_ptr<std::vector<float>>> payloads = {
      c.PushPacket(Fixed(0 * kConsumerPeriodFrames), kConsumerPeriodFrames),
      c.PushPacket(Fixed(1 * kConsumerPeriodFrames), kConsumerPeriodFrames),
      c.PushPacket(Fixed(2 * kConsumerPeriodFrames), kConsumerPeriodFrames),
      c.PushPacket(Fixed(3 * kConsumerPeriodFrames), kConsumerPeriodFrames),
      c.PushPacket(Fixed(4 * kConsumerPeriodFrames), kConsumerPeriodFrames + 1),
  };

  realm().AdvanceTo(zx::time(0));

  int64_t output_frame = 0;
  for (size_t k = 0; k < payloads.size(); k++) {
    SCOPED_TRACE("packet[" + std::to_string(k) + "]");
    const auto packet_frames = payloads[k]->size() / kFormat.channels();
    EXPECT_EQ(RunMixJobs(realm().now(), realm().now()), realm().now() + kPeriod);
    EXPECT_THAT(c.writer->packets(),
                ElementsAre(FieldsAre(false, output_frame, packet_frames, payloads[k]->data())));
    c.writer->packets().clear();
    realm().AdvanceTo(realm().now() + kPeriod);
    output_frame += packet_frames;
  }
}

TEST_F(PipelineMixThreadRunMixJobsTest, OneConsumerStopsDuringJob) {
  ScopedThreadChecker checker(thread().checker());

  // pt0 is the presentation time consumed by c.consumer->RunMixJob(ctx, 0, kPeriod). Since we
  // consume one period ahead, this is start of the second mix period.
  const auto pt0 = zx::time(0) + kPeriod;
  ConsumerStageWrapper c(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                         UnreadableClock(mono_clock()));
  c.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  c.command_queue->push(StopCommand{.stop_frame = 1});
  thread().AddConsumer(c.consumer);
  thread().NotifyConsumerStarting(c.consumer);
  thread().AddClock(mono_clock());

  // First job writes 1 frame then stops.
  realm().AdvanceTo(zx::time(0));
  EXPECT_EQ(RunMixJobs(realm().now(), realm().now()), zx::time::infinite());
  EXPECT_THAT(c.writer->packets(), ElementsAre(FieldsAre(/*is_silent=*/true, 0, 1, nullptr)));
}

TEST_F(PipelineMixThreadRunMixJobsTest, MultipleConsumers) {
  ScopedThreadChecker checker(thread().checker());

  // pt0 is the presentation time consumed by c.consumer->RunMixJob(ctx, 0, kPeriod). Since we
  // consume one period ahead, this is start of the second mix period.
  const auto pt0 = zx::time(0) + kPeriod;

  ConsumerStageWrapper c0(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                          UnreadableClock(mono_clock()));
  ConsumerStageWrapper c1(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                          UnreadableClock(mono_clock()));
  ConsumerStageWrapper c2(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                          UnreadableClock(mono_clock()));

  c0.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  c1.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});
  c2.command_queue->push(StartCommand{.start_presentation_time = pt0, .start_frame = 0});

  std::vector<int> write_order;
  c0.writer->SetOnWriteSilence([&write_order](auto, auto) { write_order.push_back(0); });
  c1.writer->SetOnWriteSilence([&write_order](auto, auto) { write_order.push_back(1); });
  c2.writer->SetOnWriteSilence([&write_order](auto, auto) { write_order.push_back(2); });

  c0.consumer->set_max_downstream_consumers(2);
  c1.consumer->set_max_downstream_consumers(1);
  c2.consumer->set_max_downstream_consumers(0);

  // Push in unsorted order to verify sorting.
  thread().AddConsumer(c1.consumer);
  thread().AddConsumer(c0.consumer);
  thread().AddConsumer(c2.consumer);
  thread().NotifyConsumerStarting(c1.consumer);
  thread().NotifyConsumerStarting(c0.consumer);
  thread().NotifyConsumerStarting(c2.consumer);
  thread().AddClock(mono_clock());

  // First mix job should write one packet of silence to each consumer, in order {c0,c1,c2}.
  realm().AdvanceTo(zx::time(0));
  EXPECT_EQ(RunMixJobs(realm().now(), realm().now()), zx::time(0) + kPeriod);

  EXPECT_THAT(write_order, ElementsAre(0, 1, 2));
  EXPECT_THAT(c0.writer->packets(),
              ElementsAre(FieldsAre(/*is_silent=*/true, 0, kPeriodFrames, nullptr)));
  EXPECT_THAT(c1.writer->packets(),
              ElementsAre(FieldsAre(/*is_silent=*/true, 0, kPeriodFrames, nullptr)));
  EXPECT_THAT(c2.writer->packets(),
              ElementsAre(FieldsAre(/*is_silent=*/true, 0, kPeriodFrames, nullptr)));
}

class PipelineMixThreadRunLoopTest : public ::testing::Test {
 public:
  static constexpr auto kPeriod = zx::msec(10);
  static constexpr int64_t kPeriodFrames = 480;

  GlobalTaskQueue& task_queue() { return *task_queue_; }
  SyntheticClockRealm& realm() { return *realm_; }
  std::shared_ptr<const Clock> mono_clock() { return mono_clock_; }
  PipelineMixThread& thread() { return *thread_; }

 private:
  std::shared_ptr<GlobalTaskQueue> task_queue_ = std::make_shared<GlobalTaskQueue>();
  std::shared_ptr<SyntheticClockRealm> realm_ = SyntheticClockRealm::Create();
  std::shared_ptr<const Clock> mono_clock_ =
      realm_->CreateClock("mono_clock", Clock::kMonotonicDomain, false);

  std::shared_ptr<PipelineMixThread> thread_ = PipelineMixThread::Create({
      .id = 1,
      .name = "TestThread",
      .mix_period = kPeriod,
      .cpu_per_period = kPeriod / 2,
      .global_task_queue = task_queue_,
      .timer = realm_->CreateTimer(),
      .mono_clock = mono_clock_,
  });
};

TEST_F(PipelineMixThreadRunLoopTest, AddStartedConsumers) {
  // pt0 is the presentation time consumed by consumer->RunMixJob(ctx, 0, kPeriod). Since we
  // consume one period ahead, this is start of the second mix period.
  const auto pt0 = zx::time(0) + kPeriod;
  ConsumerStageWrapper c0(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                          UnreadableClock(mono_clock()));
  ConsumerStageWrapper c1(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                          UnreadableClock(mono_clock()));

  // Queue start and stop commands for both consumers. Since these are queued before we call
  // AddConsumer, we shouldn't need to call NotifyConsumerStarting.
  c0.command_queue->push(StartCommand{
      .start_presentation_time = pt0 + 2 * kPeriod,
      .start_frame = 2 * kPeriodFrames,
  });
  c1.command_queue->push(StartCommand{
      .start_presentation_time = pt0 + 5 * kPeriod,
      .start_frame = 5 * kPeriodFrames,
  });
  c0.command_queue->push(StopCommand{.stop_frame = 2 * kPeriodFrames + 10});
  c1.command_queue->push(StopCommand{.stop_frame = 5 * kPeriodFrames + 10});

  // Add both consumers.
  task_queue().Push(thread().id(), [this, &c0, &c1] {
    ScopedThreadChecker checker(thread().checker());
    thread().AddConsumer(c0.consumer);
    thread().AddConsumer(c1.consumer);
    thread().NotifyConsumerStarting(c0.consumer);
    thread().NotifyConsumerStarting(c1.consumer);
    thread().AddClock(mono_clock());
  });

  EXPECT_THAT(c0.writer->packets(), ElementsAre());
  EXPECT_THAT(c1.writer->packets(), ElementsAre());

  // Advance to the third period, which should produce a packet from c0 but not c1.
  realm().AdvanceTo(zx::time(0) + 2 * kPeriod);
  EXPECT_THAT(c0.writer->packets(),
              ElementsAre(FieldsAre(/*is_silent=*/true, 2 * kPeriodFrames, 10, nullptr)));
  EXPECT_THAT(c1.writer->packets(), ElementsAre());
  c0.writer->packets().clear();

  // Advance to the sixth period, which should produce a packet from c1 but not c2.
  realm().AdvanceTo(zx::time(0) + 5 * kPeriod);
  EXPECT_THAT(c0.writer->packets(), ElementsAre());
  EXPECT_THAT(c1.writer->packets(),
              ElementsAre(FieldsAre(/*is_silent=*/true, 5 * kPeriodFrames, 10, nullptr)));
}

TEST_F(PipelineMixThreadRunLoopTest, AddRemoveUnstartedConsumers) {
  // pt0 is the presentation time consumed by consumer->RunMixJob(ctx, 0, kPeriod). Since we
  // consume one period ahead, this is start of the second mix period.
  const auto pt0 = zx::time(0) + kPeriod;
  ConsumerStageWrapper c0(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                          UnreadableClock(mono_clock()));

  // Add this consumer.
  task_queue().Push(thread().id(), [this, &c0] {
    ScopedThreadChecker checker(thread().checker());
    thread().AddConsumer(c0.consumer);
    thread().NotifyConsumerStarting(c0.consumer);
    thread().AddClock(mono_clock());
  });
  EXPECT_THAT(c0.writer->packets(), ElementsAre());

  // Advancing should run the above task plus the first mix job. The consumer is stopped, so there's
  // no output.
  realm().AdvanceTo(zx::time(0));
  EXPECT_THAT(c0.writer->packets(), ElementsAre());

  // Start the consumer then advance through the second mix job. This should produce a packet.
  c0.command_queue->push(StartCommand{
      .start_presentation_time = pt0 + kPeriod,
      .start_frame = kPeriodFrames,
  });
  task_queue().Push(thread().id(), [this, &c0] {
    ScopedThreadChecker checker(thread().checker());
    thread().NotifyConsumerStarting(c0.consumer);
  });
  realm().AdvanceTo(zx::time(0) + kPeriod);
  EXPECT_THAT(c0.writer->packets(),
              ElementsAre(FieldsAre(/*is_silent=*/true, kPeriodFrames, kPeriodFrames, nullptr)));
  c0.writer->packets().clear();

  // Replace this consumer with another unstarted consumer.
  ConsumerStageWrapper c1(kFormat, /*presentation_delay=*/zx::nsec(0), PipelineDirection::kOutput,
                          UnreadableClock(mono_clock()));

  // Add this consumer.
  task_queue().Push(thread().id(), [this, &c0, &c1] {
    ScopedThreadChecker checker(thread().checker());
    thread().RemoveConsumer(c0.consumer);
    thread().AddConsumer(c1.consumer);
    thread().NotifyConsumerStarting(c1.consumer);
  });
  EXPECT_THAT(c0.writer->packets(), ElementsAre());
  EXPECT_THAT(c1.writer->packets(), ElementsAre());

  // Advance to the third mix job. The consumer is stopped, so there's no output.
  realm().AdvanceTo(zx::time(0) + 2 * kPeriod);
  EXPECT_THAT(c0.writer->packets(), ElementsAre());
  EXPECT_THAT(c1.writer->packets(), ElementsAre());

  // Start the consumer then advance through the fourth mix job. This should produce a packet.
  c1.command_queue->push(StartCommand{
      .start_presentation_time = pt0 + 3 * kPeriod,
      .start_frame = 3 * kPeriodFrames,
  });
  task_queue().Push(thread().id(), [this, &c1] {
    ScopedThreadChecker checker(thread().checker());
    thread().NotifyConsumerStarting(c1.consumer);
  });
  realm().AdvanceTo(zx::time(0) + 3 * kPeriod);
  EXPECT_THAT(c0.writer->packets(), ElementsAre());
  EXPECT_THAT(c1.writer->packets(), ElementsAre(FieldsAre(/*is_silent=*/true, 3 * kPeriodFrames,
                                                          kPeriodFrames, nullptr)));
}

}  // namespace media_audio
