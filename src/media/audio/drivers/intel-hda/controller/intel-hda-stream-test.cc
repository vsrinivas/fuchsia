// Copyright 2021 The Fuchsia Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-hda-stream.h"

#include <fuchsia/hardware/intelhda/codec/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl/llcpp/connect_service.h>

#include <optional>
#include <thread>
#include <vector>

#include <ddktl/device.h>
#include <intel-hda/codec-utils/codec-driver-base.h>
#include <intel-hda/codec-utils/stream-base.h>
#include <zxtest/zxtest.h>

namespace {
namespace audio_fidl = fuchsia_hardware_audio;
const char* kTestString = "testastic";
constexpr int64_t kTestTime = 0x12345;
constexpr float kTestGain = -12.f;
constexpr float kTestGain2 = -15.f;
constexpr float kTestMinGain = -20.f;
constexpr float kTestMaxGain = -10.f;
constexpr float kTestGainStep = 2.f;
}  // namespace

namespace audio::intel_hda {

class TestCodec : public codecs::IntelHDACodecDriverBase {
 public:
  explicit TestCodec() = default;

  zx_status_t ActivateStream(const fbl::RefPtr<codecs::IntelHDAStreamBase>& stream) {
    return codecs::IntelHDACodecDriverBase::ActivateStream(stream);
  }

  Status Bind(zx_device_t* codec_dev, const char* name) {
    return codecs::IntelHDACodecDriverBase::Bind(codec_dev, name);
  }
  void DeviceRelease() { codecs::IntelHDACodecDriverBase::DeviceRelease(); }
};

class TestStream : public codecs::IntelHDAStreamBase {
 public:
  explicit TestStream() : codecs::IntelHDAStreamBase(123, false) {}
  ~TestStream() {}
  zx_status_t Bind() {
    fbl::AutoLock lock(obj_lock());
    return PublishDeviceLocked();
  }

