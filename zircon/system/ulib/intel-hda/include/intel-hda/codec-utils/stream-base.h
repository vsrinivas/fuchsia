// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTEL_HDA_CODEC_UTILS_STREAM_BASE_H_
#define INTEL_HDA_CODEC_UTILS_STREAM_BASE_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/intelhda/codec/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/channel.h>

#include <utility>

#include <audio-proto/audio-proto.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/intel-hda-proto.h>

#include "channel.h"

namespace audio {
namespace intel_hda {
namespace codecs {

// Thread safety token.
//
// This token acts like a "no-op mutex", allowing compiler thread safety annotations
// to be placed on code or data that should only be accessed by a particular thread.
// Any code that acquires the token makes the claim that it is running on the (single)
// correct thread, and hence it is safe to access the annotated data and execute the annotated code.
struct __TA_CAPABILITY("role") Token {};
class __TA_SCOPED_CAPABILITY ScopedToken {
 public:
  explicit ScopedToken(const Token& token) __TA_ACQUIRE(token) {}
  ~ScopedToken() __TA_RELEASE() {}
};

class IntelHDACodecDriverBase;

class IntelHDAStreamBase;

// IntelHdaStreamStream implements fidl::WireServer<Device>.
// All this is serialized in the single threaded IntelHdaStreamStream's dispatcher() in loop_.
class IntelHDAStreamBase : public fbl::RefCounted<IntelHDAStreamBase>,
                           public fbl::WAVLTreeContainable<fbl::RefPtr<IntelHDAStreamBase>>,
                           public fidl::WireServer<fuchsia_hardware_audio::Device> {
 public:
  // StreamChannel (thread compatible) implements fidl::WireServer<StreamConfig> so the server
  // for a StreamConfig channel is a StreamChannel instead of a IntelHDAStreamBase (as is the case
  // for Device and RingBuffer channels), this way we can track which StreamConfig channel for gain
  // changes notifications.
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
    // Does not take ownership of stream, which must refer to a valid IntelHDAStreamBase that
    // outlives this object.
    explicit StreamChannel(IntelHDAStreamBase* stream) : stream_(*stream) {
      last_reported_gain_state_.cur_gain = kInvalidGain;
    }

    // fuchsia hardware audio Stream Interface.
    void GetProperties(GetPropertiesRequestView request,
                       GetPropertiesCompleter::Sync& completer) override {
      stream_.GetProperties(this, completer);
    }
    void GetSupportedFormats(GetSupportedFormatsRequestView request,
                             GetSupportedFormatsCompleter::Sync& completer) override {
      stream_.GetSupportedFormats(completer);
    }
    void WatchGainState(WatchGainStateRequestView request,
                        WatchGainStateCompleter::Sync& completer) override {
      stream_.WatchGainState(this, completer);
    }
    void WatchPlugState(WatchPlugStateRequestView request,
                        WatchPlugStateCompleter::Sync& completer) override {
      stream_.WatchPlugState(this, completer);
    }
    void SetGain(SetGainRequestView request, SetGainCompleter::Sync& completer) override {
      stream_.SetGain(request->target_state, completer);
    }
    void CreateRingBuffer(CreateRingBufferRequestView request,
                          CreateRingBufferCompleter::Sync& completer) override {
      stream_.CreateRingBuffer(this, request->format, std::move(request->ring_buffer), completer);
    }

   private:
    friend class IntelHDAStreamBase;
    enum class Plugged : uint32_t {
      kNotReported = 1,
      kPlugged = 2,
      kUnplugged = 3,
    };

    static constexpr float kInvalidGain = std::numeric_limits<float>::max();

    IntelHDAStreamBase& stream_;
    std::optional<StreamChannel::WatchPlugStateCompleter::Async> plug_completer_;
    std::optional<StreamChannel::WatchGainStateCompleter::Async> gain_completer_;
    Plugged last_reported_plugged_state_ = Plugged::kNotReported;
    audio_proto::GainState last_reported_gain_state_ = {};
  };

  zx_status_t Activate(fbl::RefPtr<IntelHDACodecDriverBase>&& parent_codec,
                       const fbl::RefPtr<Channel>& codec_channel) __TA_EXCLUDES(obj_lock_);

  void Deactivate() __TA_EXCLUDES(obj_lock_, default_domain_token());

  zx_status_t ProcessResponse(const CodecResponse& resp) __TA_EXCLUDES(obj_lock_);
  zx_status_t ProcessRequestStream(const ihda_proto::RequestStreamResp& resp)
      __TA_EXCLUDES(obj_lock_);
  virtual zx_status_t ProcessSetStreamFmt(const ihda_proto::SetStreamFmtResp& resp)
      __TA_EXCLUDES(obj_lock_);
  void StreamChannelSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                              zx_status_t status, const zx_packet_signal_t* signal,
                              StreamChannel* channel, bool priviledged);

