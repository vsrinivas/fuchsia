// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <audio-proto/audio-proto.h>
#include <ddktl/device.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <fuchsia/hardware/audio/c/fidl.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <atomic>
#include <type_traits>
#include <utility>

namespace audio {

struct SimpleAudioStreamProtocol : public ddk::internal::base_protocol {
    explicit SimpleAudioStreamProtocol(bool is_input) {
        ddk_proto_id_ = is_input
                      ? ZX_PROTOCOL_AUDIO_INPUT
                      : ZX_PROTOCOL_AUDIO_OUTPUT;
    }

    bool is_input() const { return ddk_proto_id_ == ZX_PROTOCOL_AUDIO_INPUT; }
};

class SimpleAudioStream;
using SimpleAudioStreamBase = ddk::Device<SimpleAudioStream,
                                          ddk::Messageable,
                                          ddk::Unbindable>;

class SimpleAudioStream : public SimpleAudioStreamBase,
                          public SimpleAudioStreamProtocol,
                          public fbl::RefCounted<SimpleAudioStream> {
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
    void Shutdown() __TA_EXCLUDES(domain_->token());

    // DDK device implementation
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
        return fuchsia_hardware_audio_Device_dispatch(this, txn, msg, &AUDIO_FIDL_THUNKS);
    }

  protected:
    friend class fbl::RefPtr<SimpleAudioStream>;

    SimpleAudioStream(zx_device_t* parent, bool is_input)
        : SimpleAudioStreamBase(parent),
          SimpleAudioStreamProtocol(is_input) {}
    virtual ~SimpleAudioStream() = default;

    // Hooks for driver implementation.

    // Init - General hook
    //
    // Called once during device creation, before the execution domain has been
    // created and before any device node has been published.
    //
    // During Init, devices *must*
    // 1) Populate the supported_formats_ vector with at least one valid format
    //    range.
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
    // Note: The execution domain has not been created at this point.  Because
    // of this, it is safe to assert that we are holding the domain token and to
    // access members and methods which are protected by the domain token.
    // Users should *not* attempt to activate any event sources during Init as
    // the domain has not been created and activation will fail.  See InitPost.
    //
    virtual zx_status_t Init() __TA_REQUIRES(domain_->token()) = 0;

    // InitPost - General hook
    //
    // Called once during device creation, after the execution domain has been
    // created and after Init has succeeded, but before any device node has been
    // published.
    //
    // This is the point at which the execution domain has been created and the
    // point at which implementations should activate any custom event sources
    // they need to use.
    //
    virtual zx_status_t InitPost() { return ZX_OK; }

    // RingBufferShutdown - General hook
    //
    // Called any time the client ring buffer channel is closed, and only after
    // the ring buffer is in the stopped state.  Implementations may release
    // their VMO and perform additional hardware shutdown tasks as needed here.
    //
    virtual void RingBufferShutdown() __TA_REQUIRES(domain_->token()) {}

    // ShutdownHook - general hook
    //
    // Called during final shutdown, after the execution domain has been
    // shutdown.  All execution domain event sources have been deactivated and
    // any callbacks have been completed.  Implementations should finish
    // completely shutting down all hardware and prepare for destruction.
    virtual void ShutdownHook() __TA_REQUIRES(domain_->token()) {}

    // EnableAsyncNotification - general hook
    //
    // Called whenever a client enables or disables notification of plug events.
    // Subclass can override this, to remain aware of these requests.
    virtual void EnableAsyncNotification(bool enable) __TA_REQUIRES(domain_->token()) {}

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
        __TA_REQUIRES(domain_->token()) = 0;

