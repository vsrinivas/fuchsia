// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/clock.h>
#include <threads.h>

#include <set>

#include <audio-proto-utils/format-utils.h>
#include <audio-utils/audio-output.h>
#include <zxtest/zxtest.h>

namespace audio {

using ::llcpp::fuchsia::hardware::audio::Device;

class MockSimpleAudio : public SimpleAudioStream {
 public:
  static constexpr uint32_t kTestFrameRate = 48000;
  static constexpr uint8_t kTestNumberOfChannels = 2;
  static constexpr uint32_t kTestFifoDepth = 16;
  static constexpr uint32_t kTestPositionNotify = 4;

  MockSimpleAudio(zx_device_t* parent) : SimpleAudioStream(parent, false /* is input */) {}

  zx_status_t PostSetPlugState(bool plugged) {
    async::PostTask(dispatcher(), [this, plugged]() {
      ScopedToken t(domain_token());
      SimpleAudioStream::SetPlugState(plugged);
    });
    return ZX_OK;
  }

 protected:
  zx_status_t Init() __TA_REQUIRES(domain_token()) override {
    fbl::AllocChecker ac;
    supported_formats_.reserve(1, &ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    audio_stream_format_range_t range;

    range.min_channels = kTestNumberOfChannels;
    range.max_channels = kTestNumberOfChannels;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_frames_per_second = kTestFrameRate;
    range.max_frames_per_second = kTestFrameRate;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

    supported_formats_.push_back(range);

    fifo_depth_ = kTestFifoDepth;

    // Set our gain capabilities.
    cur_gain_state_.cur_gain = 0;
    cur_gain_state_.cur_mute = false;
    cur_gain_state_.cur_agc = false;
    cur_gain_state_.min_gain = 0;
    cur_gain_state_.max_gain = 100;
    cur_gain_state_.gain_step = 0;
    cur_gain_state_.can_mute = false;
    cur_gain_state_.can_agc = false;

    SetInitialPlugState(AUDIO_PDNF_CAN_NOTIFY);

    snprintf(device_name_, sizeof(device_name_), "test-audio-in");
    snprintf(mfr_name_, sizeof(mfr_name_), "Bike Sheds, Inc.");
    snprintf(prod_name_, sizeof(prod_name_), "testy_mctestface");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;

    return ZX_OK;
  }

  zx_status_t SetGain(const audio_proto::SetGainReq& req) __TA_REQUIRES(domain_token()) override {
    cur_gain_state_.cur_gain = req.gain;
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

  void ProcessRingNotification() __TA_REQUIRES(domain_token()) {
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

class Bind;
class Bind : public fake_ddk::Bind {
 public:
  int total_children() const { return total_children_; }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (parent == fake_ddk::kFakeParent) {
      *out = fake_ddk::kFakeDevice;
      add_called_ = true;
    } else if (parent == fake_ddk::kFakeDevice) {
      *out = kFakeChild;
      children_++;
      total_children_++;
    } else {
      *out = kUnknownDevice;
      bad_parent_ = false;
    }
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) override {
    if (device == fake_ddk::kFakeDevice) {
      remove_called_ = true;
    } else if (device == kFakeChild) {
      // Check that all children are removed before the parent is removed.
      if (!remove_called_) {
        children_--;
      }
    } else {
      bad_device_ = true;
    }
    return ZX_OK;
  }

  bool IsRemoved() { return remove_called_; }

  bool Ok() {
    return ((children_ == 0) && add_called_ && remove_called_ && !bad_parent_ && !bad_device_);
  }

 private:
  zx_device_t* kFakeChild = reinterpret_cast<zx_device_t*>(0x1234);
  zx_device_t* kUnknownDevice = reinterpret_cast<zx_device_t*>(0x5678);

  int total_children_ = 0;
  int children_ = 0;

  bool bad_parent_ = false;
  bool bad_device_ = false;
  bool add_called_ = false;
  bool remove_called_ = false;
};

TEST(SimpleAudioTest, DdkLifeCycleTest) {
  Bind tester;
  auto server = audio::SimpleAudioStream::Create<audio::MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);
  ASSERT_EQ(ZX_OK, server->DdkSuspend(0));
  EXPECT_FALSE(tester.IsRemoved());
  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
}

TEST(SimpleAudioTest, SetAndGetGain) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<audio::MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  Device::SyncClient client(std::move(tester.FidlClient()));
  Device::ResultOf::GetChannel channel_wrap = client.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  // After we get the channel we use audio::utils serialization until we convert to FIDL.
  auto channel_client = audio::utils::AudioOutput::Create(1);
  channel_client->SetStreamChannel(std::move(channel_wrap->ch));

  auto gain = 1.2345f;
  channel_client->SetGain(gain);

  audio_stream_cmd_get_gain_resp gain_state;
  channel_client->GetGain(&gain_state);
  ASSERT_EQ(gain_state.cur_gain, gain);
}

TEST(SimpleAudioTest, EnumerateMultipleRates) {
  struct EnumerateRates : public MockSimpleAudio {
    EnumerateRates(zx_device_t* parent) : MockSimpleAudio(parent) {}
    zx_status_t Init() __TA_REQUIRES(domain_token()) override {
      auto status = MockSimpleAudio::Init();

      audio_stream_format_range_t range;

      range.min_channels = kTestNumberOfChannels;
      range.max_channels = kTestNumberOfChannels;
      range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
      range.min_frames_per_second = 48000;
      range.max_frames_per_second = 768000;
      range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

      supported_formats_ = fbl::Vector<audio_stream_format_range_t>{range};
      return status;
    }
  };
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<EnumerateRates>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  Device::SyncClient client(std::move(tester.FidlClient()));
  Device::ResultOf::GetChannel channel_wrap = client.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  // After we get the channel we use audio::utils serialization until we convert to FIDL.
  auto channel_client = audio::utils::AudioOutput::Create(1);
  channel_client->SetStreamChannel(std::move(channel_wrap->ch));

  fbl::Vector<audio_stream_format_range_t> ranges;
  channel_client->GetSupportedFormats(&ranges);
  ASSERT_EQ(1, ranges.size());
  ASSERT_EQ(MockSimpleAudio::kTestNumberOfChannels, ranges[0].min_channels);
  ASSERT_EQ(MockSimpleAudio::kTestNumberOfChannels, ranges[0].max_channels);
  ASSERT_EQ(AUDIO_SAMPLE_FORMAT_16BIT, ranges[0].sample_formats);
  ASSERT_EQ(48000, ranges[0].min_frames_per_second);
  ASSERT_EQ(768000, ranges[0].max_frames_per_second);
  ASSERT_EQ(ASF_RANGE_FLAG_FPS_48000_FAMILY, ranges[0].flags);

  audio::utils::FrameRateEnumerator enumerator(ranges[0]);
  std::set<uint32_t> rates;
  for (uint32_t rate : enumerator) {
    rates.insert(rate);
  }
  ASSERT_EQ(5, rates.size());
  ASSERT_EQ(rates, std::set<uint32_t>({48'000, 96'000, 192'000, 384'000, 768'000}));
}

TEST(SimpleAudioTest, GetIds) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<audio::MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  Device::SyncClient client(std::move(tester.FidlClient()));
  Device::ResultOf::GetChannel channel_wrap = client.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  // After we get the channel we use audio::utils serialization until we convert to FIDL.
  auto channel_client = audio::utils::AudioOutput::Create(1);
  channel_client->SetStreamChannel(std::move(channel_wrap->ch));

  audio_stream_cmd_get_unique_id_resp_t id = {};
  channel_client->GetUniqueId(&id);
  audio_stream_unique_id_t mic = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;
  ASSERT_BYTES_EQ(id.unique_id.data, mic.data, strlen(reinterpret_cast<char*>(mic.data)) + 1);
  audio_stream_cmd_get_string_resp_t str = {};
  channel_client->GetString(AUDIO_STREAM_STR_ID_MANUFACTURER, &str);
  ASSERT_BYTES_EQ(str.str, "Bike Sheds, Inc.", strlen("Bike Sheds, Inc.") + 1);
}

TEST(SimpleAudioTest, MultipleChannelsPlugDetectState) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<audio::MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  Device::SyncClient client(std::move(tester.FidlClient()));
  // We get 2 channels from the one FIDL channel acquired via FidlClient() using GetChannel.
  Device::ResultOf::GetChannel channel_wrap1 = client.GetChannel();
  ASSERT_EQ(channel_wrap1.status(), ZX_OK);

  Device::ResultOf::GetChannel channel_wrap2 = client.GetChannel();
  ASSERT_EQ(channel_wrap2.status(), ZX_OK);

  // After we get the channel we use audio::utils serialization until we convert to FIDL.
  auto channel_client1 = audio::utils::AudioOutput::Create(1);
  auto channel_client2 = audio::utils::AudioOutput::Create(2);
  channel_client1->SetStreamChannel(std::move(channel_wrap1->ch));
  channel_client2->SetStreamChannel(std::move(channel_wrap2->ch));

  audio_stream_cmd_plug_detect_resp resp = {};
  channel_client1->GetPlugState(&resp, false);
  ASSERT_EQ(resp.flags, AUDIO_PDNF_CAN_NOTIFY);
  channel_client2->GetPlugState(&resp, true);
  ASSERT_EQ(resp.flags, AUDIO_PDNF_CAN_NOTIFY);
}

TEST(SimpleAudioTest, MultipleChannelsPlugDetectNotify) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<audio::MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  Device::SyncClient client(std::move(tester.FidlClient()));
  // We get multiple channels from the one FIDL channel acquired via FidlClient() using
  // GetChannel.
  Device::ResultOf::GetChannel channel_wrap1 = client.GetChannel();
  Device::ResultOf::GetChannel channel_wrap2 = client.GetChannel();
  Device::ResultOf::GetChannel channel_wrap3 = client.GetChannel();
  ASSERT_EQ(channel_wrap1.status(), ZX_OK);
  ASSERT_EQ(channel_wrap2.status(), ZX_OK);
  ASSERT_EQ(channel_wrap3.status(), ZX_OK);
  // After we get the channel we use audio::utils serialization until we convert to FIDL.
  auto channel_client1 = audio::utils::AudioOutput::Create(1);
  auto channel_client2 = audio::utils::AudioOutput::Create(1);
  auto channel_client3 = audio::utils::AudioOutput::Create(1);
  channel_client1->SetStreamChannel(std::move(channel_wrap1->ch));
  channel_client2->SetStreamChannel(std::move(channel_wrap2->ch));
  channel_client3->SetStreamChannel(std::move(channel_wrap3->ch));

  // Create threads to wait for notifications on them.
  auto f = [](void* arg) -> int {
    audio::utils::AudioOutput* channel_client = static_cast<audio::utils::AudioOutput*>(arg);
    bool client_notified = false;
    auto cb = [&client_notified](bool plug_state, zx_time_t plug_time) -> bool {
      client_notified = plug_state;
      return false;  // Stop monitoring.
    };
    utils::AudioDeviceStream::PlugMonitorCallback monitor = cb;
    channel_client->PlugMonitor(30, &monitor);
    return client_notified ? 0 : 1;
  };

  audio_stream_cmd_plug_detect_resp_t resp = {};
  // GetPlugState() enables notifications now, so the channel message from SetPlugState is ready
  // when PlugMonitor is run.  GetPlugState is a blocking call.
  channel_client1->GetPlugState(&resp, true);
  channel_client2->GetPlugState(&resp, true);
  channel_client3->GetPlugState(&resp, true);
  server->PostSetPlugState(true);

  thrd_t thread1, thread2, thread3;
  ASSERT_OK(thrd_create_with_name(&thread1, f, channel_client1.get(), "test-thread-1"));
  ASSERT_OK(thrd_create_with_name(&thread2, f, channel_client2.get(), "test-thread-2"));
  ASSERT_OK(thrd_create_with_name(&thread3, f, channel_client3.get(), "test-thread-3"));

  int result = -1;
  thrd_join(thread1, &result);
  ASSERT_EQ(result, 0);
  result = -1;
  thrd_join(thread2, &result);
  ASSERT_EQ(result, 0);
  result = -1;
  thrd_join(thread3, &result);
  ASSERT_EQ(result, 0);
}

TEST(SimpleAudioTest, RingBufferTests) {
  fake_ddk::Bind tester;
  auto server = audio::SimpleAudioStream::Create<audio::MockSimpleAudio>(fake_ddk::kFakeParent);
  ASSERT_NOT_NULL(server);

  Device::SyncClient client(std::move(tester.FidlClient()));
  Device::ResultOf::GetChannel channel_wrap = client.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  // After we get the channel we use audio::utils serialization until we convert to FIDL.
  auto channel_client = audio::utils::AudioOutput::Create(1);
  channel_client->SetStreamChannel(std::move(channel_wrap->ch));

  audio_sample_format_t format = AUDIO_SAMPLE_FORMAT_16BIT;
  ASSERT_OK(channel_client->SetFormat(MockSimpleAudio::kTestFrameRate,
                                      MockSimpleAudio::kTestNumberOfChannels, format));

  constexpr uint32_t kNumberOfPositionNotifications = 5;
  // Buffer is set to hold 1 second, with 10 x kNumberOfPositionNotifications notifications
  // per ring buffer (i.e. per second) we limit the time waiting in the loop below to ~100ms.
  ASSERT_OK(channel_client->GetBuffer(MockSimpleAudio::kTestFrameRate,
                                      kNumberOfPositionNotifications * 10));
  ASSERT_EQ(channel_client->fifo_depth(), MockSimpleAudio::kTestFifoDepth);
  ASSERT_OK(channel_client->StartRingBuffer());

  audio_rb_position_notify_t pos_notif = {};
  uint32_t bytes_read = 0;
  zx_signals_t signals = {};
  for (size_t i = 0; i < kNumberOfPositionNotifications; ++i) {
    ASSERT_OK(channel_client->BorrowRingBufferChannel()->wait_one(ZX_CHANNEL_READABLE,
                                                                  zx::time::infinite(), &signals));
    ASSERT_OK(channel_client->BorrowRingBufferChannel()->read(
        0, &pos_notif, nullptr, sizeof(pos_notif), 0, &bytes_read, nullptr));
    ASSERT_EQ(pos_notif.ring_buffer_pos, MockSimpleAudio::kTestPositionNotify);
  }
  ASSERT_OK(channel_client->StopRingBuffer());
}

}  // namespace audio