 private:
  fbl::RefPtr<Channel> device_channel_;
};

class FakeController;
using FakeControllerType = ddk::Device<FakeController>;

class FakeController : public FakeControllerType, public ddk::IhdaCodecProtocol<FakeController> {
 public:
  explicit FakeController(zx_device_t* parent)
      : FakeControllerType(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  ~FakeController() { loop_.Shutdown(); }
  zx_device_t* dev() { return reinterpret_cast<zx_device_t*>(this); }
  zx_status_t Bind() { return DdkAdd("fake-controller-device-test"); }
  void DdkRelease() {}

  ihda_codec_protocol_t proto() const {
    ihda_codec_protocol_t proto;
    proto.ctx = const_cast<FakeController*>(this);
    proto.ops = const_cast<ihda_codec_protocol_ops_t*>(&ihda_codec_protocol_ops_);
    return proto;
  }

  // ZX_PROTOCOL_IHDA_CODEC Interface
  zx_status_t IhdaCodecGetDriverChannel(zx::channel* out_channel) {
    zx::channel channel_local;
    zx::channel channel_remote;
    zx_status_t status = zx::channel::create(0, &channel_local, &channel_remote);
    if (status != ZX_OK) {
      return status;
    }

    fbl::RefPtr<Channel> channel = Channel::Create(std::move(channel_local));
    if (channel == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
    codec_driver_channel_ = channel;
    codec_driver_channel_->SetHandler([](async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {});
    status = codec_driver_channel_->BeginWait(loop_.dispatcher());
    if (status != ZX_OK) {
      codec_driver_channel_.reset();
      return status;
    }

    if (status == ZX_OK) {
      *out_channel = std::move(channel_remote);
    }

    return status;
  }

 private:
  async::Loop loop_;
  fbl::RefPtr<Channel> codec_driver_channel_;
};

class Binder : public fake_ddk::Bind {
 public:
 private:
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override {
    auto context = reinterpret_cast<const FakeController*>(device);
    if (proto_id == ZX_PROTOCOL_IHDA_CODEC) {
      *reinterpret_cast<ihda_codec_protocol_t*>(protocol) = context->proto();
      return ZX_OK;
    }
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status = fake_ddk::Bind::DeviceAdd(drv, parent, args, out);
    bad_parent_ = false;
    return status;
  }
};

TEST(HdaStreamTest, GetStreamPropertiesDefaults) {
  Binder tester;
  FakeController fake_controller(fake_ddk::kFakeParent);
  ASSERT_OK(fake_controller.Bind());

  auto codec = fbl::AdoptRef(new TestCodec);
  auto ret = codec->Bind(fake_controller.dev(), "test");
  ASSERT_OK(ret.code());
  auto stream = fbl::AdoptRef(new TestStream);
  ASSERT_OK(codec->ActivateStream(stream));
  ASSERT_OK(stream->Bind());

  auto client_wrap = fidl::BindSyncClient(tester.FidlClient<audio_fidl::Device>());
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto result = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->GetProperties();
  ASSERT_OK(result.status());
  const char* kManufacturer = "<unknown>";
  ASSERT_BYTES_EQ(result->properties.manufacturer().data(), kManufacturer, strlen(kManufacturer));

  EXPECT_EQ(result->properties.is_input(), false);
  EXPECT_EQ(result->properties.min_gain_db(), 0.);
  EXPECT_EQ(result->properties.max_gain_db(), 0.);
  EXPECT_EQ(result->properties.gain_step_db(), 0.);
  EXPECT_EQ(result->properties.can_mute(), false);
  EXPECT_EQ(result->properties.can_agc(), false);
  EXPECT_EQ(result->properties.plug_detect_capabilities(),
            audio_fidl::wire::PlugDetectCapabilities::kHardwired);
  EXPECT_EQ(result->properties.clock_domain(), 0);

  codec->DeviceRelease();
  fake_controller.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_controller.DdkRelease();
}

TEST(HdaStreamTest, SetAndGetGainDefaults) {
  Binder tester;
  FakeController fake_controller(fake_ddk::kFakeParent);
  ASSERT_OK(fake_controller.Bind());

  auto codec = fbl::AdoptRef(new TestCodec);
  auto ret = codec->Bind(fake_controller.dev(), "test");
  ASSERT_OK(ret.code());
  auto stream = fbl::AdoptRef(new TestStream);
  ASSERT_OK(codec->ActivateStream(stream));
  ASSERT_OK(stream->Bind());

  auto client_wrap = fidl::BindSyncClient(tester.FidlClient<audio_fidl::Device>());
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  {
    {
      fidl::Arena allocator;
      audio_fidl::wire::GainState gain_state(allocator);
      gain_state.set_gain_db(allocator, kTestGain);
      auto status =
          fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->SetGain(std::move(gain_state));
      EXPECT_OK(status.status());
    }
    auto gain_state = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->WatchGainState();
    EXPECT_OK(gain_state.status());
    EXPECT_EQ(0, gain_state->gain_state.gain_db());  // Gain was not changed, default range is 0.
  }

  codec->DeviceRelease();
  fake_controller.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_controller.DdkRelease();
}

TEST(HdaStreamTest, WatchPlugStateDefaults) {
  Binder tester;
  FakeController fake_controller(fake_ddk::kFakeParent);
  ASSERT_OK(fake_controller.Bind());

  auto codec = fbl::AdoptRef(new TestCodec);
  auto ret = codec->Bind(fake_controller.dev(), "test");
  ASSERT_OK(ret.code());
  auto stream = fbl::AdoptRef(new TestStream);
  ASSERT_OK(codec->ActivateStream(stream));
  ASSERT_OK(stream->Bind());

  auto client_wrap = fidl::BindSyncClient(tester.FidlClient<audio_fidl::Device>());
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto state = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->WatchPlugState();
  ASSERT_OK(state.status());
  ASSERT_TRUE(state->plug_state.plugged());

  codec->DeviceRelease();
  fake_controller.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_controller.DdkRelease();
}

class TestStreamCustom : public TestStream {
 public:
  explicit TestStreamCustom() = default;
  void OnGetStringLocked(const audio_proto::GetStringReq& req,
                         audio_proto::GetStringResp* out_resp) {
    int res =
        snprintf(reinterpret_cast<char*>(out_resp->str), sizeof(out_resp->str), "%s", kTestString);
    out_resp->result = ZX_OK;
    out_resp->strlen = std::min<uint32_t>(res, sizeof(out_resp->str) - 1);
    out_resp->id = req.id;
  }
  void OnGetGainLocked(audio_proto::GainState* out_resp) {
    out_resp->cur_gain = last_gain_;
    out_resp->min_gain = kTestMinGain;
    out_resp->max_gain = kTestMaxGain;
    out_resp->gain_step = kTestGainStep;
    out_resp->cur_mute = false;
    out_resp->can_mute = true;
  }
  void OnSetGainLocked(const audio_proto::SetGainReq& req, audio_proto::SetGainResp* out_resp) {
    last_gain_ = req.gain;
    out_resp->result = ZX_OK;
  }
  void OnPlugDetectLocked(StreamChannel* response_channel, audio_proto::PlugDetectResp* out_resp) {
    out_resp->flags = (plugged_ ? AUDIO_PDNF_PLUGGED : 0) | AUDIO_PDNF_CAN_NOTIFY;
    out_resp->plug_state_time = kTestTime;
  }
  void NotifyPlugState(bool plugged, int64_t plug_time) {
    fbl::AutoLock lock(obj_lock());
    plugged_ = plugged;
    codecs::IntelHDAStreamBase::NotifyPlugStateLocked(plugged, plug_time);
  }

 private:
  float last_gain_ = kTestGain;
  bool plugged_ = true;
};

TEST(HdaStreamTest, GetStreamProperties) {
  Binder tester;
  FakeController fake_controller(fake_ddk::kFakeParent);
  ASSERT_OK(fake_controller.Bind());

  auto codec = fbl::AdoptRef(new TestCodec);
  auto ret = codec->Bind(fake_controller.dev(), "test");
  ASSERT_OK(ret.code());
  auto stream = fbl::AdoptRef(new TestStreamCustom);
  ASSERT_OK(codec->ActivateStream(stream));
  ASSERT_OK(stream->Bind());

  auto client_wrap = fidl::BindSyncClient(tester.FidlClient<audio_fidl::Device>());
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto result = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->GetProperties();
  ASSERT_OK(result.status());
  ASSERT_BYTES_EQ(result->properties.manufacturer().data(), kTestString, strlen(kTestString));
  ASSERT_BYTES_EQ(result->properties.product().data(), kTestString, strlen(kTestString));

  EXPECT_EQ(result->properties.is_input(), false);
  EXPECT_EQ(result->properties.min_gain_db(), kTestMinGain);
  EXPECT_EQ(result->properties.max_gain_db(), kTestMaxGain);
  EXPECT_EQ(result->properties.gain_step_db(), kTestGainStep);
  EXPECT_EQ(result->properties.can_mute(), true);
  EXPECT_EQ(result->properties.can_agc(), false);
  EXPECT_EQ(result->properties.plug_detect_capabilities(),
            audio_fidl::wire::PlugDetectCapabilities::kCanAsyncNotify);
  EXPECT_EQ(result->properties.clock_domain(), 0);

  codec->DeviceRelease();
  fake_controller.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_controller.DdkRelease();
}

TEST(HdaStreamTest, SetAndGetGain) {
  Binder tester;
  FakeController fake_controller(fake_ddk::kFakeParent);
  ASSERT_OK(fake_controller.Bind());

  auto codec = fbl::AdoptRef(new TestCodec);
  auto ret = codec->Bind(fake_controller.dev(), "test");
  ASSERT_OK(ret.code());
  auto stream = fbl::AdoptRef(new TestStreamCustom);
  ASSERT_OK(codec->ActivateStream(stream));
  ASSERT_OK(stream->Bind());

  auto client_wrap = fidl::BindSyncClient(tester.FidlClient<audio_fidl::Device>());
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  {
    {
      fidl::Arena allocator;
      audio_fidl::wire::GainState gain_state(allocator);
      gain_state.set_gain_db(allocator, kTestGain);
      auto status =
          fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->SetGain(std::move(gain_state));
      EXPECT_OK(status.status());
    }
    auto gain_state = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->WatchGainState();
    EXPECT_OK(gain_state.status());
    EXPECT_EQ(kTestGain, gain_state->gain_state.gain_db());
  }

  {
    std::thread th([&] {
      auto gain_state = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->WatchGainState();
      EXPECT_OK(gain_state.status());
      EXPECT_EQ(kTestGain2, gain_state->gain_state.gain_db());
    });
    fidl::Arena allocator;
    audio_fidl::wire::GainState gain_state(allocator);
    gain_state.set_gain_db(allocator, kTestGain2);
    auto status =
        fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->SetGain(std::move(gain_state));
    EXPECT_OK(status.status());
    th.join();
  }

  codec->DeviceRelease();
  fake_controller.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_controller.DdkRelease();
}

TEST(HdaStreamTest, WatchPlugState) {
  Binder tester;
  FakeController fake_controller(fake_ddk::kFakeParent);
  ASSERT_OK(fake_controller.Bind());

  auto codec = fbl::AdoptRef(new TestCodec);
  auto ret = codec->Bind(fake_controller.dev(), "test");
  ASSERT_OK(ret.code());
  auto stream = fbl::AdoptRef(new TestStreamCustom);
  ASSERT_OK(codec->ActivateStream(stream));
  ASSERT_OK(stream->Bind());

  auto client_wrap = fidl::BindSyncClient(tester.FidlClient<audio_fidl::Device>());
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);
  {
    auto state = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->WatchPlugState();
    ASSERT_OK(state.status());
    EXPECT_TRUE(state->plug_state.plugged());
  }
  {
    std::thread th([&] {
      auto state = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)->WatchPlugState();
      ASSERT_OK(state.status());
      EXPECT_FALSE(state->plug_state.plugged());
      EXPECT_EQ(state->plug_state.plug_state_time(), kTestTime);
    });
    stream->NotifyPlugState(false, kTestTime);
    th.join();
  }

  codec->DeviceRelease();
  fake_controller.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_controller.DdkRelease();
}

}  // namespace audio::intel_hda
