// Copyright 2021 The Fuchsia Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/usb/composite/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <optional>
#include <vector>

#include <ddktl/device.h>
#include <usb/request-cpp.h>
#include <zxtest/zxtest.h>

#include "../usb-audio-device.h"
#include "../usb-audio-stream.h"

namespace {
namespace audio_fidl = fuchsia_hardware_audio;

audio_fidl::wire::PcmFormat GetDefaultPcmFormat() {
  audio_fidl::wire::PcmFormat format;
  format.number_of_channels = 2;
  format.channels_to_use_bitmask = 0x03;
  format.sample_format = audio_fidl::wire::SampleFormat::kPcmSigned;
  format.frame_rate = 48'000;
  format.bytes_per_sample = 2;
  format.valid_bits_per_sample = 16;
  return format;
}

class FakeDevice;
using FakeDeviceType = ddk::Device<FakeDevice>;

class FakeDevice : public FakeDeviceType,
                   public ddk::UsbProtocol<FakeDevice>,
                   public ddk::UsbCompositeProtocol<FakeDevice> {
 public:
  FakeDevice(zx_device_t* parent) : FakeDeviceType(parent) {}
  // dev() is used in Binder::DeviceGetProtocol below.
  zx_device_t* dev() { return reinterpret_cast<zx_device_t*>(this); }
  zx_status_t Bind() { return DdkAdd("usb-fake-device-test"); }
  void DdkRelease() {}

  usb_protocol_t proto() const {
    usb_protocol_t proto;
    proto.ctx = const_cast<FakeDevice*>(this);
    proto.ops = const_cast<usb_protocol_ops_t*>(&usb_protocol_ops_);
    return proto;
  }
  usb_composite_protocol_t proto_composite() const {
    usb_composite_protocol_t proto;
    proto.ctx = const_cast<FakeDevice*>(this);
    proto.ops = const_cast<usb_composite_protocol_ops_t*>(&usb_composite_protocol_ops_);
    return proto;
  }

  // USB protocol implementation.
  zx_status_t UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                            int64_t timeout, const uint8_t* write_buffer, size_t write_size) {
    return ZX_OK;
  }
  zx_status_t UsbControlIn(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                           int64_t timeout, uint8_t* out_read_buffer, size_t read_size,
                           size_t* out_read_actual) {
    struct {
      uint8_t request_type;
      uint8_t request;
      uint16_t value;
      uint16_t index;
      uint8_t data0;
      std::optional<uint8_t> data1;
    } canned_replies[] = {
        // clang-format off
        {0xA1, 0x82, 0x201, 0x900, 0x00, 0xdb},
        {0xA1, 0x83, 0x201, 0x900, 0x00, 0x00},
        {0xA1, 0x84, 0x201, 0x900, 0x00, 0x01},
        {0xA1, 0x82, 0x202, 0x900, 0x00, 0xdb},
        {0xA1, 0x83, 0x202, 0x900, 0x00, 0x00},
        {0xA1, 0x84, 0x202, 0x900, 0x00, 0x01},
        {0xA1, 0x81, 0x201, 0x900, 0x00, 0xf6},
        {0xA1, 0x81, 0x100, 0x900, 0x00, std::nullopt},
        {0xA1, 0x82, 0x200, 0xA00, 0x00, 0xf4},
        {0xA1, 0x83, 0x200, 0xA00, 0x00, 0x17},
        {0xA1, 0x84, 0x200, 0xA00, 0x00, 0x01},
        {0xA1, 0x81, 0x200, 0xA00, 0x00, 0x08},
        {0xA1, 0x81, 0x100, 0xA00, 0x00, std::nullopt},
        {0xA1, 0x81, 0x700, 0xA00, 0x01, std::nullopt},
        {0xA1, 0x82, 0x200, 0xD00, 0x00, 0xe9},
        {0xA1, 0x83, 0x200, 0xD00, 0x00, 0x08},
        {0xA1, 0x84, 0x200, 0xD00, 0x00, 0x01},
        {0xA1, 0x81, 0x200, 0xD00, 0x00, 0xf9},
        {0xA1, 0x81, 0x100, 0xD00, 0x01, std::nullopt},
        // clang-format on
    };
    for (size_t i = 0; i < std::size(canned_replies); ++i) {
      if (request_type == canned_replies[i].request_type && request == canned_replies[i].request &&
          value == canned_replies[i].value && index == canned_replies[i].index) {
        uint8_t* p = reinterpret_cast<uint8_t*>(out_read_buffer);
        *p++ = canned_replies[i].data0;
        if (canned_replies[i].data1.has_value()) {
          *p++ = canned_replies[i].data1.value();
          *out_read_actual = 2;
        } else {
          *out_read_actual = 1;
        }
        return ZX_OK;
      }
    }
    return ZX_ERR_INTERNAL;
  }

