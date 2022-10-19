// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_STREAM_H_
#define SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_STREAM_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/profile.h>
#include <lib/zx/vmo.h>
#include <zircon/listnode.h>

#include <memory>

#include <audio-proto/audio-proto.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <usb/usb.h>

#include "debug-logging.h"

namespace audio {
namespace usb {

class UsbAudioDevice;
class UsbAudioStreamInterface;

struct AudioStreamProtocol : public ddk::internal::base_protocol {
  explicit AudioStreamProtocol(bool is_input) {
    ddk_proto_id_ = is_input ? ZX_PROTOCOL_AUDIO_INPUT : ZX_PROTOCOL_AUDIO_OUTPUT;
  }

  bool is_input() const { return (ddk_proto_id_ == ZX_PROTOCOL_AUDIO_INPUT); }
};

class UsbAudioStream;
using UsbAudioStreamBase =
    ddk::Device<UsbAudioStream,
                ddk::Messageable<fuchsia_hardware_audio::StreamConfigConnector>::Mixin,
                ddk::Unbindable>;

// UsbAudioStream implements WireServer<Device> and WireServer<RingBuffer>.
// All this is serialized in the single threaded UsbAudioStream's dispatcher() in loop_.
class UsbAudioStream : public UsbAudioStreamBase,
                       public AudioStreamProtocol,
                       public fbl::RefCounted<UsbAudioStream>,
                       public fbl::DoublyLinkedListable<fbl::RefPtr<UsbAudioStream>>,
                       public fidl::WireServer<fuchsia_hardware_audio::RingBuffer> {
 public:
  class Channel : public fbl::RefCounted<Channel> {
   public:
    template <typename T = Channel, typename... ConstructorSignature>
    static fbl::RefPtr<T> Create(ConstructorSignature&&... args) {
      fbl::AllocChecker ac;
      auto ptr = fbl::AdoptRef(new (&ac) T(std::forward<ConstructorSignature>(args)...));

      if (!ac.check()) {
        return nullptr;
      }

      return ptr;
    }

   private:
    friend class fbl::RefPtr<Channel>;
  };

  struct RingBufferChannel : public Channel {
    void UnbindServer() {
      if (binding_ref_.has_value()) {
        binding_ref_->Unbind();
      }
    }

    void BindServer(
        std::optional<fidl::ServerBindingRef<fuchsia_hardware_audio::RingBuffer>> binding_ref) {
      binding_ref_ = std::move(binding_ref);
    }

   private:
    std::optional<fidl::ServerBindingRef<fuchsia_hardware_audio::RingBuffer>> binding_ref_;
  };

  // StreamChannel (thread compatible) implements fidl::WireServer<StreamConfig> so the server
  // for a StreamConfig channel is a StreamChannel instead of a UsbAudioStream (as is the case for
  // Device and RingBuffer channels), this way we can track which StreamConfig channel for gain
  // changes notifications.
  // In some methods, we pass "this" (StreamChannel*) to UsbAudioStream that
  // gets managed in UsbAudioStream.
  // All this is serialized in the single threaded UsbAudioStream's dispatcher() in loop_.
  // All the fidl::WireServer<StreamConfig> methods are forwarded to UsbAudioStream.
  class StreamChannel : public Channel,
                        public fidl::WireServer<fuchsia_hardware_audio::StreamConfig>,
                        public fbl::DoublyLinkedListable<fbl::RefPtr<StreamChannel>> {
   public:
    // Does not take ownership of stream, which must refer to a valid UsbAudioStream that outlives
    // this object.
    explicit StreamChannel(UsbAudioStream* stream) : stream_(*stream) {
      last_reported_gain_state_.cur_gain = kInvalidGain;
    }
    ~StreamChannel() = default;

    void BindServer(
        std::optional<fidl::ServerBindingRef<fuchsia_hardware_audio::StreamConfig>> binding_ref) {
      binding_ref_ = std::move(binding_ref);
    }
    void UnbindServer() {
      if (binding_ref_.has_value()) {
        binding_ref_->Unbind();
      }
    }

    // fuchsia hardware audio Stream Interface.
    void GetProperties(GetPropertiesCompleter::Sync& completer) override {
      stream_.GetProperties(completer);
    }
    void GetHealthState(GetHealthStateCompleter::Sync& completer) override { completer.Reply({}); }
    void SignalProcessingConnect(SignalProcessingConnectRequestView request,
                                 SignalProcessingConnectCompleter::Sync& completer) override {
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }
    void GetSupportedFormats(GetSupportedFormatsCompleter::Sync& completer) override {
      stream_.GetSupportedFormats(completer);
    }
    void WatchGainState(WatchGainStateCompleter::Sync& completer) override {
      stream_.WatchGainState(this, completer);
    }
    void WatchPlugState(WatchPlugStateCompleter::Sync& completer) override {
      stream_.WatchPlugState(this, completer);
    }
    void SetGain(SetGainRequestView request, SetGainCompleter::Sync& completer) override {
      stream_.SetGain(std::move(request->target_state), completer);
    }
    void CreateRingBuffer(CreateRingBufferRequestView request,
                          CreateRingBufferCompleter::Sync& completer) override {
      stream_.CreateRingBuffer(this, std::move(request->format), std::move(request->ring_buffer),
                               completer);
    }

   private:
    friend class UsbAudioStream;

    enum class Plugged : uint32_t {
      kNotReported = 1,
      kPlugged = 2,
      kUnplugged = 3,
    };

    static constexpr float kInvalidGain = std::numeric_limits<float>::max();

    UsbAudioStream& stream_;
    std::optional<StreamChannel::WatchPlugStateCompleter::Async> plug_completer_;
    std::optional<StreamChannel::WatchGainStateCompleter::Async> gain_completer_;
    Plugged last_reported_plugged_state_ = Plugged::kNotReported;
    audio_proto::GainState last_reported_gain_state_ = {};
    std::optional<fidl::ServerBindingRef<fuchsia_hardware_audio::StreamConfig>> binding_ref_;
  };

  static fbl::RefPtr<UsbAudioStream> Create(UsbAudioDevice* parent,
                                            std::unique_ptr<UsbAudioStreamInterface> ifc);
  zx_status_t Bind();
  void StreamChannelSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                              zx_status_t status, const zx_packet_signal_t* signal,
                              Channel* channel, bool priviledged);
  void RingBufferChannelSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                  zx_status_t status, const zx_packet_signal_t* signal,
                                  Channel* channel);