    // SetGain - Stream interface method
    //
    // Drivers which support gain control may overload this method in order to
    // receive a callback when a validated set gain request has been received by
    // a client.  After processing the request, drivers *must* update the
    // cur_gain_state_ member to indicate the current gain state.  This is what
    // will be reported to users who request a callback from SetGain, as well as
    // what will be reported for GetGain operations.
    //
    virtual zx_status_t SetGain(const audio_proto::SetGainReq& req)
        __TA_REQUIRES(domain_->token()) {
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
                                  uint32_t* out_num_rb_frames,
                                  zx::vmo* out_buffer) __TA_REQUIRES(domain_->token()) = 0;

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
    virtual zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_->token()) = 0;

    // Stop - RingBuffer interface method
    //
    // Stop the ring buffer.  Will only be called after both a format and a
    // buffer have been established, and only when the ring buffer is in the
    // started state.
    //
    virtual zx_status_t Stop() __TA_REQUIRES(domain_->token()) = 0;

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
    // TODO(johngro): Add support for IRQ event sources to the dispatcher pool
    // library.  With this is place, we can probably just demand that
    // NotifyPosition is always called from within the context of the execution
    // domain and not need to worry about any locking for the ring buffer
    // channel.
    zx_status_t NotifyPosition(const audio_proto::RingBufPositionNotify& notif);

    // Incoming interfaces (callable from child classes into this class)
    //

    // SetInitialPlugState
    //
    // Must be called by child class during Init(), so that the device's Plug
    // capabilities are correctly understood (and published) by the base class.
    void SetInitialPlugState(audio_pd_notify_flags_t initial_state) __TA_REQUIRES(domain_->token());

    // SetPlugState - asynchronous hook for child class
    //
    // Callable at any time after InitPost, if the device is not hardwired.
    // Must be called from the same execution domain as other hooks listed here.
    zx_status_t SetPlugState(bool plugged) __TA_REQUIRES(domain_->token());

    // Callable any time after SetFormat while the RingBuffer channel is active,
    // but only valid after GetBuffer is called. Can be called from any context.
    uint32_t LoadNotificationsPerRing() const { return expected_notifications_per_ring_.load(); }

    // The execution domain
    fbl::RefPtr<dispatcher::ExecutionDomain> domain_;

    // State and capabilities which need to be established and maintained by the
    // driver implementation.
    fbl::Vector<audio_stream_format_range_t> supported_formats_ __TA_GUARDED(domain_->token());
    audio_proto::GetGainResp cur_gain_state_ __TA_GUARDED(domain_->token());
    audio_stream_unique_id_t unique_id_ __TA_GUARDED(domain_->token()) = {};
    char mfr_name_[64] __TA_GUARDED(domain_->token()) = {};
    char prod_name_[64] __TA_GUARDED(domain_->token()) = {};
    char device_name_[32] = {};

    uint32_t frame_size_ __TA_GUARDED(domain_->token()) = 0;
    uint32_t fifo_depth_ __TA_GUARDED(domain_->token()) = 0;
    uint64_t external_delay_nsec_ __TA_GUARDED(domain_->token()) = 0;

  private:
    // Private subclass of dispatcher::Channel that adds DoublyLinkedListable for accounting
    class AudioStreamChannel : public dispatcher::Channel,
                            public fbl::DoublyLinkedListable<fbl::RefPtr<AudioStreamChannel>> {
      private:
        friend class dispatcher::Channel;
        friend class fbl::RefPtr<AudioStreamChannel>;

        AudioStreamChannel() = default;
        ~AudioStreamChannel() = default;
    };

    // Internal method; called by the general Create template method.
    zx_status_t CreateInternal();

    // Internal method; called after all initialization is complete to actually
    // publish the stream device node.
    zx_status_t PublishInternal();

    // fuchsia hardware audio Device Interface
    zx_status_t GetChannel(fidl_txn_t* txn);

    // Stream interface
    zx_status_t ProcessStreamChannel(dispatcher::Channel* channel, bool privileged)
        __TA_REQUIRES(domain_->token());

    void DeactivateStreamChannel(const dispatcher::Channel* channel)
        __TA_REQUIRES(domain_->token(), channel_lock_);

    zx_status_t OnGetStreamFormats(dispatcher::Channel* channel,
                                   const audio_proto::StreamGetFmtsReq& req) const
        __TA_REQUIRES(domain_->token());

    zx_status_t OnSetStreamFormat(dispatcher::Channel* channel,
                                  const audio_proto::StreamSetFmtReq& req,
                                  bool privileged)
        __TA_REQUIRES(domain_->token());

    zx_status_t OnGetGain(dispatcher::Channel* channel, const audio_proto::GetGainReq& req) const
        __TA_REQUIRES(domain_->token());

    zx_status_t OnSetGain(dispatcher::Channel* channel, const audio_proto::SetGainReq& req)
        __TA_REQUIRES(domain_->token());

    zx_status_t OnPlugDetect(dispatcher::Channel* channel, const audio_proto::PlugDetectReq& req)
        __TA_REQUIRES(domain_->token());

    zx_status_t OnGetUniqueId(dispatcher::Channel* channel,
                              const audio_proto::GetUniqueIdReq& req) const
        __TA_REQUIRES(domain_->token());

    zx_status_t OnGetString(dispatcher::Channel* channel,
                            const audio_proto::GetStringReq& req) const
        __TA_REQUIRES(domain_->token());

    zx_status_t NotifyPlugDetect() __TA_REQUIRES(domain_->token());

    // Ring buffer interface
    zx_status_t ProcessRingBufferChannel(dispatcher::Channel* channel)
        __TA_REQUIRES(domain_->token());

    void DeactivateRingBufferChannel(const dispatcher::Channel* channel)
        __TA_REQUIRES(domain_->token(), channel_lock_);

    zx_status_t OnGetFifoDepth(dispatcher::Channel* channel,
                               const audio_proto::RingBufGetFifoDepthReq& req)
        __TA_REQUIRES(domain_->token());

    zx_status_t OnGetBuffer(dispatcher::Channel* channel,
                            const audio_proto::RingBufGetBufferReq& req)
        __TA_REQUIRES(domain_->token());

    zx_status_t OnStart(dispatcher::Channel* channel, const audio_proto::RingBufStartReq& req)
        __TA_REQUIRES(domain_->token());

    zx_status_t OnStop(dispatcher::Channel* channel, const audio_proto::RingBufStopReq& req)
        __TA_REQUIRES(domain_->token());

    static fuchsia_hardware_audio_Device_ops_t AUDIO_FIDL_THUNKS;

    // Stream and ring buffer channel state.
    fbl::Mutex channel_lock_ __TA_ACQUIRED_AFTER(domain_->token());
    fbl::RefPtr<dispatcher::Channel> stream_channel_ __TA_GUARDED(channel_lock_);
    fbl::RefPtr<dispatcher::Channel> rb_channel_ __TA_GUARDED(channel_lock_);

    fbl::DoublyLinkedList<fbl::RefPtr<AudioStreamChannel>> plug_notify_channels_
        __TA_GUARDED(domain_->token());

    // Plug capabilities default to hardwired, if not changed by a child class.
    audio_pd_notify_flags_t pd_flags_ __TA_GUARDED(domain_->token())
        = AUDIO_PDNF_HARDWIRED | AUDIO_PDNF_PLUGGED;
    zx_time_t plug_time_ __TA_GUARDED(domain_->token()) = 0;

    // State used for protocol enforcement.
    bool rb_started_ __TA_GUARDED(domain_->token()) = false;
    bool rb_fetched_ __TA_GUARDED(domain_->token()) = false;
    std::atomic<uint32_t> expected_notifications_per_ring_{0};
};

}  // namespace audio
