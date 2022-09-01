// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_DAI_BASE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_DAI_BASE_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/intelhda/codec/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>

#include "stream-base.h"

namespace audio::intel_hda::codecs {

class IntelHDACodecDriverBase;

// IntelHDADaiBase implements fidl::WireServer<DaiConnector>.
// All this is serialized in the single threaded IntelHDADaiBase's dispatcher() in loop_.
class IntelHDADaiBase : public IntelHDAStreamBase,
                        public fidl::WireServer<fuchsia_hardware_audio::DaiConnector> {
 public:
  // DaiChannel (thread compatible) implements fidl::WireServer<Dai> so the server
  // for a Dai channel is a DaiChannel instead of a IntelHDADaiBase (as is the case
  // for DaiConnector and RingBuffer channels), this way we can track which Dai channel for gain
  // changes notifications.
  // In some methods, we pass "this" (DaiChannel*) to IntelHDADaiBase that
  // gets managed in IntelHDADaiBase.
  // All this is serialized in the single threaded IntelHDADaiBase's dispatcher() in loop_.
  // All the fidl::WireServer<Dai> methods are forwarded to IntelHDADaiBase.
  class DaiChannel : public RingBufferChannel,
                     public fidl::WireServer<fuchsia_hardware_audio::Dai>,
                     public fbl::DoublyLinkedListable<fbl::RefPtr<DaiChannel>> {
   public:
    template <typename... ConstructorSignature>
    static fbl::RefPtr<DaiChannel> Create(ConstructorSignature&&... args) {
      fbl::AllocChecker ac;
      auto ptr = fbl::AdoptRef(new (&ac) DaiChannel(std::forward<ConstructorSignature>(args)...));

      if (!ac.check()) {
        return nullptr;
      }

      return ptr;
    }
    // Does not take ownership of DAI, which must refer to a valid IntelHDADaiBase that
    // outlives this object.
    explicit DaiChannel(IntelHDADaiBase* dai) : dai_(*dai) {}

    // fuchsia hardware audio DAI interface.
    void GetProperties(GetPropertiesCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(dai_.obj_lock());
      dai_.GetProperties(this, completer);
    }
    void GetHealthState(GetHealthStateCompleter::Sync& completer) override { completer.Reply({}); }
    void SignalProcessingConnect(SignalProcessingConnectRequestView request,
                                 SignalProcessingConnectCompleter::Sync& completer) override {
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }
    void GetRingBufferFormats(GetRingBufferFormatsCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(dai_.obj_lock());
      dai_.GetRingBufferFormats(completer);
    }
    void GetDaiFormats(GetDaiFormatsCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(dai_.obj_lock());
      dai_.GetDaiFormats(completer);
    }
    void Reset(ResetCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(dai_.obj_lock());
      dai_.Reset(completer);
    }
    void CreateRingBuffer(CreateRingBufferRequestView request,
                          CreateRingBufferCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(dai_.obj_lock());
      dai_.CreateRingBuffer(this, request->dai_format, request->ring_buffer_format,
                            std::move(request->ring_buffer), completer);
    }

   private:
    friend class IntelHDADaiBase;
    IntelHDADaiBase& dai_;
  };
  void ProcessClientDeactivateLocked(DaiChannel* channel) __TA_REQUIRES(obj_lock());
  async_dispatcher_t* dispatcher() const { return loop_.dispatcher(); }

 protected:
  IntelHDADaiBase(uint32_t id, bool is_input);
  ~IntelHDADaiBase() override = default;

  void SetSupportedFormatsLocked(fbl::Vector<audio_proto::FormatRange>&& formats)
      __TA_REQUIRES(obj_lock()) {
    supported_formats_ = std::move(formats);
  }
  // TODO(84428): As part of redesign SST add more than one format supported through this API.
  void SetSupportedDaiFormatsLocked(fuchsia_hardware_audio::wire::DaiFormat dai_format)
      __TA_REQUIRES(obj_lock()) {
    dai_format_ = std::move(dai_format);
  }
  void NotifyPlugStateLocked(bool plugged, int64_t plug_time) __TA_REQUIRES(obj_lock());

  // Overrides.
  void OnDeactivate() override;
  void RemoveDeviceLocked() override __TA_REQUIRES(obj_lock());
  zx_status_t ProcessSetStreamFmtLocked(const ihda_proto::SetStreamFmtResp& codec_resp) override
      __TA_REQUIRES(obj_lock());

  // Overloads to control DAI behavior.
  virtual void OnChannelDeactivateLocked(const DaiChannel& channel) __TA_REQUIRES(obj_lock());

  virtual void OnGetStringLocked(const audio_proto::GetStringReq& req,
                                 audio_proto::GetStringResp* out_resp) __TA_REQUIRES(obj_lock());
  virtual void OnResetLocked() __TA_REQUIRES(obj_lock()) = 0;

  // fuchsia.hardware.audio.DaiConnector.
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override;

  // fuchsia hardware audio Dai Interface (forwarded from DaiChannel)
  // All require obj_lock since they take Dai's address and use it.
  void GetProperties(DaiChannel* channel, DaiChannel::GetPropertiesCompleter::Sync& completer)
      __TA_REQUIRES(obj_lock());
  void GetRingBufferFormats(DaiChannel::GetRingBufferFormatsCompleter::Sync& completer)
      __TA_REQUIRES(obj_lock());
  void GetDaiFormats(DaiChannel::GetDaiFormatsCompleter::Sync& completer) __TA_REQUIRES(obj_lock());
  void Reset(DaiChannel::ResetCompleter::Sync& completer) __TA_REQUIRES(obj_lock());
  virtual void CreateRingBuffer(DaiChannel* channel,
                                fuchsia_hardware_audio::wire::DaiFormat dai_format,
                                fuchsia_hardware_audio::wire::Format ring_buffer_format,
                                ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer,
                                DaiChannel::CreateRingBufferCompleter::Sync& completer)
      __TA_REQUIRES(obj_lock());

  zx_status_t PublishDeviceLocked() override __TA_REQUIRES(obj_lock());

 private:
  fbl::RefPtr<DaiChannel> dai_channel_ __TA_GUARDED(obj_lock());
  fbl::Vector<audio_proto::FormatRange> supported_formats_ __TA_GUARDED(obj_lock());
  fuchsia_hardware_audio::wire::DaiFormat dai_format_ __TA_GUARDED(obj_lock());
  fbl::DoublyLinkedList<fbl::RefPtr<DaiChannel>> dai_channels_ __TA_GUARDED(obj_lock());

  static zx_protocol_device_t DAI_DEVICE_THUNKS;
  zx_device_t* dai_device_ __TA_GUARDED(obj_lock()) = nullptr;
  ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> rb_channel_;
  audio_proto::GainState cur_gain_state_ = {};
  zx_time_t plug_time_ = 0;
  async::Loop loop_;
};

}  // namespace audio::intel_hda::codecs

#endif  // SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_DAI_BASE_H_
