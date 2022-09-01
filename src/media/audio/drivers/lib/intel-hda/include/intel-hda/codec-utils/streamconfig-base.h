// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_STREAMCONFIG_BASE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_STREAMCONFIG_BASE_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/intelhda/codec/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>

#include "stream-base.h"

namespace audio::intel_hda::codecs {

class IntelHDACodecDriverBase;

// IntelHdaStreamStream implements fidl::WireServer<StreamConfigConnector>.
// All this is serialized in the single threaded IntelHdaStreamStream's dispatcher() in loop_.
class IntelHDAStreamConfigBase
    : public IntelHDAStreamBase,
      public fidl::WireServer<fuchsia_hardware_audio::StreamConfigConnector> {
 public:
  // StreamChannel (thread compatible) implements fidl::WireServer<StreamConfig> so the server
  // for a StreamConfig channel is a StreamChannel instead of a IntelHDAStreamBase (as is the case
  // for StreamConfigConnector and RingBuffer channels), this way we can track which StreamConfig
  // channel for gain changes notifications.
  // In some methods, we pass "this" (StreamChannel*) to IntelHDAStreamBase that
  // gets managed in IntelHDAStreamBase.
  // All this is serialized in the single threaded IntelHDAStreamBase's dispatcher() in loop_.
  // All the fidl::WireServer<StreamConfig> methods are forwarded to IntelHDAStreamBase.
  class StreamChannel : public RingBufferChannel,
                        public fidl::WireServer<fuchsia_hardware_audio::StreamConfig>,
                        public fbl::DoublyLinkedListable<fbl::RefPtr<StreamChannel>> {
   public:
    template <typename... ConstructorSignature>
    static fbl::RefPtr<StreamChannel> Create(ConstructorSignature&&... args) {
      fbl::AllocChecker ac;
      auto ptr =
          fbl::AdoptRef(new (&ac) StreamChannel(std::forward<ConstructorSignature>(args)...));

      if (!ac.check()) {
        return nullptr;
      }

      return ptr;
    }
    // Does not take ownership of stream, which must refer to a valid IntelHDAStreamConfigBase that
    // outlives this object.
    explicit StreamChannel(IntelHDAStreamConfigBase* stream) : stream_(*stream) {
      last_reported_gain_state_.cur_gain = kInvalidGain;
    }

    // fuchsia hardware audio Stream Interface.
    void GetProperties(GetPropertiesCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(stream_.obj_lock());
      stream_.GetProperties(this, completer);
    }
    void GetHealthState(GetHealthStateCompleter::Sync& completer) override { completer.Reply({}); }
    void SignalProcessingConnect(SignalProcessingConnectRequestView request,
                                 SignalProcessingConnectCompleter::Sync& completer) override {
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }
    void GetSupportedFormats(GetSupportedFormatsCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(stream_.obj_lock());
      stream_.GetSupportedFormats(completer);
    }
    void WatchGainState(WatchGainStateCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(stream_.obj_lock());
      stream_.WatchGainState(this, completer);
    }
    void WatchPlugState(WatchPlugStateCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(stream_.obj_lock());
      stream_.WatchPlugState(this, completer);
    }
    void SetGain(SetGainRequestView request, SetGainCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(stream_.obj_lock());
      stream_.SetGain(request->target_state, completer);
    }
    void CreateRingBuffer(CreateRingBufferRequestView request,
                          CreateRingBufferCompleter::Sync& completer) override {
      fbl::AutoLock obj_lock(stream_.obj_lock());
      stream_.CreateRingBuffer(this, request->format, std::move(request->ring_buffer), completer);
    }

   private:
    friend class IntelHDAStreamConfigBase;
    enum class Plugged : uint32_t {
      kNotReported = 1,
      kPlugged = 2,
      kUnplugged = 3,
    };

    static constexpr float kInvalidGain = std::numeric_limits<float>::max();

    IntelHDAStreamConfigBase& stream_;
    std::optional<StreamChannel::WatchPlugStateCompleter::Async> plug_completer_;
    std::optional<StreamChannel::WatchGainStateCompleter::Async> gain_completer_;
    Plugged last_reported_plugged_state_ = Plugged::kNotReported;
    audio_proto::GainState last_reported_gain_state_ = {};
  };
  void ProcessClientDeactivateLocked(StreamChannel* channel) __TA_REQUIRES(obj_lock());
  async_dispatcher_t* dispatcher() const { return loop_.dispatcher(); }

 protected:
  IntelHDAStreamConfigBase(uint32_t id, bool is_input);
  ~IntelHDAStreamConfigBase() override = default;

  fbl::RefPtr<StreamChannel> stream_channel() const __TA_REQUIRES(obj_lock()) {
    return stream_channel_;
  }
  void SetSupportedFormatsLocked(fbl::Vector<audio_proto::FormatRange>&& formats)
      __TA_REQUIRES(obj_lock()) {
    supported_formats_ = std::move(formats);
  }
  void NotifyPlugStateLocked(bool plugged, int64_t plug_time) __TA_REQUIRES(obj_lock());

  // Overrides.
  void OnDeactivate() override;
  void RemoveDeviceLocked() override __TA_REQUIRES(obj_lock());
  zx_status_t ProcessSetStreamFmtLocked(const ihda_proto::SetStreamFmtResp& codec_resp) override
      __TA_REQUIRES(obj_lock());

  // Overloads to control stream behavior.
  virtual void OnChannelDeactivateLocked(const StreamChannel& channel) __TA_REQUIRES(obj_lock());

  // IntelHDAStreamBase assumes the derived classes do not update their gain on their own.
  virtual void OnGetGainLocked(audio_proto::GainState* out_resp) __TA_REQUIRES(obj_lock());
  virtual void OnSetGainLocked(const audio_proto::SetGainReq& req,
                               audio_proto::SetGainResp* out_resp) __TA_REQUIRES(obj_lock());
  virtual void OnPlugDetectLocked(StreamChannel* channel, audio_proto::PlugDetectResp* out_resp)
      __TA_REQUIRES(obj_lock());
  virtual void OnGetStringLocked(const audio_proto::GetStringReq& req,
                                 audio_proto::GetStringResp* out_resp) __TA_REQUIRES(obj_lock());
  virtual void OnGetClockDomainLocked(audio_proto::GetClockDomainResp* out_resp)
      __TA_REQUIRES(obj_lock());

  // fuchsia.hardware.audio.StreamConfigConnector.
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override;

  // fuchsia hardware audio Stream Interface (forwarded from StreamChannel)
  // All require obj_lock since they take StreamConfig's address and use it.
  void GetProperties(StreamChannel* channel, StreamChannel::GetPropertiesCompleter::Sync& completer)
      __TA_REQUIRES(obj_lock());
  void GetSupportedFormats(StreamChannel::GetSupportedFormatsCompleter::Sync& completer)
      __TA_REQUIRES(obj_lock());
  virtual void CreateRingBuffer(StreamChannel* channel, fuchsia_hardware_audio::wire::Format format,
                                ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer,
                                StreamChannel::CreateRingBufferCompleter::Sync& completer)
      __TA_REQUIRES(obj_lock());
  // If a derived class needs to update gain on its own, it can override this method.
  void WatchGainState(StreamChannel* channel,
                      StreamChannel::WatchGainStateCompleter::Sync& completer)
      __TA_REQUIRES(obj_lock());
  // Derived classes with async plug detect support can call NotifyPlugStateLocked and not
  // override this method.
  void WatchPlugState(StreamChannel* channel,
                      StreamChannel::WatchPlugStateCompleter::Sync& completer)
      __TA_REQUIRES(obj_lock());
  void SetGain(fuchsia_hardware_audio::wire::GainState target_state,
               StreamChannel::SetGainCompleter::Sync& completer) __TA_REQUIRES(obj_lock());
  zx_status_t PublishDeviceLocked() override __TA_REQUIRES(obj_lock());

 private:
  void DeactivateStreamChannel(StreamChannel* channel);
  zx_status_t DoGetStreamFormatsLocked(StreamChannel* channel, bool privileged,
                                       const audio_proto::StreamGetFmtsReq& req)
      __TA_REQUIRES(obj_lock());
  zx_status_t DoSetStreamFormatLocked(StreamChannel* channel, bool privileged,
                                      const audio_proto::StreamSetFmtReq& fmt)
      __TA_REQUIRES(obj_lock());

  fbl::RefPtr<StreamChannel> stream_channel_ __TA_GUARDED(obj_lock());
  fbl::Vector<audio_proto::FormatRange> supported_formats_ __TA_GUARDED(obj_lock());
  fbl::DoublyLinkedList<fbl::RefPtr<StreamChannel>> stream_channels_ __TA_GUARDED(obj_lock());

  static zx_protocol_device_t STREAM_DEVICE_THUNKS;
  zx_device_t* stream_device_ __TA_GUARDED(obj_lock()) = nullptr;
  ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> rb_channel_;
  audio_proto::GainState cur_gain_state_ = {};
  zx_time_t plug_time_ = 0;
  async::Loop loop_;
};

}  // namespace audio::intel_hda::codecs

#endif  // SRC_MEDIA_AUDIO_DRIVERS_LIB_INTEL_HDA_INCLUDE_INTEL_HDA_CODEC_UTILS_STREAMCONFIG_BASE_H_