  void UsbRequestQueue(usb_request_t* usb_request,
                       const usb_request_complete_callback_t* complete_cb) {}

  usb_speed_t UsbGetSpeed() { return USB_SPEED_FULL; }

  zx_status_t UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint8_t UsbGetConfiguration() { return 0; }
  zx_status_t UsbSetConfiguration(uint8_t configuration) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbResetEndpoint(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbResetDevice() { return ZX_ERR_NOT_SUPPORTED; }
  size_t UsbGetMaxTransferSize(uint8_t ep_address) { return 0; }
  uint32_t UsbGetDeviceId() { return 0; }
  void UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc) {
    constexpr uint8_t descriptor[] = {0x12, 0x01, 0x00, 0x02, 0xe0, 0x01, 0x01, 0x40, 0x87,
                                      0x80, 0xaa, 0x0a, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    memcpy(out_desc, descriptor, sizeof(descriptor));
  }
  zx_status_t UsbGetConfigurationDescriptorLength(uint8_t configuration, size_t* out_length) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t UsbGetConfigurationDescriptor(uint8_t configuration, uint8_t* out_desc_buffer,
                                            size_t desc_size, size_t* out_desc_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t UsbGetDescriptorsLength() { return sizeof(usb_descriptor_); }
  void UsbGetDescriptors(uint8_t* out_descs_buffer, size_t descs_size, size_t* out_descs_actual) {
    memcpy(out_descs_buffer, usb_descriptor_, sizeof(usb_descriptor_));
    *out_descs_actual = sizeof(usb_descriptor_);
  }
  zx_status_t UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, uint16_t* out_lang_id,
                                     uint8_t* out_string_buffer, size_t string_size,
                                     size_t* out_string_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbCancelAll(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }
  uint64_t UsbGetCurrentFrame() { return 0; }
  size_t UsbGetRequestSize() {
    return usb::BorrowedRequest<void>::RequestSize(sizeof(usb_request_t));
  }

  // USB composite protocol implementation.
  size_t UsbCompositeGetAdditionalDescriptorLength() { return 0; }
  zx_status_t UsbCompositeGetAdditionalDescriptorList(uint8_t* out_desc_list, size_t desc_count,
                                                      size_t* out_desc_actual) {
    *out_desc_actual = 0;
    return ZX_OK;
  }
  zx_status_t UsbCompositeClaimInterface(const usb_interface_descriptor_t* desc, uint32_t length) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  static inline constexpr uint8_t usb_descriptor_[] = {
      0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x0a, 0x24, 0x01, 0x00, 0x01, 0x64,
      0x00, 0x02, 0x01, 0x02, 0x0c, 0x24, 0x02, 0x01, 0x01, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00,
      0x00, 0x0c, 0x24, 0x02, 0x02, 0x01, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x09, 0x24,
      0x03, 0x06, 0x01, 0x03, 0x00, 0x09, 0x00, 0x09, 0x24, 0x03, 0x07, 0x01, 0x01, 0x00, 0x08,
      0x00, 0x07, 0x24, 0x05, 0x08, 0x01, 0x0a, 0x00, 0x0a, 0x24, 0x06, 0x09, 0x0f, 0x01, 0x01,
      0x02, 0x02, 0x00, 0x09, 0x24, 0x06, 0x0a, 0x02, 0x01, 0x43, 0x00, 0x00, 0x09, 0x24, 0x06,
      0x0d, 0x02, 0x01, 0x03, 0x00, 0x00, 0x0d, 0x24, 0x04, 0x0f, 0x02, 0x01, 0x0d, 0x02, 0x03,
      0x00, 0x00, 0x00, 0x00, 0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x09, 0x04,
      0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00, 0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00, 0x0e,
      0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x02, 0x80, 0xbb, 0x00, 0x44, 0xac, 0x00, 0x09, 0x05,
      0x01, 0x09, 0xc8, 0x00, 0x01, 0x00, 0x00, 0x07, 0x25, 0x01, 0x01, 0x01, 0x01, 0x00, 0x09,
      0x04, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x09, 0x04, 0x02, 0x01, 0x01, 0x01, 0x02,
      0x00, 0x00, 0x07, 0x24, 0x01, 0x07, 0x01, 0x01, 0x00, 0x0e, 0x24, 0x02, 0x01, 0x01, 0x02,
      0x10, 0x02, 0x80, 0xbb, 0x00, 0x44, 0xac, 0x00, 0x09, 0x05, 0x82, 0x0d, 0x64, 0x00, 0x01,
      0x00, 0x00, 0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00};
};
}  // namespace

namespace audio::usb {

class Binder : public fake_ddk::Bind {
 public:
 private:
  using Operation = void (*)(void* ctx);
  struct Context {
    Operation unbind;
    Operation release;
    void* ctx;
  };
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override {
    auto context = reinterpret_cast<const FakeDevice*>(device);
    if (proto_id == ZX_PROTOCOL_USB) {
      *reinterpret_cast<usb_protocol_t*>(protocol) = context->proto();
      return ZX_OK;
    }
    if (proto_id == ZX_PROTOCOL_USB_COMPOSITE) {
      *reinterpret_cast<usb_composite_protocol_t*>(protocol) = context->proto_composite();
      return ZX_OK;
    }
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status;
    bad_parent_ = false;

    if (args && args->ops) {
      if (args->ops->message) {
        std::optional<zx::channel> remote_channel = std::nullopt;
        if (args->client_remote) {
          remote_channel.emplace(args->client_remote);
        }

        if ((status = fidl_.SetMessageOp(args->ctx, args->ops->message,
                                         std::move(remote_channel))) < 0) {
          return status;
        }
      }
      if (args->ops->unbind || args->ops->release) {
        devs_.push_back({args->ops->unbind, args->ops->release, args->ctx});
        unbind_op_ = args->ops->unbind;  // Starts the unbind/release of devices.
        op_ctx_ = args->ctx;
      }
    }

    *out = fake_ddk::kFakeDevice;
    add_called_ = true;
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) override {
    for (auto i = devs_.begin(); i != devs_.end();) {
      auto unbind = i->unbind;
      auto release = i->release;
      auto context = i->ctx;
      i = devs_.erase(i);
      if (unbind) {
        unbind(context);
      }
      if (release) {
        release(context);
      }
    }
    remove_called_ = true;
    return ZX_OK;
  }

