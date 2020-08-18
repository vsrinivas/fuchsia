// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/clock.h>
#include <threads.h>

#include <set>

#include <audio-proto-utils/format-utils.h>
#include <zxtest/zxtest.h>

namespace {

namespace audio_fidl = ::llcpp::fuchsia::hardware::audio;

audio_fidl::PcmFormat GetDefaultPcmFormat() {
  audio_fidl::PcmFormat format;
  format.number_of_channels = 2;
  format.channels_to_use_bitmask = 0x03;
  format.sample_format = audio_fidl::SampleFormat::PCM_SIGNED;
  format.frame_rate = 48000;
  format.bytes_per_sample = 2;
  format.valid_bits_per_sample = 16;
  return format;
}

}  // namespace

namespace audio {

class MockSimpleAudio : public SimpleAudioStream {
 public:
  static constexpr uint32_t kTestFrameRate = 48000;
  static constexpr uint8_t kTestNumberOfChannels = 2;
  static constexpr uint32_t kTestFifoDepth = 16;
  static constexpr uint32_t kTestClockDomain = audio_fidl::CLOCK_DOMAIN_EXTERNAL;
  static constexpr uint32_t kTestPositionNotify = 4;
  static constexpr float kTestGain = 1.2345f;

  MockSimpleAudio(zx_device_t* parent) : SimpleAudioStream(parent, false /* is input */) {}

  void PostSetPlugState(bool plugged, zx::duration delay) {
    async::PostDelayedTask(
        dispatcher(),
        [this, plugged]() {
          ScopedToken t(domain_token());
          SetPlugState(plugged);
        },
        delay);
  }

 protected:
  zx_status_t Init() __TA_REQUIRES(domain_token()) override {
    fbl::AllocChecker ac;
    supported_formats_.reserve(1, &ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    audio_stream_format_range_t range = {};
    range.min_channels = kTestNumberOfChannels;
    range.max_channels = kTestNumberOfChannels;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_frames_per_second = kTestFrameRate;
    range.max_frames_per_second = kTestFrameRate;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

    supported_formats_.push_back(range);

    fifo_depth_ = kTestFifoDepth;
    clock_domain_ = kTestClockDomain;

    // Set our gain capabilities.
    cur_gain_state_.cur_gain = 0;
    cur_gain_state_.cur_mute = false;
    cur_gain_state_.cur_agc = false;
    cur_gain_state_.min_gain = 0;
    cur_gain_state_.max_gain = 100;
    cur_gain_state_.gain_step = 0;
    cur_gain_state_.can_mute = true;
    cur_gain_state_.can_agc = true;

    SetInitialPlugState(AUDIO_PDNF_CAN_NOTIFY);

    snprintf(device_name_, sizeof(device_name_), "test-audio-in");
    snprintf(mfr_name_, sizeof(mfr_name_), "Bike Sheds, Inc.");
    snprintf(prod_name_, sizeof(prod_name_), "testy_mctestface");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;

    return ZX_OK;
  }

  zx_status_t SetGain(const audio_proto::SetGainReq& req) __TA_REQUIRES(domain_token()) override {
    if (req.flags & AUDIO_SGF_GAIN_VALID) {
      cur_gain_state_.cur_gain = req.gain;
    }
    if (req.flags & AUDIO_SGF_AGC_VALID) {
      cur_gain_state_.cur_agc = req.flags & AUDIO_SGF_AGC;
    }
    if (req.flags & AUDIO_SGF_MUTE_VALID) {
      cur_gain_state_.cur_mute = req.flags & AUDIO_SGF_MUTE;
    }
    return ZX_OK;
  }

  zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
      __TA_REQUIRES(domain_token()) override {
    return ZX_OK;
  }

  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) __TA_REQUIRES(domain_token()) override {
    zx::vmo rb;
    *out_num_rb_frames = req.min_ring_buffer_frames;
    zx::vmo::create(*out_num_rb_frames * 2 * 2, 0, &rb);
    us_per_notification_ = 1'000 * MockSimpleAudio::kTestFrameRate / *out_num_rb_frames * 1'000 /
                           req.notifications_per_ring;
    constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
    return rb.duplicate(rights, out_buffer);
  }

  zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_token()) override {
    *out_start_time = zx::clock::get_monotonic().get();
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
    return ZX_OK;
  }

  zx_status_t Stop() __TA_REQUIRES(domain_token()) override {
    notify_timer_.Cancel();
    return ZX_OK;
  }

  void ProcessRingNotification() {
    ScopedToken t(domain_token());
    audio_proto::RingBufPositionNotify resp = {};
    resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;
    resp.monotonic_time = zx::clock::get_monotonic().get();
    resp.ring_buffer_pos = kTestPositionNotify;
    NotifyPosition(resp);
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  }

  void ShutdownHook() __TA_REQUIRES(domain_token()) override { Stop(); }

 private:
  async::TaskClosureMethod<MockSimpleAudio, &MockSimpleAudio::ProcessRingNotification> notify_timer_
      TA_GUARDED(domain_token()){this};

  uint32_t us_per_notification_ = 0;
};

TEST(SimpleAudioTest, DdkLifeCycleTest) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);
  ddk::SuspendTxn txn(server->zxdev(), 0, false, DEVICE_SUSPEND_REASON_SELECTIVE_SUSPEND);
  server->DdkSuspend(std::move(txn));
  EXPECT_FALSE(tester.remove_called());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, UnbindAndAlsoShutdown) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);
  server->DdkAsyncRemove();
  server->Shutdown();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, SetAndGetGain) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto builder = audio_fidl::GainState::UnownedBuilder();
  fidl::aligned<float> target_gain = MockSimpleAudio::kTestGain;
  builder.set_gain_db(fidl::unowned_ptr(&target_gain));
  auto status =
      audio_fidl::StreamConfig::Call::SetGain(zx::unowned_channel(ch->channel), builder.build());
  ASSERT_OK(status.status());
  auto gain_state =
      audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch->channel));
  ASSERT_OK(gain_state.status());
  ASSERT_EQ(MockSimpleAudio::kTestGain, gain_state->gain_state.gain_db());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, WatchGainAndCloseStreamBeforeReply) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto builder = audio_fidl::GainState::UnownedBuilder();
  fidl::aligned<float> target_gain = MockSimpleAudio::kTestGain;
  builder.set_gain_db(fidl::unowned_ptr(&target_gain));
  auto status =
      audio_fidl::StreamConfig::Call::SetGain(zx::unowned_channel(ch->channel), builder.build());
  ASSERT_OK(status.status());

  // One watch for initial reply.
  auto gain_state =
      audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch->channel));
  ASSERT_OK(gain_state.status());
  ASSERT_EQ(MockSimpleAudio::kTestGain, gain_state->gain_state.gain_db());

  // A second watch with no reply since there is no change of gain.
  auto f = [](void* arg) -> int {
    auto ch = static_cast<audio_fidl::Device::ResultOf::GetChannel*>(arg);
    audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel((*ch)->channel));
    return 0;
  };
  thrd_t th;
  ASSERT_OK(thrd_create_with_name(&th, f, &ch, "test-thread"));

  // We want the watch to be started before we reset the channel triggering a deactivation.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  ch->channel.reset();

  int result = -1;
  thrd_join(th, &result);
  ASSERT_EQ(result, 0);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, SetAndGetAgc) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto builder = audio_fidl::GainState::UnownedBuilder();

  fidl::aligned<bool> target_agc1 = true;
  builder.set_agc_enabled(fidl::unowned_ptr(&target_agc1));
  auto status1 =
      audio_fidl::StreamConfig::Call::SetGain(zx::unowned_channel(ch->channel), builder.build());
  ASSERT_OK(status1.status());
  auto gain_state1 =
      audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch->channel));
  ASSERT_OK(gain_state1.status());
  ASSERT_TRUE(gain_state1->gain_state.agc_enabled());

  fidl::aligned<bool> target_agc2 = false;
  builder.set_agc_enabled(fidl::unowned_ptr(&target_agc2));
  auto status2 =
      audio_fidl::StreamConfig::Call::SetGain(zx::unowned_channel(ch->channel), builder.build());
  ASSERT_OK(status2.status());
  auto gain_state2 =
      audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch->channel));
  ASSERT_OK(gain_state2.status());
  ASSERT_FALSE(gain_state2->gain_state.agc_enabled());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, SetAndGetMute) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto builder = audio_fidl::GainState::UnownedBuilder();

  fidl::aligned<bool> muted1 = true;
  builder.set_muted(fidl::unowned_ptr(&muted1));
  auto status1 =
      audio_fidl::StreamConfig::Call::SetGain(zx::unowned_channel(ch->channel), builder.build());
  ASSERT_OK(status1.status());
  auto gain_state1 =
      audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch->channel));
  ASSERT_OK(gain_state1.status());
  ASSERT_TRUE(gain_state1->gain_state.muted());

  fidl::aligned<bool> muted2 = false;
  builder.set_muted(fidl::unowned_ptr(&muted2));
  auto status2 =
      audio_fidl::StreamConfig::Call::SetGain(zx::unowned_channel(ch->channel), builder.build());
  ASSERT_OK(status2.status());
  auto gain_state2 =
      audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch->channel));
  ASSERT_OK(gain_state2.status());
  ASSERT_FALSE(gain_state2->gain_state.muted());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, SetMuteWhenDisabled) {
  struct MockSimpleAudioLocal : public MockSimpleAudio {
    MockSimpleAudioLocal(zx_device_t* parent) : MockSimpleAudio(parent) {}
    zx_status_t Init() __TA_REQUIRES(domain_token()) override {
      auto status = MockSimpleAudio::Init();
      cur_gain_state_.can_mute = false;
      return status;
    }
  };
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudioLocal>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto builder = audio_fidl::GainState::UnownedBuilder();

  fidl::aligned<bool> muted1 = true;
  builder.set_muted(fidl::unowned_ptr(&muted1));
  auto status1 =
      audio_fidl::StreamConfig::Call::SetGain(zx::unowned_channel(ch->channel), builder.build());
  ASSERT_OK(status1.status());
  auto gain_state1 =
      audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch->channel));
  ASSERT_OK(gain_state1.status());
  ASSERT_FALSE(gain_state1->gain_state.has_muted());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, Enumerate1) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto channel = zx::unowned_channel(ch->channel);
  audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));

  auto ret = client.GetSupportedFormats();
  auto& supported_formats = ret->supported_formats;
  auto& formats = supported_formats[0].pcm_supported_formats();
  ASSERT_EQ(1, formats.number_of_channels.count());
  ASSERT_EQ(1, formats.sample_formats.count());
  ASSERT_EQ(audio_fidl::SampleFormat::PCM_SIGNED, formats.sample_formats[0]);
  ASSERT_EQ(1, formats.frame_rates.count());
  ASSERT_EQ(48000, formats.frame_rates[0]);
  ASSERT_EQ(1, formats.bytes_per_sample.count());
  ASSERT_EQ(2, formats.bytes_per_sample[0]);
  ASSERT_EQ(1, formats.valid_bits_per_sample.count());
  ASSERT_EQ(16, formats.valid_bits_per_sample[0]);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, Enumerate2) {
  struct MockSimpleAudioLocal : public MockSimpleAudio {
    MockSimpleAudioLocal(zx_device_t* parent) : MockSimpleAudio(parent) {}
    zx_status_t Init() __TA_REQUIRES(domain_token()) override {
      auto status = MockSimpleAudio::Init();

      audio_stream_format_range_t range1 = {};
      audio_stream_format_range_t range2 = {};

      range1.min_channels = 2;
      range1.max_channels = 4;
      range1.sample_formats = AUDIO_SAMPLE_FORMAT_24BIT_IN32;
      range1.min_frames_per_second = 48'000;
      range1.max_frames_per_second = 768'000;
      range1.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

      range2.min_channels = 1;
      range2.max_channels = 1;
      range2.sample_formats = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
      range2.min_frames_per_second = 88'200;
      range2.max_frames_per_second = 88'200;
      range2.flags = ASF_RANGE_FLAG_FPS_CONTINUOUS;  // Ok only because min and max fps are equal.

      supported_formats_ = fbl::Vector<audio_stream_format_range_t>{range1, range2};
      return status;
    }
  };

  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudioLocal>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto channel = zx::unowned_channel(ch->channel);
  audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));

  auto ret = client.GetSupportedFormats();
  auto& supported_formats = ret->supported_formats;
  ASSERT_EQ(2, supported_formats.count());

  auto& formats1 = supported_formats[0].pcm_supported_formats();
  ASSERT_EQ(3, formats1.number_of_channels.count());
  ASSERT_EQ(2, formats1.number_of_channels[0]);
  ASSERT_EQ(3, formats1.number_of_channels[1]);
  ASSERT_EQ(4, formats1.number_of_channels[2]);
  ASSERT_EQ(1, formats1.sample_formats.count());
  ASSERT_EQ(audio_fidl::SampleFormat::PCM_SIGNED, formats1.sample_formats[0]);
  ASSERT_EQ(5, formats1.frame_rates.count());
  std::set<uint32_t> rates1;
  for (auto& i : formats1.frame_rates) {
    rates1.insert(i);
  }
  ASSERT_EQ(rates1, std::set<uint32_t>({48'000, 96'000, 192'000, 384'000, 768'000}));
  ASSERT_EQ(1, formats1.bytes_per_sample.count());
  ASSERT_EQ(4, formats1.bytes_per_sample[0]);
  ASSERT_EQ(1, formats1.valid_bits_per_sample.count());
  ASSERT_EQ(24, formats1.valid_bits_per_sample[0]);

  auto& formats2 = supported_formats[1].pcm_supported_formats();
  ASSERT_EQ(1, formats2.number_of_channels.count());
  ASSERT_EQ(1, formats2.number_of_channels[0]);
  ASSERT_EQ(1, formats2.sample_formats.count());
  ASSERT_EQ(audio_fidl::SampleFormat::PCM_FLOAT, formats2.sample_formats[0]);
  ASSERT_EQ(1, formats2.frame_rates.count());
  std::set<uint32_t> rates2;
  for (auto& i : formats2.frame_rates) {
    rates2.insert(i);
  }
  ASSERT_EQ(rates2, std::set<uint32_t>({88'200}));
  ASSERT_EQ(1, formats2.bytes_per_sample.count());
  ASSERT_EQ(4, formats2.bytes_per_sample[0]);
  ASSERT_EQ(1, formats2.valid_bits_per_sample.count());
  ASSERT_EQ(32, formats2.valid_bits_per_sample[0]);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, CreateRingBuffer1) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto channel = zx::unowned_channel(ch->channel);
  audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  fidl::aligned<audio_fidl::PcmFormat> pcm_format = GetDefaultPcmFormat();
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));
  client.CreateRingBuffer(builder.build(), std::move(remote));

  auto result = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
  ASSERT_OK(result.status());
  ASSERT_EQ(result->properties.fifo_depth(), MockSimpleAudio::kTestFifoDepth);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, CreateRingBuffer2) {
  struct MockSimpleAudioLocal : public MockSimpleAudio {
    MockSimpleAudioLocal(zx_device_t* parent) : MockSimpleAudio(parent) {}
    zx_status_t Init() __TA_REQUIRES(domain_token()) override {
      audio_stream_format_range_t range = {};
      range.min_channels = 1;
      range.max_channels = 4;
      range.sample_formats = AUDIO_SAMPLE_FORMAT_24BIT_IN32 | AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED;
      range.min_frames_per_second = 22050;
      range.max_frames_per_second = 88200;
      range.flags = ASF_RANGE_FLAG_FPS_44100_FAMILY;
      supported_formats_.push_back(range);
      return MockSimpleAudio::Init();
    }
  };
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudioLocal>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto channel = zx::unowned_channel(ch->channel);
  audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  audio_fidl::PcmFormat pcm_format = {};
  pcm_format.number_of_channels = 4;
  pcm_format.channels_to_use_bitmask = 0x0f;
  pcm_format.sample_format = audio_fidl::SampleFormat::PCM_UNSIGNED;
  pcm_format.frame_rate = 44100;
  pcm_format.bytes_per_sample = 4;
  pcm_format.valid_bits_per_sample = 24;
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));
  client.CreateRingBuffer(builder.build(), std::move(remote));

  auto result = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
  ASSERT_OK(result.status());
  ASSERT_EQ(result->properties.fifo_depth(), MockSimpleAudio::kTestFifoDepth);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, SetBadFormat1) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto channel = zx::unowned_channel(ch->channel);
  audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Define a pretty bad format.
  audio_fidl::PcmFormat pcm_format = {};
  pcm_format.sample_format = audio_fidl::SampleFormat::PCM_SIGNED;
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));

  auto result0 = client.CreateRingBuffer(builder.build(), std::move(remote));
  ASSERT_EQ(ZX_OK, result0.status());  // CreateRingBuffer is sent successfully.

  auto result1 = client.GetSupportedFormats();
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, result1.status());  // With a bad format we get a channel close.

  auto result2 = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, result2.status());  // With a bad format we get a channel close.
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, SetBadFormat2) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto channel = zx::unowned_channel(ch->channel);
  audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Define an almost good format.
  audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
  pcm_format.frame_rate = 48001;  // Bad rate.
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));

  auto result0 = client.CreateRingBuffer(builder.build(), std::move(remote));
  ASSERT_EQ(ZX_OK, result0.status());  // CreateRingBuffer is sent successfully.

  auto result1 = client.GetSupportedFormats();
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, result1.status());  // With a bad format we get a channel close.

  auto result2 = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, result2.status());  // With a bad format we get a channel close.
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, GetIds) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto result = audio_fidl::StreamConfig::Call::GetProperties(zx::unowned_channel(ch->channel));
  ASSERT_OK(result.status());

  audio_stream_unique_id_t mic = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;
  ASSERT_BYTES_EQ(result->properties.unique_id().data(), mic.data,
                  strlen(reinterpret_cast<char*>(mic.data)));
  ASSERT_BYTES_EQ(result->properties.manufacturer().data(), "Bike Sheds, Inc.",
                  strlen("Bike Sheds, Inc."));
  ASSERT_EQ(result->properties.clock_domain(), MockSimpleAudio::kTestClockDomain);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, MultipleChannelsPlugDetectState) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  // We get 2 channels from the one FIDL channel acquired via FidlClient() using GetChannel.

  audio_fidl::Device::ResultOf::GetChannel ch1 = client.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch2 = client.GetChannel();
  ASSERT_EQ(ch1.status(), ZX_OK);
  ASSERT_EQ(ch2.status(), ZX_OK);

  auto prop1 = audio_fidl::StreamConfig::Call::GetProperties(zx::unowned_channel(ch1->channel));
  auto prop2 = audio_fidl::StreamConfig::Call::GetProperties(zx::unowned_channel(ch2->channel));
  ASSERT_OK(prop1.status());
  ASSERT_OK(prop2.status());

  ASSERT_EQ(prop1->properties.plug_detect_capabilities(),
            audio_fidl::PlugDetectCapabilities::CAN_ASYNC_NOTIFY);
  ASSERT_EQ(prop2->properties.plug_detect_capabilities(),
            audio_fidl::PlugDetectCapabilities::CAN_ASYNC_NOTIFY);

  auto state1 = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch1->channel));
  auto state2 = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch2->channel));
  ASSERT_OK(state1.status());
  ASSERT_OK(state2.status());
  ASSERT_FALSE(state1->plug_state.plugged());
  ASSERT_FALSE(state2->plug_state.plugged());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, WatchPlugDetectAndCloseStreamBeforeReply) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  // We get 2 channels from the one FIDL channel acquired via FidlClient() using GetChannel.
  audio_fidl::Device::ResultOf::GetChannel ch1 = client.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch2 = client.GetChannel();
  ASSERT_EQ(ch1.status(), ZX_OK);
  ASSERT_EQ(ch2.status(), ZX_OK);

  auto prop1 = audio_fidl::StreamConfig::Call::GetProperties(zx::unowned_channel(ch1->channel));
  auto prop2 = audio_fidl::StreamConfig::Call::GetProperties(zx::unowned_channel(ch2->channel));
  ASSERT_OK(prop1.status());
  ASSERT_OK(prop2.status());

  ASSERT_EQ(prop1->properties.plug_detect_capabilities(),
            audio_fidl::PlugDetectCapabilities::CAN_ASYNC_NOTIFY);
  ASSERT_EQ(prop2->properties.plug_detect_capabilities(),
            audio_fidl::PlugDetectCapabilities::CAN_ASYNC_NOTIFY);

  // Watch each channel for initial reply.
  auto state1 = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch1->channel));
  auto state2 = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch2->channel));
  ASSERT_OK(state1.status());
  ASSERT_OK(state2.status());
  ASSERT_FALSE(state1->plug_state.plugged());
  ASSERT_FALSE(state2->plug_state.plugged());

  // Secondary watches with no reply since there is no change of plug detect state.
  auto f = [](void* arg) -> int {
    audio_fidl::Device::ResultOf::GetChannel* ch =
        static_cast<audio_fidl::Device::ResultOf::GetChannel*>(arg);
    audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel((*ch)->channel));
    return 0;
  };
  thrd_t th1;
  ASSERT_OK(thrd_create_with_name(&th1, f, &ch1, "test-thread-1"));
  thrd_t th2;
  ASSERT_OK(thrd_create_with_name(&th2, f, &ch2, "test-thread-2"));

  // We want the watches to be started before we reset the channels triggering deactivations.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  ch1->channel.reset();
  ch2->channel.reset();

  int result = -1;
  thrd_join(th1, &result);
  ASSERT_EQ(result, 0);
  result = -1;
  thrd_join(th2, &result);
  ASSERT_EQ(result, 0);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, MultipleChannelsPlugDetectNotify) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  // We get 2 channels from the one FIDL channel acquired via FidlClient() using GetChannel.
  audio_fidl::Device::ResultOf::GetChannel ch1 = client.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch2 = client.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch3 = client.GetChannel();
  ASSERT_EQ(ch1.status(), ZX_OK);
  ASSERT_EQ(ch2.status(), ZX_OK);
  ASSERT_EQ(ch3.status(), ZX_OK);

  auto state1a = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch1->channel));
  auto state2a = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch2->channel));
  auto state3a = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch3->channel));
  ASSERT_OK(state1a.status());
  ASSERT_OK(state2a.status());
  ASSERT_OK(state3a.status());
  ASSERT_FALSE(state1a->plug_state.plugged());
  ASSERT_FALSE(state2a->plug_state.plugged());
  ASSERT_FALSE(state3a->plug_state.plugged());

  server->PostSetPlugState(true, zx::duration(zx::msec(100)));

  auto state1b = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch1->channel));
  auto state2b = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch2->channel));
  auto state3b = audio_fidl::StreamConfig::Call::WatchPlugState(zx::unowned_channel(ch3->channel));
  ASSERT_OK(state1b.status());
  ASSERT_OK(state2b.status());
  ASSERT_OK(state3b.status());
  ASSERT_TRUE(state1b->plug_state.plugged());
  ASSERT_TRUE(state2b->plug_state.plugged());
  ASSERT_TRUE(state3b->plug_state.plugged());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, MultipleChannelsGainState) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  // We get 2 channels from the one FIDL channel acquired via FidlClient() using GetChannel.
  audio_fidl::Device::ResultOf::GetChannel ch1 = client.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch2 = client.GetChannel();
  ASSERT_EQ(ch1.status(), ZX_OK);
  ASSERT_EQ(ch2.status(), ZX_OK);

  auto state1 = audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch1->channel));
  auto state2 = audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch2->channel));
  ASSERT_OK(state1.status());
  ASSERT_OK(state2.status());
  ASSERT_EQ(0.f, state1->gain_state.gain_db());
  ASSERT_EQ(0.f, state2->gain_state.gain_db());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, MultipleChannelsGainStateNotify) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  // We get 2 channels from the one FIDL channel acquired via FidlClient() using GetChannel.
  audio_fidl::Device::ResultOf::GetChannel ch1 = client.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch2 = client.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch3 = client.GetChannel();
  ASSERT_EQ(ch1.status(), ZX_OK);
  ASSERT_EQ(ch2.status(), ZX_OK);
  ASSERT_EQ(ch3.status(), ZX_OK);

  auto state1a = audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch1->channel));
  auto state2a = audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch2->channel));
  auto state3a = audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch3->channel));
  ASSERT_OK(state1a.status());
  ASSERT_OK(state2a.status());
  ASSERT_OK(state3a.status());
  ASSERT_EQ(0.f, state1a->gain_state.gain_db());
  ASSERT_EQ(0.f, state2a->gain_state.gain_db());
  ASSERT_EQ(0.f, state3a->gain_state.gain_db());

  auto f = [](void* arg) -> int {
    zx::nanosleep(zx::deadline_after(zx::msec(100)));
    audio_fidl::Device::ResultOf::GetChannel* ch1 =
        static_cast<audio_fidl::Device::ResultOf::GetChannel*>(arg);
    auto builder = audio_fidl::GainState::UnownedBuilder();
    fidl::aligned<bool> muted = false;
    fidl::aligned<bool> agc = false;
    builder.set_muted(fidl::unowned_ptr(&muted));
    builder.set_agc_enabled(fidl::unowned_ptr(&agc));
    fidl::aligned<float> target_gain = MockSimpleAudio::kTestGain;
    builder.set_gain_db(fidl::unowned_ptr(&target_gain));

    audio_fidl::StreamConfig::Call::SetGain(zx::unowned_channel((*ch1)->channel), builder.build());
    return 0;
  };
  thrd_t th;
  ASSERT_OK(thrd_create_with_name(&th, f, &ch1, "test-thread"));

  auto state1b = audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch1->channel));
  auto state2b = audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch2->channel));
  auto state3b = audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(ch3->channel));
  ASSERT_OK(state1b.status());
  ASSERT_OK(state2b.status());
  ASSERT_OK(state3b.status());
  ASSERT_EQ(MockSimpleAudio::kTestGain, state1b->gain_state.gain_db());
  ASSERT_EQ(MockSimpleAudio::kTestGain, state2b->gain_state.gain_db());
  ASSERT_EQ(MockSimpleAudio::kTestGain, state3b->gain_state.gain_db());

  int result = -1;
  thrd_join(th, &result);
  ASSERT_EQ(result, 0);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, RingBufferTests) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  fidl::aligned<audio_fidl::PcmFormat> pcm_format = GetDefaultPcmFormat();
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));
  auto rb = audio_fidl::StreamConfig::Call::CreateRingBuffer(zx::unowned_channel(ch->channel),
                                                             builder.build(), std::move(remote));
  ASSERT_OK(rb.status());

  constexpr uint32_t kNumberOfPositionNotifications = 5;
  // Buffer is set to hold at least 1 second, with kNumberOfPositionNotifications notifications
  // per ring buffer (i.e. per second) we set the time waiting for the watch below to 200ms+.

  auto vmo = audio_fidl::RingBuffer::Call::GetVmo(
      zx::unowned_channel(local), MockSimpleAudio::kTestFrameRate, kNumberOfPositionNotifications);
  ASSERT_OK(vmo.status());

  auto start = audio_fidl::RingBuffer::Call::Start(zx::unowned_channel(local));
  ASSERT_OK(start.status());

  auto position =
      audio_fidl::RingBuffer::Call::WatchClockRecoveryPositionInfo(zx::unowned_channel(local));
  ASSERT_EQ(MockSimpleAudio::kTestPositionNotify, position->position_info.position);

  auto stop = audio_fidl::RingBuffer::Call::Stop(zx::unowned_channel(local));
  ASSERT_OK(stop.status());
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, WatchPositionAndCloseRingBufferBeforeReply) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  fidl::aligned<audio_fidl::PcmFormat> pcm_format = GetDefaultPcmFormat();
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));
  auto rb = audio_fidl::StreamConfig::Call::CreateRingBuffer(zx::unowned_channel(ch->channel),
                                                             builder.build(), std::move(remote));
  ASSERT_OK(rb.status());

  constexpr uint32_t kNumberOfPositionNotifications = 5;
  // Buffer is set to hold at least 1 second, with kNumberOfPositionNotifications notifications
  // per ring buffer (i.e. per second) the time waiting before getting a position reply is 200ms+.

  auto vmo = audio_fidl::RingBuffer::Call::GetVmo(
      zx::unowned_channel(local), MockSimpleAudio::kTestFrameRate, kNumberOfPositionNotifications);
  ASSERT_OK(vmo.status());

  auto start = audio_fidl::RingBuffer::Call::Start(zx::unowned_channel(local));
  ASSERT_OK(start.status());

  // Watch position notifications.
  auto f = [](void* arg) -> int {
    auto ch = static_cast<zx::channel*>(arg);
    audio_fidl::RingBuffer::Call::WatchClockRecoveryPositionInfo(zx::unowned_channel(*ch));
    return 0;
  };
  thrd_t th;
  ASSERT_OK(thrd_create_with_name(&th, f, &local, "test-thread"));

  // We want the watch to be started before we reset the channel triggering a deactivation.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  local.reset();
  ch->channel.reset();

  int result = -1;
  thrd_join(th, &result);
  ASSERT_EQ(result, 0);
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, ClientCloseStreamConfigProtocol) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  ch->channel.reset();
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, ClientCloseRingBufferProtocol) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto channel = zx::unowned_channel(ch->channel);
  audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  fidl::aligned<audio_fidl::PcmFormat> pcm_format = GetDefaultPcmFormat();
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));
  auto ret = client.CreateRingBuffer(builder.build(), std::move(remote));
  ASSERT_OK(ret.status());

  local.reset();

  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, ClientCloseStreamConfigProtocolWithARingBufferProtocol) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto channel = zx::unowned_channel(ch->channel);
  audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  fidl::aligned<audio_fidl::PcmFormat> pcm_format = GetDefaultPcmFormat();
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));
  auto ret = client.CreateRingBuffer(builder.build(), std::move(remote));
  ASSERT_OK(ret.status());

  client.mutable_channel()->reset();

  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SimpleAudioTest, NonPriviledged) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel ch1 = client_wrap.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch2 = client_wrap.GetChannel();
  audio_fidl::Device::ResultOf::GetChannel ch3 = client_wrap.GetChannel();
  ASSERT_EQ(ch1.status(), ZX_OK);
  ASSERT_EQ(ch2.status(), ZX_OK);
  ASSERT_EQ(ch3.status(), ZX_OK);

  fidl::aligned<audio_fidl::PcmFormat> pcm_format = GetDefaultPcmFormat();
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));

  auto channel1 = zx::unowned_channel(ch1->channel);
  audio_fidl::StreamConfig::SyncClient client1(std::move(ch1->channel));
  zx::channel local1, remote1;
  ASSERT_OK(zx::channel::create(0, &local1, &remote1));
  auto ret1 = client1.CreateRingBuffer(builder.build(), std::move(remote1));
  ASSERT_OK(ret1.status());
  audio_fidl::RingBuffer::SyncClient ringbuffer1(std::move(local1));
  auto stop1 = ringbuffer1.Stop();
  ASSERT_OK(stop1.status());  // Priviledged channel.

  auto channel2 = zx::unowned_channel(ch2->channel);
  audio_fidl::StreamConfig::SyncClient client2(std::move(ch2->channel));
  zx::channel local2, remote2;
  ASSERT_OK(zx::channel::create(0, &local2, &remote2));
  auto ret2 = client2.CreateRingBuffer(builder.build(), std::move(remote2));
  ASSERT_OK(ret2.status());
  audio_fidl::RingBuffer::SyncClient ringbuffer2(std::move(local2));
  auto stop2 = ringbuffer2.Stop();
  ASSERT_NOT_OK(stop2.status());  // Non-priviledged channel.

  auto channel3 = zx::unowned_channel(ch3->channel);
  audio_fidl::StreamConfig::SyncClient client3(std::move(ch3->channel));
  zx::channel local3, remote3;
  ASSERT_OK(zx::channel::create(0, &local3, &remote3));
  auto ret3 = client3.CreateRingBuffer(builder.build(), std::move(remote3));
  ASSERT_OK(ret3.status());
  audio_fidl::RingBuffer::SyncClient ringbuffer3(std::move(local3));
  auto stop3 = ringbuffer3.Stop();
  ASSERT_NOT_OK(stop3.status());  // Non-priviledged channel.

  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

}  // namespace audio
