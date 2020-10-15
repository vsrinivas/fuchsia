// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_AUDIO_STREAM_INCLUDE_LIB_SIMPLE_AUDIO_STREAM_SIMPLE_AUDIO_STREAM_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_AUDIO_STREAM_INCLUDE_LIB_SIMPLE_AUDIO_STREAM_SIMPLE_AUDIO_STREAM_H_

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <atomic>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <audio-proto/audio-proto.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

namespace audio {

namespace audio_fidl = ::llcpp::fuchsia::hardware::audio;

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

struct SimpleAudioStreamProtocol : public ddk::internal::base_protocol {
  explicit SimpleAudioStreamProtocol(bool is_input) {
    ddk_proto_id_ = is_input ? ZX_PROTOCOL_AUDIO_INPUT_2 : ZX_PROTOCOL_AUDIO_OUTPUT_2;
  }

  bool is_input() const { return ddk_proto_id_ == ZX_PROTOCOL_AUDIO_INPUT_2; }
};

class SimpleAudioStream;
using SimpleAudioStreamBase =
    ddk::Device<SimpleAudioStream, ddk::Messageable, ddk::Suspendable, ddk::Unbindable>;

// The SimpleAudioStream server (thread compatible) implements Device::Interface and
// RingBuffer::Interface.
// All this is serialized in the single threaded SimpleAudioStream's dispatcher().
class SimpleAudioStream : public SimpleAudioStreamBase,
                          public SimpleAudioStreamProtocol,
                          public fbl::RefCounted<SimpleAudioStream>,
                          public audio_fidl::Device::Interface,
                          public audio_fidl::RingBuffer::Interface {
 public:
  // Create
  //
  // A general method which handles the construction/initialization of
  // SimpleAudioStream implementation.  Given an implementation called
  // 'MyStream', invocation should look something like..
  //
  // auto stream = SimpleAudioStream::Create<MyStream>(arg1, arg2, ...);
  //
  // Note: Implementers are encouraged to keep their constructor/destructor
  // protected/private.  In order to do so, however, they will need to make
  // sure to 'friend class SimpleAudioStream', and to 'friend class fbl::RefPtr<T>'
  template <typename T, typename... ConstructorSignature>
  static fbl::RefPtr<T> Create(ConstructorSignature&&... args) {
    static_assert(std::is_base_of<SimpleAudioStream, T>::value,
                  "Class must derive from SimpleAudioStream!");

    fbl::AllocChecker ac;
    auto ret = fbl::AdoptRef(new (&ac) T(std::forward<ConstructorSignature>(args)...));

    if (!ac.check()) {
      return nullptr;
    }

    if (ret->CreateInternal() != ZX_OK) {
      ret->Shutdown();
      return nullptr;
    }

    return ret;
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(SimpleAudioStream);

  // Public properties.
  bool is_input() const { return SimpleAudioStreamProtocol::is_input(); }

  // User facing shutdown method.  Implementers with shutdown requirements
  // should overload ShutdownHook.
  void Shutdown() __TA_EXCLUDES(domain_token());

  // DDK device implementation
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void DdkSuspend(ddk::SuspendTxn txn);

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    audio_fidl::Device::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

 protected:
  friend class fbl::RefPtr<SimpleAudioStream>;

  SimpleAudioStream(zx_device_t* parent, bool is_input)
      : SimpleAudioStreamBase(parent),
        SimpleAudioStreamProtocol(is_input),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  virtual ~SimpleAudioStream() = default;

  // Hooks for driver implementation.

  // Init - General hook
  //
  // Called once during device creation, before the execution domain has been
  // created and before any device node has been published.
  //
  // During Init, devices *must*
  // 1) Populate the supported_formats_ vector with at least one valid format
  //    range. The flag ASF_RANGE_FLAG_FPS_CONTINUOUS is not supported (unless
  //    min_frames_per_second and max_frames_per_second are equal since in that
  //    case the flag is irrelevant).
  // 2) Report the stream's gain control capabilities and current gain control
  //    state in the cur_gain_state_ member.
  // 3) Supply a valid, null-terminated, device node name in the device_name_
  //    member.
  // 4) Supply a persistent unique ID in the unique_id_ member.
  // 5) Call SetInitialPlugState to declare its plug detection capabilities
  //    and initial plug state, if the device is not exclusively hardwired.
  //
  // During Init, devices *should*
  // 1) Supply a valid null-terminated UTF-8 encoded manufacturer name in the
  //    mfr_name_ member.
  // 2) Supply a valid null-terminated UTF-8 encoded product name in the
  //    prod_name_ member.
  //
  virtual zx_status_t Init() __TA_REQUIRES(domain_token()) = 0;

  // RingBufferShutdown - General hook
  //
  // Called any time the client ring buffer channel is closed, and only after
  // the ring buffer is in the stopped state.  Implementations may release
  // their VMO and perform additional hardware shutdown tasks as needed here.
  //
  virtual void RingBufferShutdown() __TA_REQUIRES(domain_token()) {}

  // ShutdownHook - general hook
  //
  // Called during final shutdown, after the execution domain has been
  // shutdown.  All execution domain event sources have been deactivated and
  // any callbacks have been completed.  Implementations should finish
  // completely shutting down all hardware and prepare for destruction.
  virtual void ShutdownHook() __TA_REQUIRES(domain_token()) {}

  // Stream interface methods
  //
  // ChangeFormat - Stream interface method
  //
  // All drivers must implement ChangeFormat.  When called, the following
  // guarantees are provided.
  //
  // 1) Any existing ring buffer channel has been deactivated and the ring
  //    buffer (if it had existed previously) is in the stopped state.
  // 2) The format request has been validated against the supported_formats_
  //    list supplied by the implementation.
  // 3) The frame_size_ for the requested format has been computed.
  //
  // Drivers should take appropriate steps to prepare hardware for the
  // requested format change.  Depending on driver requirements, this may
  // involve configuring hardware and starting clocks, or may simply involve
  // deferring such operations until later.
  //
  // Upon success, drivers *must* have filled out the fifo_depth_ and
  // external_delay_nsec fields with appropriate values.
  //
  virtual zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
      __TA_REQUIRES(domain_token()) = 0;

  // SetGain - Stream interface method
  //
  // Drivers which support gain control may overload this method in order to
  // receive a callback when a validated set gain request has been received by
  // a client.  After processing the request, drivers *must* update the
  // cur_gain_state_ member to indicate the current gain state.  This is what
  // will be reported to users who request a callback from SetGain, as well as
  // what will be reported for GetGain operations.
  //
  virtual zx_status_t SetGain(const audio_proto::SetGainReq& req) __TA_REQUIRES(domain_token()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // RingBuffer interface methods
  //

  // GetBuffer - RingBuffer interface method
  //
  // Called after a successful format change in order to establish the shared
  // ring buffer.  GetBuffer will never be called while the ring buffer is in
  // the started state.
  //
  // Upon success, drivers should return a valid VMO with appropriate
  // permissions (READ | MAP | TRANSFER for inputs, WRITE as well for outputs)
  // as well as reporting the total number of usable frames in the ring.
  //
  virtual zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                uint32_t* out_num_rb_frames, zx::vmo* out_buffer)
      __TA_REQUIRES(domain_token()) = 0;

  // Start - RingBuffer interface method
  //
  // Start the ring buffer.  Will only be called after both a format and a
  // buffer have been established, and only when the ring buffer is in the
  // stopped state.
  //
  // Drivers *must* report the time at which the first frame will be clocked
  // out on the CLOCK_MONOTONIC timeline, not including any external delay.
  //
  // TODO(johngro): Adapt this when we support alternate HW clock domains.
  virtual zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_token()) = 0;

  // Stop - RingBuffer interface method
  //
  // Stop the ring buffer.  Will only be called after both a format and a
  // buffer have been established, and only when the ring buffer is in the
  // started state.
  //
  virtual zx_status_t Stop() __TA_REQUIRES(domain_token()) = 0;

  // RingBuffer interface events
  //

  // NotifyPosition - RingBuffer interface event
  //
  // Send a position notification to the client over the ring buffer channel,
  // if available.  May be called from any thread.  May return
  // ZX_ERR_BAD_STATE if the ring buffer channel is currently closed, or if
  // the active client has not requested that any position notifications be
  // provided.  Implementations may use this as a signal to stop notification
  // production until the point in time at which GetBuffer is called again.
  //
  zx_status_t NotifyPosition(const audio_proto::RingBufPositionNotify& notif);

  // Incoming interfaces (callable from child classes into this class)
  //

  // SetInitialPlugState
  //
  // Must be called by child class during Init(), so that the device's Plug
  // capabilities are correctly understood (and published) by the base class.
  void SetInitialPlugState(audio_pd_notify_flags_t initial_state) __TA_REQUIRES(domain_token());

  // SetPlugState - asynchronous hook for child class
  //
  // Callable at any time after InitPost, if the device is not hardwired.
  // Must be called from the same execution domain as other hooks listed here.
  zx_status_t SetPlugState(bool plugged) __TA_REQUIRES(domain_token());

  // Callable any time after SetFormat while the RingBuffer channel is active,
  // but only valid after GetBuffer is called. Can be called from any context.
  uint32_t LoadNotificationsPerRing() const { return expected_notifications_per_ring_.load(); }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  const Token& domain_token() const __TA_RETURN_CAPABILITY(domain_token_) { return domain_token_; }

  // State and capabilities which need to be established and maintained by the
  // driver implementation.
  fbl::Vector<audio_stream_format_range_t> supported_formats_ __TA_GUARDED(domain_token());
  audio_proto::GetGainResp cur_gain_state_ __TA_GUARDED(domain_token());
  audio_stream_unique_id_t unique_id_ __TA_GUARDED(domain_token()) = {};
  char mfr_name_[64] __TA_GUARDED(domain_token()) = {};
  char prod_name_[64] __TA_GUARDED(domain_token()) = {};
  char device_name_[32] = {};
  int32_t clock_domain_ __TA_GUARDED(domain_token()) = 0;

  uint32_t frame_size_ __TA_GUARDED(domain_token()) = 0;
  uint32_t fifo_depth_ __TA_GUARDED(domain_token()) = 0;
  uint64_t external_delay_nsec_ __TA_GUARDED(domain_token()) = 0;
  audio_pd_notify_flags_t pd_flags_ __TA_GUARDED(domain_token()) =
      AUDIO_PDNF_HARDWIRED | AUDIO_PDNF_PLUGGED;

 private:
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

  // StreamChannel (thread compatible) implements StreamConfig::Interface so the server for a
  // StreamConfig channel is a StreamChannel instead of a SimpleAudioStream (as is the case for
  // Device and RingBuffer channels), this way we can track which StreamConfig channel for plug
  // detect and gain changes notifications.
  // In some methods, we pass "this" (StreamChannel*) to SimpleAudioStream that
  // gets managed in SimpleAudioStream.
  // All this is serialized in the single threaded SimpleAudioStream's dispatcher().
  // All the StreamConfig::Interface methods are forwarded to SimpleAudioStream.
  class StreamChannel : public Channel,
                        public audio_fidl::StreamConfig::Interface,
                        public fbl::DoublyLinkedListable<fbl::RefPtr<StreamChannel>> {
   public:
    // Does not take ownership of stream, which must refer to a valid SimpleAudioStream that
    // outlives this object.
    explicit StreamChannel(SimpleAudioStream* stream) : stream_(*stream) {
      last_reported_gain_state_.cur_gain = kInvalidGain;
    }
    ~StreamChannel() = default;

    // fuchsia hardware audio Stream Interface.
    virtual void GetProperties(GetPropertiesCompleter::Sync& completer) override {
      stream_.GetProperties(completer);
    }
    virtual void GetSupportedFormats(GetSupportedFormatsCompleter::Sync& completer) override {
      stream_.GetSupportedFormats(completer);
    }
    virtual void WatchGainState(WatchGainStateCompleter::Sync& completer) override {
      stream_.WatchGainState(this, completer);
    }
    virtual void WatchPlugState(WatchPlugStateCompleter::Sync& completer) override {
      stream_.WatchPlugState(this, completer);
    }
    virtual void SetGain(audio_fidl::GainState target_state,
                         SetGainCompleter::Sync& completer) override {
      stream_.SetGain(std::move(target_state), completer);
    }
    virtual void CreateRingBuffer(audio_fidl::Format format, zx::channel ring_buffer,
                                  CreateRingBufferCompleter::Sync& completer) override {
      stream_.CreateRingBuffer(this, std::move(format), std::move(ring_buffer), completer);
    }

   private:
    friend class SimpleAudioStream;

    enum class Plugged : uint32_t {
      kNotReported = 1,
      kPlugged = 2,
      kUnplugged = 3,
    };

    static constexpr float kInvalidGain = std::numeric_limits<float>::max();

    SimpleAudioStream& stream_;
    std::optional<StreamChannel::WatchPlugStateCompleter::Async> plug_completer_;
    std::optional<StreamChannel::WatchGainStateCompleter::Async> gain_completer_;
    Plugged last_reported_plugged_state_ = Plugged::kNotReported;
    audio_proto::GetGainResp last_reported_gain_state_ = {};
  };
  // Internal method; called by the general Create template method.
  zx_status_t CreateInternal();

  // Internal method; called after all initialization is complete to actually
  // publish the stream device node.
  zx_status_t PublishInternal();

  // fuchsia hardware audio Device Interface
  void GetChannel(GetChannelCompleter::Sync& completer) override;

  // fuchsia hardware audio RingBuffer Interface
  virtual void GetProperties(GetPropertiesCompleter::Sync& completer) override;
  virtual void GetVmo(uint32_t min_frames, uint32_t notifications_per_ring,
                      audio_fidl::RingBuffer::Interface::GetVmoCompleter::Sync& completer) override;
  virtual void Start(StartCompleter::Sync& completer) override;
  virtual void Stop(StopCompleter::Sync& completer) override;
  virtual void WatchClockRecoveryPositionInfo(
      WatchClockRecoveryPositionInfoCompleter::Sync& completer) override;

  // fuchsia hardware audio Stream Interface (forwarded from StreamChannel)
  void GetProperties(StreamChannel::GetPropertiesCompleter::Sync& completer);
  void GetSupportedFormats(StreamChannel::GetSupportedFormatsCompleter::Sync& completer);
  void CreateRingBuffer(StreamChannel* channel, audio_fidl::Format format, zx::channel ring_buffer,
                        StreamChannel::CreateRingBufferCompleter::Sync& completer);
  void WatchGainState(StreamChannel* channel,
                      StreamChannel::WatchGainStateCompleter::Sync& completer);
  void WatchPlugState(StreamChannel* channel,
                      StreamChannel::WatchPlugStateCompleter::Sync& completer);
  void SetGain(audio_fidl::GainState target_state,
               StreamChannel::SetGainCompleter::Sync& completer);

  void DeactivateStreamChannel(StreamChannel* channel) __TA_REQUIRES(domain_token(), channel_lock_);

  zx_status_t NotifyPlugDetect() __TA_REQUIRES(domain_token());

  void DeactivateRingBufferChannel(const Channel* channel)
      __TA_REQUIRES(domain_token(), channel_lock_);

  // Stream and ring buffer channel state.
  fbl::Mutex channel_lock_ __TA_ACQUIRED_AFTER(domain_token());
  fbl::RefPtr<StreamChannel> stream_channel_ __TA_GUARDED(channel_lock_);
  fbl::RefPtr<Channel> rb_channel_ __TA_GUARDED(channel_lock_);
  fbl::DoublyLinkedList<fbl::RefPtr<StreamChannel>> stream_channels_ __TA_GUARDED(channel_lock_);

  // Plug capabilities default to hardwired, if not changed by a child class.
  zx_time_t plug_time_ __TA_GUARDED(domain_token()) = 0;

  // State used for protocol enforcement.
  bool rb_started_ __TA_GUARDED(domain_token()) = false;
  bool rb_fetched_ __TA_GUARDED(domain_token()) = false;
  std::atomic_bool shutting_down_ = false;

  // The server implementation is single threaded, however NotifyPosition() can be called from any
  // thread. Hence to use expected_notifications_per_ring_ and position_completer_ within
  // NotifyPosition() we make expected_notifications_per_ring_ atomic and protect
  // position_completer_ with position_lock_.
  std::atomic<uint32_t> expected_notifications_per_ring_{0};
  fbl::Mutex position_lock_;
  std::optional<WatchClockRecoveryPositionInfoCompleter::Async> position_completer_
      __TA_GUARDED(position_lock_);
  async::Loop loop_;
  Token domain_token_;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_AUDIO_STREAM_INCLUDE_LIB_SIMPLE_AUDIO_STREAM_SIMPLE_AUDIO_STREAM_H_