  uint32_t id() const { return id_; }
  bool is_input() const { return is_input_; }
  uint32_t GetKey() const { return id(); }
  async_dispatcher_t* dispatcher() const { return loop_.dispatcher(); }

 protected:
  friend class fbl::RefPtr<IntelHDAStreamBase>;

  enum class Ack {
    NO,
    YES,
  };
  IntelHDAStreamBase(uint32_t id, bool is_input);
  virtual ~IntelHDAStreamBase();

  void SetPersistentUniqueId(const audio_stream_unique_id_t& id) __TA_EXCLUDES(obj_lock_);
  void SetPersistentUniqueIdLocked(const audio_stream_unique_id_t& id) __TA_REQUIRES(obj_lock_);
  void SetFormatChangeInProgress(bool in_progress) __TA_REQUIRES(obj_lock_) {
    format_change_in_progress_ = in_progress;
  }
  bool IsFormatChangeInProgress() const __TA_REQUIRES(obj_lock_) {
    return format_change_in_progress_;
  }

  // Properties available to subclasses.
  uint8_t dma_stream_tag() const __TA_REQUIRES(obj_lock_) { return dma_stream_tag_; }

  const fbl::RefPtr<IntelHDACodecDriverBase>& parent_codec() const __TA_REQUIRES(obj_lock_) {
    return parent_codec_;
  }

  bool is_active() const __TA_REQUIRES(obj_lock_) { return parent_codec() != nullptr; }

  fbl::Mutex* obj_lock() __TA_RETURN_CAPABILITY(obj_lock_) { return &obj_lock_; }

  const Token& default_domain_token() const __TA_RETURN_CAPABILITY(domain_token_) {
    return domain_token_;
  }
  fbl::RefPtr<StreamChannel> stream_channel() const __TA_REQUIRES(obj_lock_) {
    return stream_channel_;
  }
  uint16_t encoded_fmt() const __TA_REQUIRES(obj_lock_) { return encoded_fmt_; }

  // Methods callable from subclasses
  zx_status_t PublishDeviceLocked() __TA_REQUIRES(obj_lock_);
  void SetSupportedFormatsLocked(fbl::Vector<audio_proto::FormatRange>&& formats)
      __TA_REQUIRES(obj_lock_) {
    supported_formats_ = std::move(formats);
  }

