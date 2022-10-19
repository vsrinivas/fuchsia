// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_DAI_H_
#define SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_DAI_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/audio/cpp/banjo.h>
#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <optional>

#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/metadata/audio.h>
#include <sdk/lib/fidl/cpp/binding.h>
#include <soc/aml-common/aml-tdm-audio.h>

#include "aml-tdm-config-device.h"

namespace audio::aml_g12 {

class AmlG12TdmDai;
using AmlG12TdmDaiDeviceType =
    ddk::Device<AmlG12TdmDai, ddk::Messageable<fuchsia_hardware_audio::DaiConnector>::Mixin>;

class AmlG12TdmDai : public AmlG12TdmDaiDeviceType,
                     public ddk::DaiProtocol<AmlG12TdmDai, ddk::base_protocol>,
                     public ::fuchsia::hardware::audio::RingBuffer,
                     public ::fuchsia::hardware::audio::Dai {
 public:
  AmlG12TdmDai(zx_device_t* parent, ddk::PDev pdev);

  void DdkRelease();
  zx_status_t DaiConnect(zx::channel channel);
  zx_status_t InitPDev();
  void Shutdown();

 protected:
  // FIDL HLCPP methods for fuchsia.hardware.audio.ringbuffer.
  void Stop(StopCallback callback) override;  // Protected for unit test.

 private:
  // FIDL LLCPP method for fuchsia.hardware.audio.DaiConnector.
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override;

  // FIDL HLCPP methods for fuchsia.hardware.audio.Dai.
  void Reset(ResetCallback callback) override;
  void GetProperties(::fuchsia::hardware::audio::Dai::GetPropertiesCallback callback) override;
  void GetHealthState(::fuchsia::hardware::audio::Dai::GetHealthStateCallback callback) override {
    callback({});
  }
  void SignalProcessingConnect(
      fidl::InterfaceRequest<fuchsia::hardware::audio::signalprocessing::SignalProcessing>
          signal_processing) override {
    signal_processing.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void GetRingBufferFormats(GetRingBufferFormatsCallback callback) override;
  void GetDaiFormats(GetDaiFormatsCallback callback) override;
  void CreateRingBuffer(
      ::fuchsia::hardware::audio::DaiFormat dai_format,
      ::fuchsia::hardware::audio::Format ring_buffer_format,
      ::fidl::InterfaceRequest<::fuchsia::hardware::audio::RingBuffer> ring_buffer) override;

  // FIDL HLCPP methods for fuchsia.hardware.audio.ringbuffer.
  void GetProperties(
      ::fuchsia::hardware::audio::RingBuffer::GetPropertiesCallback callback) override;
  void WatchClockRecoveryPositionInfo(WatchClockRecoveryPositionInfoCallback callback) override;
  void WatchDelayInfo(WatchDelayInfoCallback callback) override;
  void GetVmo(uint32_t min_frames, uint32_t clock_recovery_notifications_per_ring,
              GetVmoCallback callback) override;
  void Start(StartCallback callback) override;
  void SetActiveChannels(uint64_t active_channels_bitmask,
                         SetActiveChannelsCallback callback) override {
    callback(fpromise::error(ZX_ERR_NOT_SUPPORTED));
  }

  zx_status_t InitBuffer(size_t size);
  void ProcessRingNotification();
  void InitDaiFormats();
  virtual bool AllowNonContiguousRingBuffer() { return false; }  // For unit testing.

  std::unique_ptr<AmlTdmConfigDevice> aml_audio_;
  metadata::AmlConfig metadata_ = {};
  uint32_t us_per_notification_ = 0;
  ::fuchsia::hardware::audio::DaiFormat dai_format_ = {};
  bool rb_started_ = false;
  bool rb_fetched_ = false;
  int64_t internal_delay_nsec_ = 0;
  bool delay_info_sent_ = false;
  async::TaskClosureMethod<AmlG12TdmDai, &AmlG12TdmDai::ProcessRingNotification> notify_timer_{
      this};
  zx::vmo ring_buffer_vmo_;
  fzl::PinnedVmo pinned_ring_buffer_;
  zx::bti bti_;
  std::optional<fidl::Binding<::fuchsia::hardware::audio::Dai>> dai_binding_;
  std::optional<fidl::Binding<::fuchsia::hardware::audio::RingBuffer>> ringbuffer_binding_;
  async::Loop loop_;
  uint32_t frame_size_ = 0;
  std::atomic<uint32_t> expected_notifications_per_ring_{0};
  std::optional<WatchClockRecoveryPositionInfoCallback> position_callback_;
  ddk::PDev pdev_;
};

}  // namespace audio::aml_g12

#endif  // SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_DAI_H_