  std::vector<Context> devs_;
};

TEST(UsbAudioTest, GetStreamProperties) {
  Binder tester;
  FakeDevice fake_device(fake_ddk::kFakeParent);
  ASSERT_OK(fake_device.Bind());
  ASSERT_OK(UsbAudioDevice::DriverBind(fake_device.dev()));

  fidl::WireSyncClient<audio_fidl::Device> client(std::move(tester.FidlClient()));
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto result = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel).GetProperties();
  ASSERT_OK(result.status());

  ASSERT_EQ(result->properties.clock_domain(), 0);
  ASSERT_EQ(result->properties.min_gain_db(), -37.);
  ASSERT_EQ(result->properties.max_gain_db(), 0.);
  ASSERT_EQ(result->properties.gain_step_db(), 1.);
  ASSERT_EQ(result->properties.can_mute(), true);
  ASSERT_EQ(result->properties.can_agc(), false);
  ASSERT_EQ(result->properties.plug_detect_capabilities(),
            audio_fidl::wire::PlugDetectCapabilities::kHardwired);

  fake_device.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_device.DdkRelease();
}

TEST(UsbAudioTest, MultipleStreamConfigClients) {
  Binder tester;
  FakeDevice fake_device(fake_ddk::kFakeParent);
  ASSERT_OK(fake_device.Bind());
  ASSERT_OK(UsbAudioDevice::DriverBind(fake_device.dev()));

  fidl::WireSyncClient<audio_fidl::Device> client(std::move(tester.FidlClient()));
  fidl::WireResult<audio_fidl::Device::GetChannel> ch1 = client.GetChannel();
  fidl::WireResult<audio_fidl::Device::GetChannel> ch2 = client.GetChannel();
  ASSERT_OK(ch1.status());
  ASSERT_OK(ch2.status());

  fake_device.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_device.DdkRelease();
}

TEST(UsbAudioTest, SetAndGetGain) {
  Binder tester;
  FakeDevice fake_device(fake_ddk::kFakeParent);
  ASSERT_OK(fake_device.Bind());
  ASSERT_OK(UsbAudioDevice::DriverBind(fake_device.dev()));

  fidl::WireSyncClient<audio_fidl::Device> client(std::move(tester.FidlClient()));
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  constexpr float kTestGain = -12.f;
  {
    fidl::Arena allocator;
    audio_fidl::wire::GainState gain_state(allocator);
    gain_state.set_gain_db(allocator, kTestGain);
    auto status =
        fidl::WireCall<audio_fidl::StreamConfig>(ch->channel).SetGain(std::move(gain_state));
    ASSERT_OK(status.status());
  }

  auto gain_state = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel).WatchGainState();
  ASSERT_OK(gain_state.status());

  ASSERT_EQ(kTestGain, gain_state->gain_state.gain_db());

  fake_device.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_device.DdkRelease();
}