  // Overloads to control stream behavior.
  virtual zx_status_t OnActivateLocked() __TA_REQUIRES(obj_lock_);
  virtual void OnDeactivateLocked() __TA_REQUIRES(obj_lock_);
  virtual void OnChannelDeactivateLocked(const StreamChannel& channel) __TA_REQUIRES(obj_lock_);
  virtual zx_status_t OnDMAAssignedLocked() __TA_REQUIRES(obj_lock_);
  virtual zx_status_t OnSolicitedResponseLocked(const CodecResponse& resp) __TA_REQUIRES(obj_lock_);
  virtual zx_status_t OnUnsolicitedResponseLocked(const CodecResponse& resp)
      __TA_REQUIRES(obj_lock_);
  virtual zx_status_t BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt)
      __TA_REQUIRES(obj_lock_);
  virtual zx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt) __TA_REQUIRES(obj_lock_);
  // IntelHDAStreamBase assumes the derived classes do not update their gain on thier own.
  virtual void OnGetGainLocked(audio_proto::GainState* out_resp) __TA_REQUIRES(obj_lock_);
  virtual void OnSetGainLocked(const audio_proto::SetGainReq& req,
                               audio_proto::SetGainResp* out_resp) __TA_REQUIRES(obj_lock_);
  virtual void OnPlugDetectLocked(StreamChannel* channel, audio_proto::PlugDetectResp* out_resp)
      __TA_REQUIRES(obj_lock_);
  virtual void OnGetStringLocked(const audio_proto::GetStringReq& req,
                                 audio_proto::GetStringResp* out_resp) __TA_REQUIRES(obj_lock_);
  virtual void OnGetClockDomainLocked(audio_proto::GetClockDomainResp* out_resp)
      __TA_REQUIRES(obj_lock_);

  // Debug logging
  virtual void PrintDebugPrefix() const;

  zx_status_t SendCodecCommandLocked(uint16_t nid, CodecVerb verb, Ack do_ack)
      __TA_REQUIRES(obj_lock_);
  void NotifyPlugStateLocked(bool plugged, int64_t plug_time) __TA_REQUIRES(obj_lock_);

  zx_status_t SendCodecCommand(uint16_t nid, CodecVerb verb, Ack do_ack) __TA_EXCLUDES(obj_lock_) {
    fbl::AutoLock obj_lock(&obj_lock_);
    return SendCodecCommandLocked(nid, verb, do_ack);
  }

  // Exposed to derived class for thread annotations.
  const fbl::Mutex& obj_lock() const __TA_RETURN_CAPABILITY(obj_lock_) { return obj_lock_; }

  void ProcessClientDeactivateLocked(StreamChannel* channel) __TA_REQUIRES(obj_lock_);
  // Unsolicited tag allocation for streams.
  zx_status_t AllocateUnsolTagLocked(uint8_t* out_tag) __TA_REQUIRES(obj_lock_);
  void ReleaseUnsolTagLocked(uint8_t tag) __TA_REQUIRES(obj_lock_);

  // fuchsia.hardware.audio.Device
  void GetChannel(GetChannelRequestView request, GetChannelCompleter::Sync& completer) override;

  // fuchsia hardware audio Stream Interface (forwarded from StreamChannel)
  void GetProperties(StreamChannel* channel,
                     StreamChannel::GetPropertiesCompleter::Sync& completer);
  void GetSupportedFormats(StreamChannel::GetSupportedFormatsCompleter::Sync& completer);
  virtual void CreateRingBuffer(StreamChannel* channel, fuchsia_hardware_audio::wire::Format format,
                                ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer,
                                StreamChannel::CreateRingBufferCompleter::Sync& completer);
  // If a derived class needs to update gain on its own, it can override this method.
  void WatchGainState(StreamChannel* channel,
                      StreamChannel::WatchGainStateCompleter::Sync& completer);
  // Derived classes with async plug detect support can call NotifyPlugStateLocked and not
  // override this method.
  void WatchPlugState(StreamChannel* channel,
                      StreamChannel::WatchPlugStateCompleter::Sync& completer);
  void SetGain(fuchsia_hardware_audio::wire::GainState target_state,
               StreamChannel::SetGainCompleter::Sync& completer);

 private:
  void DeactivateStreamChannel(StreamChannel* channel);

  zx_status_t DoGetStreamFormatsLocked(StreamChannel* channel, bool privileged,
                                       const audio_proto::StreamGetFmtsReq& req)
      __TA_REQUIRES(obj_lock_);
  zx_status_t DoSetStreamFormatLocked(StreamChannel* channel, bool privileged,
                                      const audio_proto::StreamSetFmtReq& fmt)
      __TA_REQUIRES(obj_lock_);

  zx_status_t SetDMAStreamLocked(uint16_t id, uint8_t tag) __TA_REQUIRES(obj_lock_);

  const uint32_t id_;
  const bool is_input_;
  char dev_name_[ZX_DEVICE_NAME_MAX] = {0};
  fbl::Mutex obj_lock_;

  fbl::RefPtr<IntelHDACodecDriverBase> parent_codec_ __TA_GUARDED(obj_lock_);
  fbl::RefPtr<Channel> codec_channel_ __TA_GUARDED(obj_lock_);

  uint16_t dma_stream_id_ __TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_ID;
  uint8_t dma_stream_tag_ __TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_TAG;

  zx_device_t* parent_device_ __TA_GUARDED(obj_lock_) = nullptr;
  zx_device_t* stream_device_ __TA_GUARDED(obj_lock_) = nullptr;

  fbl::RefPtr<StreamChannel> stream_channel_ __TA_GUARDED(obj_lock_);
  fbl::Vector<audio_proto::FormatRange> supported_formats_ __TA_GUARDED(obj_lock_);
  fbl::DoublyLinkedList<fbl::RefPtr<StreamChannel>> stream_channels_ __TA_GUARDED(obj_lock_);

  uint16_t encoded_fmt_ __TA_GUARDED(obj_lock_);
  uint32_t unsol_tag_count_ __TA_GUARDED(obj_lock_) = 0;
  audio_stream_unique_id_t persistent_unique_id_;

  static zx_status_t EncodeStreamFormat(const audio_proto::StreamSetFmtReq& fmt,
                                        uint16_t* encoded_fmt_out);

  static zx_protocol_device_t STREAM_DEVICE_THUNKS;
  Token domain_token_;
  bool format_change_in_progress_ = false;
  ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> rb_channel_;
  audio_proto::GainState cur_gain_state_ = {};
  zx_time_t plug_time_ = 0;
  async::Loop loop_;
};

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio

#endif  // INTEL_HDA_CODEC_UTILS_STREAM_BASE_H_