  const char* log_prefix() const { return log_prefix_; }

  // DDK device implementation
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // For unit test.
  const inspect::Inspector& inspect() const { return inspect_; }

 private:
  friend class fbl::RefPtr<UsbAudioStream>;

  enum class RingBufferState {
    STOPPED,
    STOPPING,
    STOPPING_AFTER_UNPLUG,
    STARTING,
    STARTED,
  };

  UsbAudioStream(UsbAudioDevice* parent, std::unique_ptr<UsbAudioStreamInterface> ifc);
  virtual ~UsbAudioStream();

  void ComputePersistentUniqueId();

  void ReleaseRingBufferLocked() __TA_REQUIRES(lock_);

  // Device FIDL implementation
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override;

  // fuchsia hardware audio RingBuffer Interface
  void GetProperties(GetPropertiesCompleter::Sync& completer) override;
  void GetVmo(GetVmoRequestView request, GetVmoCompleter::Sync& completer) override;
  void Start(StartCompleter::Sync& completer) override;
  void Stop(StopCompleter::Sync& completer) override;
  void WatchClockRecoveryPositionInfo(
      WatchClockRecoveryPositionInfoCompleter::Sync& completer) override;
  void WatchDelayInfo(WatchDelayInfoCompleter::Sync& completer) override;

