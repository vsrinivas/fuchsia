// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/base_consumer_stage.h"

#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/fake_consumer_stage_writer.h"
#include "src/media/audio/services/mixer/mix/testing/fake_pipeline_thread.h"

namespace media_audio {
namespace {

using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::VariantWith;

const Format kFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});

class BaseConsumerStageTest : public testing::Test {
 public:
  BaseConsumerStageTest() {
    packet_queue_ = MakeDefaultPacketQueue(kFormat),
    writer_ = std::make_shared<FakeConsumerStageWriter>();
    consumer_ = std::make_shared<BaseConsumerStage>(BaseConsumerStage::Args{
        .format = kFormat,
        .reference_clock = DefaultUnreadableClock(),
        .thread = std::make_shared<FakePipelineThread>(1),
        .writer = writer_,
    });
    // Frame 0 is presented at time 0.
    consumer_->UpdatePresentationTimeToFracFrame(
        TimelineFunction(0, 0, kFormat.frac_frames_per_ns()));
    consumer_->AddSource(packet_queue_, {});
  }

  std::shared_ptr<std::vector<float>> PushPacket(Fixed start_frame, int64_t length) {
    auto payload = std::make_shared<std::vector<float>>(length * kFormat.channels());
    packet_queue_->push(PacketView({kFormat, start_frame, length, payload->data()}));
    return payload;
  }

  BaseConsumerStage& consumer() { return *consumer_; }
  FakeConsumerStageWriter& writer() { return *writer_; }

 private:
  std::shared_ptr<BaseConsumerStage> consumer_;
  std::shared_ptr<FakeConsumerStageWriter> writer_;
  std::shared_ptr<SimplePacketQueueProducerStage> packet_queue_;
};

TEST_F(BaseConsumerStageTest, Empty) {
  consumer().CopyFromSource(DefaultCtx(), 0, 10);
  EXPECT_THAT(writer().packets(), ElementsAre(FieldsAre(true, 0, 10, nullptr)));
}

TEST_F(BaseConsumerStageTest, SinglePacket) {
  auto payload0 = PushPacket(Fixed(0), 10);
  consumer().CopyFromSource(DefaultCtx(), 0, 10);
  EXPECT_THAT(writer().packets(), ElementsAre(FieldsAre(false, 0, 10, payload0->data())));
}

TEST_F(BaseConsumerStageTest, MultiplePacketsWithGaps) {
  auto payload1 = PushPacket(Fixed(10), 10);
  auto payload3 = PushPacket(Fixed(30), 10);
  auto payload4 = PushPacket(Fixed(40), 10);

  consumer().CopyFromSource(DefaultCtx(), 0, 60);

  // Should have the above three packets with silence at packets 0, 2, and 5.
  EXPECT_THAT(writer().packets(), ElementsAre(FieldsAre(true, 0, 10, nullptr),             //
                                              FieldsAre(false, 10, 10, payload1->data()),  //
                                              FieldsAre(true, 20, 10, nullptr),            //
                                              FieldsAre(false, 30, 10, payload3->data()),  //
                                              FieldsAre(false, 40, 10, payload4->data()),  //
                                              FieldsAre(true, 50, 10, nullptr)));
}

TEST_F(BaseConsumerStageTest, PacketAtFractionalOffset) {
  auto payload0 = PushPacket(Fixed(10) + ffl::FromRatio(1, 4), 5);
  auto payload1 = PushPacket(Fixed(16), 32);

  consumer().CopyFromSource(DefaultCtx(), 0, 50);

  // The first frame of packet0 overlaps frame 11.
  EXPECT_THAT(writer().packets(), ElementsAre(FieldsAre(true, 0, 11, nullptr),            //
                                              FieldsAre(false, 11, 5, payload0->data()),  //
                                              FieldsAre(false, 16, 32, payload1->data()),
                                              FieldsAre(true, 48, 2, nullptr)));
}

}  // namespace
}  // namespace media_audio