TEST(UsbAudioTest, Enumerate) {
  Binder tester;
  FakeDevice fake_device(fake_ddk::kFakeParent);
  ASSERT_OK(fake_device.Bind());
  ASSERT_OK(UsbAudioDevice::DriverBind(fake_device.dev()));

  fidl::WireSyncClient<audio_fidl::Device> client_wrap(std::move(tester.FidlClient()));
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  fidl::WireSyncClient<audio_fidl::StreamConfig> client(std::move(ch->channel));

  auto ret = client.GetSupportedFormats();
  auto& supported_formats = ret->supported_formats;
  ASSERT_EQ(2, supported_formats.count());

  auto& formats1 = supported_formats[0].pcm_supported_formats2();
  ASSERT_EQ(1, formats1.channel_sets().count());
  ASSERT_EQ(2, formats1.channel_sets()[0].attributes().count());
  ASSERT_EQ(1, formats1.sample_formats().count());
  ASSERT_EQ(audio_fidl::wire::SampleFormat::kPcmSigned, formats1.sample_formats()[0]);
  ASSERT_EQ(1, formats1.frame_rates().count());
  ASSERT_EQ(48'000, formats1.frame_rates()[0]);
  ASSERT_EQ(1, formats1.bytes_per_sample().count());
  ASSERT_EQ(2, formats1.bytes_per_sample()[0]);
  ASSERT_EQ(1, formats1.valid_bits_per_sample().count());
  ASSERT_EQ(16, formats1.valid_bits_per_sample()[0]);

  auto& formats2 = supported_formats[1].pcm_supported_formats2();
  ASSERT_EQ(1, formats2.channel_sets().count());
  ASSERT_EQ(2, formats2.channel_sets()[0].attributes().count());
  ASSERT_EQ(1, formats2.sample_formats().count());
  ASSERT_EQ(audio_fidl::wire::SampleFormat::kPcmSigned, formats2.sample_formats()[0]);
  ASSERT_EQ(1, formats2.frame_rates().count());
  ASSERT_EQ(44'100, formats2.frame_rates()[0]);
  ASSERT_EQ(1, formats2.bytes_per_sample().count());
  ASSERT_EQ(2, formats2.bytes_per_sample()[0]);
  ASSERT_EQ(1, formats2.valid_bits_per_sample().count());
  ASSERT_EQ(16, formats2.valid_bits_per_sample()[0]);

  fake_device.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_device.DdkRelease();
}

TEST(UsbAudioTest, CreateRingBuffer) {
  Binder tester;
  FakeDevice fake_device(fake_ddk::kFakeParent);
  ASSERT_OK(fake_device.Bind());
  ASSERT_OK(UsbAudioDevice::DriverBind(fake_device.dev()));

  fidl::WireSyncClient<audio_fidl::Device> client_wrap(std::move(tester.FidlClient()));
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  fidl::WireSyncClient<audio_fidl::StreamConfig> client(std::move(ch->channel));

  auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(endpoints.value());

  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  format.set_pcm_format(allocator, GetDefaultPcmFormat());
  client.CreateRingBuffer(std::move(format), std::move(remote));

  fake_device.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_device.DdkRelease();
}

TEST(UsbAudioTest, RingBuffer) {
  Binder tester;
  FakeDevice fake_device(fake_ddk::kFakeParent);
  ASSERT_OK(fake_device.Bind());
  ASSERT_OK(UsbAudioDevice::DriverBind(fake_device.dev()));

  fidl::WireSyncClient<audio_fidl::Device> client(std::move(tester.FidlClient()));
  fidl::WireResult<audio_fidl::Device::GetChannel> ch = client.GetChannel();
  ASSERT_EQ(ch.status(), ZX_OK);

  auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(endpoints.value());

  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  format.set_pcm_format(allocator, GetDefaultPcmFormat());
  auto rb = fidl::WireCall<audio_fidl::StreamConfig>(ch->channel)
                .CreateRingBuffer(std::move(format), std::move(remote));
  ASSERT_OK(rb.status());

  auto result = fidl::WireCall<audio_fidl::RingBuffer>(local).GetProperties();
  ASSERT_OK(result.status());
  ASSERT_EQ(result->properties.external_delay(), 0);
  ASSERT_EQ(result->properties.fifo_depth(), 576);  // Changes with frame rate.
  ASSERT_EQ(result->properties.needs_cache_flush_or_invalidate(), true);

  constexpr uint32_t kNumberOfPositionNotifications = 5;
  constexpr uint32_t kMinFrames = 10;
  auto vmo = fidl::WireCall<audio_fidl::RingBuffer>(local).GetVmo(kMinFrames,
                                                                  kNumberOfPositionNotifications);
  ASSERT_OK(vmo.status());

  fake_device.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_device.DdkRelease();
}

}  // namespace audio::usb