  // fuchsia hardware audio Stream Interface (forwarded from StreamChannel)
  void GetProperties(StreamChannel::GetPropertiesCompleter::Sync& completer);
  void GetSupportedFormats(StreamChannel::GetSupportedFormatsCompleter::Sync& completer);
  void CreateRingBuffer(StreamChannel* channel, fuchsia_hardware_audio::wire::Format format,
                        ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer,
                        StreamChannel::CreateRingBufferCompleter::Sync& completer);
  void WatchGainState(StreamChannel* channel,
                      StreamChannel::WatchGainStateCompleter::Sync& completer);
  void WatchPlugState(StreamChannel* channel,
                      StreamChannel::WatchPlugStateCompleter::Sync& completer);
  void SetGain(fuchsia_hardware_audio::wire::GainState target_state,
               StreamChannel::SetGainCompleter::Sync& completer);
  void SetActiveChannels(SetActiveChannelsRequestView request,
                         SetActiveChannelsCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void DeactivateStreamChannelLocked(StreamChannel* channel) __TA_REQUIRES(lock_);

  // Thunks for dispatching stream channel events.

  zx_status_t OnGetStreamFormatsLocked(Channel* channel, const audio_proto::StreamGetFmtsReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnSetStreamFormatLocked(Channel* channel, const audio_proto::StreamSetFmtReq& req,
                                      bool privileged) __TA_REQUIRES(lock_);
  zx_status_t OnGetGainLocked(Channel* channel, const audio_proto::GetGainReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnSetGainLocked(Channel* channel, const audio_proto::SetGainReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnPlugDetectLocked(Channel* channel, const audio_proto::PlugDetectReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnGetUniqueIdLocked(Channel* channel, const audio_proto::GetUniqueIdReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnGetStringLocked(Channel* channel, const audio_proto::GetStringReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnGetClockDomainLocked(Channel* channel, const audio_proto::GetClockDomainReq& req)
      __TA_REQUIRES(lock_);

  // Thunks for dispatching ring buffer channel events.
  zx_status_t ProcessRingBufferChannel(Channel* channel);
  void DeactivateRingBufferChannelLocked(const Channel* channel) __TA_REQUIRES(lock_);

  // Stream command handlers
  // Ring buffer command handlers
  zx_status_t OnGetFifoDepthLocked(Channel* channel, const audio_proto::RingBufGetFifoDepthReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnGetBufferLocked(Channel* channel, const audio_proto::RingBufGetBufferReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnStartLocked(Channel* channel, const audio_proto::RingBufStartReq& req)
      __TA_REQUIRES(lock_);
  zx_status_t OnStopLocked(Channel* channel, const audio_proto::RingBufStopReq& req)
      __TA_REQUIRES(lock_);

  void RequestComplete(usb_request_t* req);
  void QueueRequestLocked() __TA_REQUIRES(req_lock_);
  void CompleteRequestLocked(usb_request_t* req) __TA_REQUIRES(req_lock_);

  static void RequestCompleteCallback(void* ctx, usb_request_t* request);

  UsbAudioDevice& parent_;
  const std::unique_ptr<UsbAudioStreamInterface> ifc_;
  char log_prefix_[LOG_PREFIX_STORAGE] = {0};
  audio_stream_unique_id_t persistent_unique_id_;

  fbl::Mutex lock_;
  fbl::Mutex req_lock_ __TA_ACQUIRED_AFTER(lock_);

  fbl::RefPtr<StreamChannel> stream_channel_ __TA_GUARDED(lock_);
  fbl::RefPtr<RingBufferChannel> rb_channel_ __TA_GUARDED(lock_);
  fbl::DoublyLinkedList<fbl::RefPtr<StreamChannel>> stream_channels_ __TA_GUARDED(lock_);

  int32_t clock_domain_ = 0;

  size_t selected_format_ndx_;
  uint32_t selected_frame_rate_;
  uint32_t frame_size_;
  uint32_t iso_packet_rate_;
  uint32_t bytes_per_packet_;
  uint32_t fifo_bytes_;
  bool delay_info_updated_ = false;
  int64_t internal_delay_nsec_ = 0;
  uint32_t fractional_bpp_inc_;
  uint32_t fractional_bpp_acc_ __TA_GUARDED(req_lock_);
  uint32_t ring_buffer_offset_ __TA_GUARDED(req_lock_);
  uint64_t usb_frame_num_ __TA_GUARDED(req_lock_);
  bool rb_vmo_fetched_ __TA_GUARDED(lock_) = false;

  uint32_t bytes_per_notification_ = 0;
  uint32_t notification_acc_ __TA_GUARDED(req_lock_);

  zx::vmo ring_buffer_vmo_;
  void* ring_buffer_virt_ = nullptr;
  uint32_t ring_buffer_size_ = 0;
  uint32_t ring_buffer_pos_ __TA_GUARDED(req_lock_);
  volatile RingBufferState ring_buffer_state_ __TA_GUARDED(req_lock_) = RingBufferState::STOPPED;

  std::optional<StartCompleter::Async> start_completer_ __TA_GUARDED(req_lock_);
  std::optional<StopCompleter::Async> stop_completer_ __TA_GUARDED(req_lock_);
  std::optional<WatchClockRecoveryPositionInfoCompleter::Async> position_completer_
      __TA_GUARDED(req_lock_);

  list_node_t free_req_ __TA_GUARDED(req_lock_);
  uint32_t free_req_cnt_ __TA_GUARDED(req_lock_);
  uint32_t allocated_req_cnt_;
  const zx_time_t create_time_;

  // TODO(johngro) : See MG-940.  eliminate this ASAP
  bool req_complete_prio_bumped_ = false;

  // |shutting_down_| is a boolean indicating whether |loop_| is about to be shut down.
  bool shutting_down_ __TA_GUARDED(lock_) = false;
  async::Loop loop_;

  inspect::Inspector inspect_;
  inspect::Node root_;
  inspect::StringProperty state_;
  inspect::UintProperty number_of_stream_channels_;
  inspect::IntProperty start_time_;
  inspect::IntProperty position_request_time_;
  inspect::IntProperty position_reply_time_;
  inspect::UintProperty ring_buffer_size2_;
  inspect::UintProperty usb_requests_sent_;
  inspect::IntProperty usb_requests_outstanding_;
  inspect::UintProperty frames_requested_;
  inspect::UintProperty number_of_channels_;
  inspect::UintProperty frame_rate_;
  inspect::UintProperty bits_per_slot_;
  inspect::UintProperty bits_per_sample_;
  inspect::StringProperty sample_format_;
  inspect::UintArray supported_min_number_of_channels_;
  inspect::UintArray supported_max_number_of_channels_;
  inspect::UintArray supported_min_frame_rates_;
  inspect::UintArray supported_max_frame_rates_;
  inspect::UintArray supported_bits_per_slot_;
  inspect::UintArray supported_bits_per_sample_;
  inspect::StringArray supported_sample_formats_;
};

}  // namespace usb
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_STREAM_H_
