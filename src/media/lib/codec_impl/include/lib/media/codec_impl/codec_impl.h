// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_IMPL_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_IMPL_H_

#include <fuchsia/media/drm/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/fit/variant.h>
#include <lib/thread-safe-deleter/thread_safe_deleter.h>
#include <zircon/compiler.h>

#include <list>
#include <queue>

#include <fbl/macros.h>

#include "codec_adapter.h"
#include "codec_adapter_events.h"
#include "codec_admission_control.h"
#include "codec_buffer.h"
#include "codec_packet.h"
#include "fake_map_range.h"

// The CodecImpl class can be used for both SW and HW codecs.
//
// Roughly speaking, this class converts the Codec FIDL interface which has
// cross-process pipelining of stream switches into a more synchronous
// in-process CodecAdapter interface which only has input and output data
// handled async, with stream control handled sync for the most part.
//
// This class also handles Codec protocol checks applicable to any Codec server.
//
// TODO(dustingreen): Pull CodecImpl out to a source_set, to be used by
// omx_codec_runner.h/cc also.

// Lifetime:
//
// A CodecImpl is created, either Bind()ed or destructed, and if Bind()ed, then
// later when the channel fails or there's a protocol error, calls the owner's
// error handler and the owner deletes the CodecImpl.  There is intentionally no
// way to re-use a CodecImpl for another Codec channel.

// Error handling:
//
// There are two types of errors, per-CodecImpl errors and per-devhost-process
// errors.
//
// Per-CodecImpl:
//
// We handle per-Codec protocol errors and the like by calling Unbind() on the
// CodecImpl, which fairly soon results in ~CodecImpl async, but does not exit
// the whole devhost process.  We also handle a few per-CodecImpl errors this
// way even though some of those are closer to being caused by per-devhost
// conditions, just in case.
//
// Per-devhost-process:
//
// In contrast, per-devhost error conditions, like the inability to post work to
// the shared_fidl_thread(), are handled by exiting the devhost, because those
// conditions are not really unique to any one CodecImpl.

class CodecImpl : public fuchsia::media::StreamProcessor,
                  public CodecAdapterEvents,
                  private CodecAdapter {
 public:
  using StreamProcessorParams =
      fit::variant<fuchsia::mediacodec::CreateDecoder_Params,
                   fuchsia::mediacodec::CreateEncoder_Params, fuchsia::media::drm::DecryptorParams>;

  // The CodecImpl will take care of doing set_error_handler() on the sysmem
  // connection.  The sysmem connection should be set up to use the
  // shared_fidl_dispatcher.
  CodecImpl(fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem,
            std::unique_ptr<CodecAdmission> codec_admission,
            async_dispatcher_t* shared_fidl_dispatcher, thrd_t shared_fidl_thread,
            StreamProcessorParams params,
            fidl::InterfaceRequest<fuchsia::media::StreamProcessor> request);

  ~CodecImpl();

  // This is only intended for use by LocalCodecFactory in creating the
  // appropriate CodecAdapter.
  std::mutex& lock();

  // The LocalCodecFactory calls this method once just after CodecImpl
  // construction and just before BindAsync().
  //
  // There's only one CodecAdapter for the lifetime of the CodecImpl.  This
  // mechanism intentionally doesn't permit switching input format to a
  // completely different format, and a CodecAdapter is free to reject any
  // format change it wants to reject.  Before giving up, a client that uses
  // per-stream input format overrides should go around one more time with a
  // freshly created Codec created directly with the new format if the client
  // gets a Codec failure having overridden the input format on a stream of a
  // Codec such that the stream's input format doesn't exactly match the Codec's
  // input format (at least for now).
  void SetCoreCodecAdapter(std::unique_ptr<CodecAdapter> codec_adapter);

  // BindAsync()
  //
  // This enables serving Codec (soon).
  //
  // Must be called on shared_fidl_thread.
  //
  // It remains permitted to cause ~CodecImpl (on shared_fidl_thread) after this
  // call.
  //
  // The core codec initialization and actual binding occur shortly later async
  // after the start of this call, possibly after this call has returned.  This
  // is to avoid core codec initialization slowing down the shared_fidl_thread()
  // which may be handling other stream data for a different CodecImpl instance.
  //
  // Any error, including those encountered before binding is fully complete,
  // will call error_handler on a clean stack on shared_fidl_thread(), after
  // this call (also on shared_fidl_thread()) returns.  If the client code runs
  // ~CodecImpl on shared_fidl_thread instead (before error_handler has run on
  // shared_fidl_thread), the error_handler will be deleted without being run.
  //
  // The error_handler is expected to trigger ~CodecImpl to run, either
  // synchronously during error_handler(), or shortly after async.  In other
  // words it's the responsibility of client code to delete the CodecImpl in a
  // timely manner during or soon after error_handler().  Until ~CodecImpl, the
  // CodecAdmission won't be released, and the channel itself won't be closed
  // (intentionally, to ensure the old instance is cleaned up before a new
  // instance is created based on a client retry triggered by server channel
  // closure).
  void BindAsync(fit::closure error_handler);

  //
  // Codec interface
  //
  void EnableOnStreamFailed() override;
  void SetInputBufferSettings(fuchsia::media::StreamBufferSettings input_settings) override;
  void AddInputBuffer(fuchsia::media::StreamBuffer buffer) override;
  void SetInputBufferPartialSettings(
      fuchsia::media::StreamBufferPartialSettings input_settings) override;
  void SetOutputBufferSettings(fuchsia::media::StreamBufferSettings output_settings) override;
  void AddOutputBuffer(fuchsia::media::StreamBuffer buffer) override;
  void SetOutputBufferPartialSettings(
      fuchsia::media::StreamBufferPartialSettings output_settings) override;
  void CompleteOutputBufferPartialSettings(uint64_t buffer_lifetime_ordinal) override;
  void FlushEndOfStreamAndCloseStream(uint64_t stream_lifetime_ordinal) override;
  void CloseCurrentStream(uint64_t stream_lifetime_ordinal, bool release_input_buffers,
                          bool release_output_buffers) override;
  void Sync(SyncCallback callback) override;
  void RecycleOutputPacket(fuchsia::media::PacketHeader available_output_packet) override;
  void QueueInputFormatDetails(uint64_t stream_lifetime_ordinal,
                               fuchsia::media::FormatDetails format_details) override;
  void QueueInputPacket(fuchsia::media::Packet packet) override;
  void QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) override;

  // These are public so that CodecBuffer doesn't have to be a friend of CodecImpl.

  // This way CodecBuffer doesn't use the core_codec_bti_ directly.
  zx_status_t Pin(uint32_t options, const zx::vmo& vmo, uint64_t offset, uint64_t size,
                  zx_paddr_t* addrs, size_t addrs_count, zx::pmt* pmt);

  // Complain sync, then Unbind() async.  Even if more than one caller
  // complains, the async Unbind() work will only run once (but in such cases it
  // can be nice to see all the complaining in case multiple things fail at
  // once).  While more than one source of failure can complain, only one will
  // actually trigger Unbind() work, and the rest will just return knowing that
  // Unbind() work is started.  The Unbind() work itself will synchronize such
  // that other-thread sources of failure are no longer possible (can no longer
  // even complain) before deallocating "this".
  //
  // Callers to Fail() must not be holding lock_.  On return from Fail(), "this"
  // must not be touched as it can already be deallocated.
  void Fail(const char* format, ...);

  // Callers to FailLocked() must hold lock_ during the call.  On return from
  // FailLocked(), the caller can know that "this" is still allocated only up
  // to the point where the caller releases lock_.  Callers are encouraged not
  // to touch "this" after the call to FailLocked() besides releasing lock_,
  // for consistency with how Fail() is used; that said, the unlock itself is
  // safe.
  void FailLocked(const char* format, ...);
  // Report a devhost-fatal error.  This method never returns - instead we
  // fault the whole process.  This should only be used in cases where we
  // don't really expect an error, and where a client can't unilaterally induce
  // the error - but in case the error happens despite not being expected, we
  // want nice output that's easy to debug.
  void FailFatalLocked(const char* format, ...);

 private:
  // We keep a queue of Stream objects rather than just a single current stream
  // object, so we can track which streams are future-discarded and which are
  // not yet known to be future-discarded.  This difference matters because
  // clients are not required to process OnOutputConstraints() with
  // stream_lifetime_ordinal of a stream that the client has since told the
  // server to discard, so we don't want StreamControl ordering domain getting
  // stuck waiting on a client to catch up to an output config that the client
  // won't process.  Instead, the StreamControl ordering domain can ignore any
  // additional messages related to the discarded stream until the stream
  // discarding message is reached at which point the core codec's mid-stream
  // output config change is cancelled/forgotten when we reset the core codec.
  //
  // In addition, if we're behind, we can catch up by skipping past some
  // messages for future-discarded streams to catch up to non-discarded stream
  // input quicker.  Theoretically we could do even better by having the FIDL
  // thread delete messages previously queued to the StreamControl domain
  // regarding a stream that is now known to be discarded by the FIDL thread,
  // and collapse/combine CloseCurrentStream() messages, but that's unlikely to
  // help much in practice and would make the implementation more difficult to
  // read, and we can mitigate unbounded queuing by demanding that clients not
  // get too far ahead else we close the channel.  While forcing a client to
  // wait isn't great, if we don't, we can't impose a circuit-breaker limit on
  // the count and/or size of queued channel messages either - ideally setting
  // such a limit should be possible for any protocol, so at some convenient
  // point the client needs to wait or postpone, but only if the client is
  // written to be able to get far ahead in the first place.
  //
  // We also keep some stream-specific tracking information in here as a
  // reasonably clean way to ensure that a new stream's tracking info is
  // initialized properly.
  class Stream {
   public:
    // These mutations occur in Output ordering domain (shared_fidl_thread()):
    explicit Stream(uint64_t stream_lifetime_ordinal);
    uint64_t stream_lifetime_ordinal();
    void SetFutureDiscarded();
    bool future_discarded();
    void SetFutureFlushEndOfStream();
    __WARN_UNUSED_RESULT bool future_flush_end_of_stream();

    // These mutations occur in StreamControl ordering domain:
    ~Stream();
    // This can be called 0-N times for a given stream, and each call replaces
    // any previously-set details.
    void SetInputFormatDetails(std::unique_ptr<fuchsia::media::FormatDetails> input_format_details);
    // Can be nullptr if no per-stream details have been set, in which case the
    // caller should look at CodecImpl::initial_input_format_details_
    // instead.  The returned pointer is only valid up until the next call to to
    // SetInputFormatDetails() or when the stream is deleted, whichever comes
    // first.  This is only meant to be called on stream_control_thread_.
    const fuchsia::media::FormatDetails* input_format_details();
    // We send oob_bytes (if any) to the core codec just before sending a
    // packet to the core codec, but only when the stream has OOB data pending.
    // A new stream has OOB data initially pending, and it becomes pending again
    // if SetInputFormatDetails() is used and the oob_bytes don't match
    // the effective oob_bytes before.  This way we avoid causing extra
    // input format changes for the core codec.
    void SetOobConfigPending(bool pending);
    __WARN_UNUSED_RESULT bool oob_config_pending();
    void SetInputEndOfStream();
    __WARN_UNUSED_RESULT bool input_end_of_stream();
    void SetOutputEndOfStream();
    __WARN_UNUSED_RESULT bool output_end_of_stream();
    void SetFailureSeen();
    __WARN_UNUSED_RESULT bool failure_seen();

    // These methods are called on the core codec processing domain.  See also
    // comments on output_format_pending_.
    void SetOutputFormatPending();
    void ClearOutputFormatPending();
    __WARN_UNUSED_RESULT bool output_format_pending();

    void SetMidStreamOutputConstraintsChangeActive();
    void ClearMidStreamOutputConstraintsChangeActive();
    __WARN_UNUSED_RESULT bool is_mid_stream_output_constraints_change_active();

   private:
    const uint64_t stream_lifetime_ordinal_ = 0;
    bool future_discarded_ = false;
    bool future_flush_end_of_stream_ = false;
    // Starts as nullptr for each new stream with implicit fallback to
    // initial_input_format_details_, but can be overridden on a per-stream
    // basis with QueueInputFormatDetails().
    std::unique_ptr<fuchsia::media::FormatDetails> input_format_details_;
    // This defaults to _true_, so that we send the OOB bytes to the HW for each
    // stream, if we have any oob_bytes to send.
    bool oob_config_pending_ = true;
    bool input_end_of_stream_ = false;
    bool output_end_of_stream_ = false;
    bool failure_seen_ = false;

    // This defaults to _true_, so that we send OnOutputFormat() before the
    // first OnOutputFormat() of a stream.  We also set this back to true any
    // time the core codec indicates onOutputFormat(), and any time the core
    // codec indicates onCoreCodecMidStreamOutputConstraintsChange() with action
    // required true.
    bool output_format_pending_ = true;

    // It's not permitted for the core codec to emit output while a mid-stream
    // output constraints change is active.
    bool is_mid_stream_output_constraints_change_active_ = false;
  };

  // PortSettings
  //
  // The PortSettings wraps/homogenizes the port settings regardless of whether
  // the settings are specified by the client using StreamBufferSettings or
  // StreamBufferPartialSettings.  In addition, in the case of
  // StreamBufferPartialSettings, this class tracks the settings that arrive
  // later from sysmem (whether we've received them yet, and if so, what the
  // values are).
  class PortSettings {
   public:
    PortSettings(CodecImpl* parent, CodecPort port, fuchsia::media::StreamBufferSettings settings);
    PortSettings(CodecImpl* parent, CodecPort port,
                 fuchsia::media::StreamBufferPartialSettings partial_settings);
    ~PortSettings();

    uint64_t buffer_lifetime_ordinal();

    uint64_t buffer_constraints_version_ordinal();

    uint32_t packet_count();
    uint32_t buffer_count();

    fuchsia::sysmem::CoherencyDomain coherency_domain();

    // If is_partial_settings(), the PortSettings are initially partial, with
    // sysmem used to complete the settings.  Along the way the PortSettings
    // transiently also have the zx::vmo handles.  In contrast, if
    // !is_partial_settings(), the settings are complete from the start (aside
    // from vmo handles which are never owned by PortSettings in this case).
    bool is_partial_settings();

    const fuchsia::media::StreamBufferPartialSettings& partial_settings();

    const fuchsia::media::StreamBufferSettings& settings();

    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> TakeToken();

    // The caller should std::move() in the buffer_collection_info.  This call
    // is only valid if this instance was created from
    // StreamBufferPartialSettings, and this method hasn't been called before
    // on this instance.
    void SetBufferCollectionInfo(fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info);

    const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info();

    // We use SetBufferCollectionInfo(), but then take the VMOs back.  This
    // just happens to be more convenient than taking the VMOs before doing
    // SetBufferCollectionInfo().
    zx::vmo TakeVmo(uint32_t buffer_index);

    uint64_t vmo_usable_start(uint32_t buffer_index);
    uint64_t vmo_usable_size();

    bool is_secure();

    // Only call from FIDL thread.
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> NewBufferCollectionRequest(
        async_dispatcher_t* dispatcher);

    // Only call from FIDL thread.
    fuchsia::sysmem::BufferCollectionPtr& buffer_collection();

    // Only call from FIDL thread.
    void UnbindBufferCollection();

    // This condition is necessary (but not sufficient) for
    // IsOutputConfiguredLocked() to return true.
    bool is_complete_seen_output();
    void SetCompleteSeenOutput();

   private:
    CodecImpl* parent_ = nullptr;

    CodecPort port_ = kInvalidPort;

    // Only one or the other of settings_ or partial_settings_ is set.
    std::unique_ptr<fuchsia::media::StreamBufferSettings> settings_;

    // Only needed/set for the partial_settings_ case.
    std::unique_ptr<const fuchsia::media::StreamBufferConstraints> constraints_;
    std::unique_ptr<fuchsia::media::StreamBufferPartialSettings> partial_settings_;

    fuchsia::sysmem::BufferCollectionPtr buffer_collection_;

    // In the case of partial_settings_, the remainder of the settings arrive
    // from sysmem in a BufferCollectionInfo_2.  When that arrives from
    // sysmem, we move the VMOs into CodecBuffer(s), and the remainder of the
    // settings get stored here.
    std::unique_ptr<fuchsia::sysmem::BufferCollectionInfo_2> buffer_collection_info_;

    bool is_complete_seen_output_ = false;
  };

  // While we list this first in the member variables to hint that this gets
  // destructed last, the actual mechanism of destruction of the CodecAdmission
  // is via posting to the shared_fidl_thread(), because if we add more stuff in
  // various base classes of this class we want the destruction of
  // CodecAdmission to happen last.  The close processing won't be considered
  // done until after this is destructed.
  //
  // See codec_admission_control.h for comments re. how we'll avoid failing a
  // create that is requested by a client shortly after the client closes the
  // previous Codec channel, when there's a concurrency cap of 1 (for example).
  std::unique_ptr<CodecAdmission> codec_admission_;

  fuchsia::sysmem::AllocatorPtr sysmem_;

  async_dispatcher_t* shared_fidl_dispatcher_;
  thrd_t shared_fidl_thread_;
  // Nearly every task we post to shared_fidl_dispatcher_ is actually posted via
  // this ClosureQueue, which is how we avoid running previously-queued lambdas
  // that capture "this" or part of "this" after "this" is already gone.  The
  // ~CodecImpl ensures that task deletion occurs _before_ most of ~CodecImpl by
  // calling shared_fidl_queue_.StopAndClear().
  ClosureQueue shared_fidl_queue_;

  // Parts of CodecImpl are accessed from shared_fidl_thread(),
  // stream_control_thread_, and decoder thread(s) such as interrupt handling
  // thread(s).
  //
  // FXL_GUARDED_BY() is not directly usable in this class because this class
  // takes advantage of for example being able to read outside the lock from
  // something that can only be modified on the current thread.  Also, which
  // thread is relevant can vary by port, while FXL_GUARDED_BY() doesn't have
  // any way to tag indexes of an array differently.
  //
  // TODO(dustingreen): Implement some lock-like contexts including reader vs.
  // writer aspects so we can use FXL_GUARDED_BY() (just not with the lock
  // directly).
  //
  // TODO(dustingreen): Switch to fbl::Mutex and fbl::ConditionVariable, because
  // they complain instead of blocking if repeated acquisition is attempted, and
  // because one can check whether the current thread holds the lock (for assert
  // purposes).
  std::mutex lock_;

  //
  // Setup/teardown aspects.
  //

  // This starts unbinding.  When unbinding is done and CodecImpl is ready to
  // be destructed, client_error_handler_ is called, unless this is being called
  // during ~CodecImpl in which case client_error_handler_ is deleted without
  // running instead.
  //
  // UnbindLocked() can be called in response to a channel error (in which case
  // the binding_ itself is already unbound), or can be called in response to a
  // protocol error.  It can be called on any thread.
  //
  // On the caller's release of lock_ after this call, "this" may be
  // deallocated, if UnbindLocked() was called on a thread other than
  // fidl_thread().  For consistency and simplicity, all callers should
  // avoid touching any part of "this" after return from this method other than
  // releasing lock_.
  //
  // If the reason for un-binding is a failure, call Fail() or FailLocked()
  // instead, which will log an error before calling UnbindLocked() at the end.
  void UnbindLocked();
  // Like UnbindLocked(), but acquires the lock so the caller doesn't have to.
  // On return from this method, "this" may already have been deleted.
  void Unbind();
  // Part of the implementation of UnbindLocked() and ~CodecImpl, which ensures
  // that all relevant FIDL bindings are un-bound.  Calls to this method must
  // only occur on the FIDL thread.
  void EnsureUnbindCompleted();

  // TODO(35200): This isn't fully hooked up yet, so doesn't actually yet
  // indicate whether buffers are secure.  Enforce that
  // port_settings_[X].is_secure() is consistent with these.
  fuchsia::mediacodec::SecureMemoryMode OutputSecureMemoryMode();
  fuchsia::mediacodec::SecureMemoryMode InputSecureMemoryMode();
  fuchsia::mediacodec::SecureMemoryMode PortSecureMemoryMode(CodecPort port);
  bool IsPortSecureRequired(CodecPort port);
  bool IsPortSecurePermitted(CodecPort port);

  // The CodecAdapter is owned by the CodecImpl, and is listed near the top of
  // the local variables in CodecImpl so that it gets deleted near the end of
  // ~CodecImpl's implicit deletions (just in case, as of this writing).
  //
  // The CodecAdapter must not make any CodecAdapterEvents calls into CodecImpl
  // while there's no active stream, and there will be no active stream by the
  // time ~CodecImpl starts.  The CodecAdapter must be ok with being destructed
  // any time there's no active stream.
  //
  // TODO(dustingreen): Maybe it would be more convenient for the CodecAdapter
  // if CodecImpl made a Shutdown() call on it after stopping the last stream
  // and before destruction - but let's see how this goes without the Shutdown()
  // call for now.
  std::unique_ptr<CodecAdapter> codec_adapter_;

  const StreamProcessorParams params_;

  // Regardless of which type of codec was created, these track the input
  // FormatDetails.
  //
  // We keep a copy of the format details used to create the codec, and on a
  // per-stream basis those details are used as the default details, but can be
  // overridden with QueueInputFormatDetails().  A new stream will default back
  // to the FormatDetails used to create the codec unless that stream uses
  // QueueInputFormatDetails().  The QueueInputFormatDetails() is not persistent
  // across streams.
  //
  // The oob_bytes field can be null if the codec type or specific format
  // does not require oob_bytes.
  //
  // This points directly to a field of decoder_params_ (or encoder_params_),
  // which out-last all usages of this pointer.
  const fuchsia::media::FormatDetails* initial_input_format_details_;

  // Held here temporarily until DeviceFidl is ready to handle errors so we can
  // bind.
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> tmp_sysmem_;

  // Held here temporarily until DeviceFidl is ready to handle errors so we can
  // bind.
  fidl::InterfaceRequest<fuchsia::media::StreamProcessor> tmp_interface_request_;

  // This binding doesn't channel-own this CodecImpl.  The DeviceFidl owns all
  // the CodecImpl(s).  The DeviceFidl will SetErrorHandler() such that its
  // ownership drops if the channel fails.  The CodecImpl takes care of cleaning
  // itself up before calling the DeviceFidl's error handler, so that CodecImpl
  // is ready for destruction by the time DeviceFidl's error handler is called.
  fidl::Binding<fuchsia::media::StreamProcessor, CodecImpl*> binding_;

  // This is the zx::channel we get indirectly from binding_.Unbind() (we only
  // need the zx::channel part).  We delay closing the Codec zx::channel until
  // after removing the concurrency tally in ~CodecAdmission, so that a Codec
  // client can try again immediately on noticing channel closure without
  // potentially bouncing off still-existing old CodecAdmission.
  zx::channel codec_to_close_;
  bool was_bind_async_called_ = false;
  // This being true means BindAsync() reached the point where we can and must
  // fail via UnbindLocked() instead of just running the owner's error handler
  // directly.
  bool was_logically_bound_ = false;
  async::Loop stream_control_loop_;
  thrd_t stream_control_thread_ = 0;
  ClosureQueue stream_control_queue_;
  fit::closure owner_error_handler_;
  bool was_unbind_started_ = false;
  bool is_stream_control_done_ = false;
  bool was_unbind_completed_ = false;
  std::condition_variable wake_stream_control_condition_;
  std::condition_variable stream_control_done_condition_;

  //
  // Codec protocol aspects.
  //

  // Some of the FIDL messages get handled or partly handled on the
  // StreamControl thread.
  void SetInputBufferSettings_StreamControl(fuchsia::media::StreamBufferSettings input_settings);
  // Temporary until StreamProcessor.AddInputBuffer is removed.
  void AddInputBuffer_StreamControl(fuchsia::media::StreamBuffer buffer);
  void AddInputBuffer_StreamControl(CodecBuffer::Info buffer_info, CodecVmoRange vmo_range);
  void SetInputBufferPartialSettings_StreamControl(
      fuchsia::media::StreamBufferPartialSettings input_partial_settings);
  void FlushEndOfStreamAndCloseStream_StreamControl(uint64_t stream_lifetime_ordinal);
  void CloseCurrentStream_StreamControl(uint64_t stream_lifetime_ordinal,
                                        bool release_input_buffers, bool release_output_buffers);
  void Sync_StreamControl(ThreadSafeDeleter<SyncCallback> callback);
  void QueueInputFormatDetails_StreamControl(uint64_t stream_lifetime_ordinal,
                                             fuchsia::media::FormatDetails format_details);
  void QueueInputPacket_StreamControl(fuchsia::media::Packet packet);
  void QueueInputEndOfStream_StreamControl(uint64_t stream_lifetime_ordinal);
  // This method returns false if input buffers aren't configured enough so far,
  // or if sysmem-based buffers can't be confirmed to be allocated.  On
  // returning false, IsStoppingLocked() will already be true.
  bool CheckWaitEnsureInputConfigured(std::unique_lock<std::mutex>& lock);

  __WARN_UNUSED_RESULT bool IsStreamActiveLocked();

  void SetInputBufferSettingsCommon(
      std::unique_lock<std::mutex>& lock, fuchsia::media::StreamBufferSettings* input_settings,
      fuchsia::media::StreamBufferPartialSettings* input_partial_settings);

  void SetOutputBufferSettingsCommon(
      std::unique_lock<std::mutex>& lock, fuchsia::media::StreamBufferSettings* output_settings,
      fuchsia::media::StreamBufferPartialSettings* output_partial_settings);

  void SetBufferSettingsCommon(std::unique_lock<std::mutex>& lock, CodecPort port,
                               fuchsia::media::StreamBufferSettings* settings,
                               fuchsia::media::StreamBufferPartialSettings* partial_settings,
                               const fuchsia::media::StreamBufferConstraints& constraints);
  void EnsureBuffersNotConfigured(std::unique_lock<std::mutex>& lock, CodecPort port);
  // Returns true if validation passed.  Returns false if validation failed and
  // FailLocked() has already been called with a specific error string (in which
  // case the caller will likely want to just return).
  __WARN_UNUSED_RESULT bool ValidateBufferSettingsVsConstraintsLocked(
      CodecPort port, const fuchsia::media::StreamBufferSettings& settings,
      const fuchsia::media::StreamBufferConstraints& constraints);

  // This is just validating that the _partial_ settings set by the client are
  // valid with respect to the constraints indicated to the client, without any
  // involvement of sysmem yet (but soon), so there's not a ton to validate
  // here.
  __WARN_UNUSED_RESULT bool ValidatePartialBufferSettingsVsConstraintsLocked(
      CodecPort port, const fuchsia::media::StreamBufferPartialSettings& partial_settings,
      const fuchsia::media::StreamBufferConstraints& constraints);

  __WARN_UNUSED_RESULT bool ValidateStreamBuffer(const fuchsia::media::StreamBuffer& buffer);

  // Temporary until StreamProcessor.AddOutputBuffer is removed.
  void AddOutputBufferInternal(fuchsia::media::StreamBuffer buffer);
  void AddOutputBufferInternal(CodecBuffer::Info buffer_info, CodecVmoRange vmo_range);

  // Returns true if the port is done configuring (last buffer was added).
  // Returns false if the port is not done configuring or if Fail() was called;
  // currently the caller doesn't need to tell the difference between these two
  // very different cases.
  __WARN_UNUSED_RESULT bool AddBufferCommon(CodecBuffer::Info buffer_info, CodecVmoRange vmo_range);

  // Return value of false means FailLocked() has already been called.
  __WARN_UNUSED_RESULT bool CheckOldBufferLifetimeOrdinalLocked(CodecPort port,
                                                                uint64_t buffer_lifetime_ordinal);

  // Return value of false means FailLocked() has already been called.
  __WARN_UNUSED_RESULT bool CheckStreamLifetimeOrdinalLocked(uint64_t stream_lifetime_ordinal);

  // Return value of false means FailLocked() has already been called.
  __WARN_UNUSED_RESULT bool StartNewStream(std::unique_lock<std::mutex>& lock,
                                           uint64_t stream_lifetime_ordinal);
  void EnsureStreamClosed(std::unique_lock<std::mutex>& lock);
  void EnsureCodecStreamClosedLockedInternal();

  // Run all items in the sysmem_completion_queue_.  The item itself is run
  // outside the lock.  Returns true if any completions ran.
  bool RunAnySysmemCompletions(std::unique_lock<std::mutex>& lock);

  // Only sysmem completions get posted this way.  These essentially cut in line
  // before most of the body of all QueueInput...StreamControl methods when
  // those are blocked waiting for sysmem completion.
  void PostSysmemCompletion(fit::closure to_run);
  // Returns false if IsStoppingLocked() is already true - just to save the
  // caller the hassle of checking itself.
  bool WaitEnsureSysmemReadyOnInput(std::unique_lock<std::mutex>& lock);
  void RunAnySysmemCompletionsOrWait(std::unique_lock<std::mutex>& lock);

  bool is_on_stream_failed_enabled_ = false;

  // This is the stream_lifetime_ordinal of the current stream as viewed from
  // StreamControl ordering domain.  This is the stream lifetime ordinal that
  // gets removed from the head of the Stream queue when StreamControl is done
  // with the stream.
  uint64_t stream_lifetime_ordinal_ = 0;
  // This is the stream_lifetime_ordinal of the most recent stream as viewed
  // from the Output ordering domain (FIDL thread).  This is the stream lifetime
  // ordinal that we add to the tail of the Stream queue.
  uint64_t future_stream_lifetime_ordinal_ = 0;

  // The Output ordering domain (FIDL thread) adds items to the tail of this
  // queue, and the StreamControl ordering domain removes items from the head of
  // this queue.  This queue is how the StreamControl ordering domain knows
  // whether a stream is discarded or not.  If a stream isn't discarded then the
  // StreamControl domain can keep waiting for the client to process
  // OnOutputConstraints() for that stream.  If the stream has been discarded,
  // then StreamControl ordering domain cannot expect the client to ever process
  // OnOutputConstraints() for the stream, and the StreamControl ordering domain
  // can instead move on to the next stream.
  //
  // In addition, this can allow the StreamControl ordering domain to skip past
  // stream-specific items for a stream that's already known to be discarded by
  // the client.
  std::list<std::unique_ptr<Stream>> stream_queue_;
  // When no current stream, this is nullptr.  When there is a current stream,
  // this points to that stream, owned by stream_queue_.
  Stream* stream_ = nullptr;

  std::unique_ptr<const fuchsia::media::StreamBufferConstraints> input_constraints_;

  // This holds the most recent settings received from the client and accepted,
  // received via SetInputBufferSettings()/SetInputBufferPartialSettings() or
  // SetOutputBufferSettings()/SetOutputBufferPartialSettings(). The settings
  // are retained as-received from the client.  In the case of the client
  // sending StreamBufferPartialSettings, we discover some of the settings via
  // sysmem (instead of from the client) and store those in port_settings_, to
  // homogenize how we handle the settigns between StreamBufferSettings and
  // StreamBufferPartialSettings.
  std::unique_ptr<PortSettings> port_settings_[kPortCount];

  // The most recent fully-configured input or output buffers had this
  // buffer_constraints_version_ordinal.  Even when !port_settings_[port], this
  // is used to detect whether the client has yet caught up to the
  // last_required_buffer_constraints_version_ordinal_[port].
  uint64_t last_provided_buffer_constraints_version_ordinal_[kPortCount] = {};

  // For CodecImpl, the initial StreamOutputConstraints can be the first sent
  // message. If sent that early, the StreamOutputConstraints is likely to
  // change again before any output data is emitted, but it _may not_.
  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> output_constraints_;

  // The core codec indicated that it didn't like an output config that had this
  // buffer_constraints_version_ordinal set.  Normally this would lead to
  // mid-stream output format change, but in case the client starts a new stream
  // before that can happen, we go ahead and force the client to provide a newer
  // config with newer buffer_constraints_version_ordinal before we do format
  // detection for the new stream, just in case the core codec would be annoyed
  // if we ignored it's previous indication.  There's no reason to require every
  // core codec to consider how an incomplete mid-stream format change of an old
  // stream interacts with a new stream, so essentially force the mid-stream
  // format change to complete before start of the new stream (as far as the
  // core codec can tell).  The core codec still has to tolerate stopping the
  // old stream before mid-stream format change is complete, so it's possible
  // we'll eventually decide all core codecs need to just consider an incomplete
  // mid-stream format change to be cancelled by stopping the old stream, in
  // which case we could remove this member var.
  uint64_t core_codec_meh_output_buffer_constraints_version_ordinal_ = 0;

  // The server's buffer_lifetime_ordinal, per port.  In contrast to
  // port_settings_[port].buffer_lifetime_ordinal, this value is allowed to be
  // even when the previous odd buffer_lifetime_ordinal is over, due to buffer
  // de-allocation.
  uint64_t buffer_lifetime_ordinal_[kPortCount] = {};

  // This is the buffer_lifetime_ordinal from SetOutputBufferSettings() or
  // SetInputBufferSettings().  This is used for protocol enforcement, to
  // enforce that AddOutputBuffer() or AddInputBuffer() is part of the same
  // buffer_lifetime_ordinal.
  uint64_t protocol_buffer_lifetime_ordinal_[kPortCount] = {};

  // Allocating these values and sending these values are tracked separately,
  // so that we can more tightly enforce the protocol.  If a client tries to
  // act on a newer ordinal before the server has actually sent it, the server
  // will notice that invalid client behavior and close the channel (instead
  // of just tracking a single number, which would potentially let the client
  // drive the server into the weeds).
  //
  // The next value we'll use for output buffer_constraints_version_ordinal and
  // output format_details_version_ordinal.
  uint64_t next_output_buffer_constraints_version_ordinal_ = 1;
  // For format-only changes that don't require buffer re-allocation, we can
  // just increment the format details ordinal.
  uint64_t next_output_format_details_version_ordinal_ = 1;

  // Separately from ordinal allocation, we track the most recent ordinal that
  // we've actually sent to the client, to allow tighter protocol enforcement in
  // case of a hostile client.
  uint64_t sent_buffer_constraints_version_ordinal_[kPortCount] = {};
  uint64_t sent_format_details_version_ordinal_[kPortCount] = {};

  // The server has sent this version ordinal with
  // buffer_constraints_action_required true.  The server can safely ignore any
  // output configuration that's stale vs. this, because the client will soon
  // catch up to at least this version.  This includes a value for input also,
  // for consistency, but this is mainly for output.
  uint64_t last_required_buffer_constraints_version_ordinal_[kPortCount] = {};

  // This is set when stream_.output_end_of_stream is set.
  std::condition_variable output_end_of_stream_seen_;

  // This is a queue of lambdas that are to be run on the StreamControl domain
  // before any further QueueInput... processing on StreamControl.  Even before
  // the sysmem completion is on this queue, QueueInput...StreamControl() will
  // be blocked waiting for sysmem completion to be done, and helping run any
  // items that show up on this queue.
  //
  // This line-cutting queue avoids forcing a round-trip to ensure the client
  // isn't sending any input until after the codec knows about the allocated
  // buffers.  This also avoids un-binding the client's channel while we wait
  // for sysmem allocation to be complete - this is worth avoiding because if we
  // unbind then we also don't find out about PEER_CLOSED which would be at
  // least somewhat problematic if the client didn't also cause sysmem
  // allocation to fail.
  //
  // We use wake_stream_control_condition_ to wake any
  // QueueInput...StreamControl waiter that's blocked and helping run items on
  // this queue, since we of course also have to give up on the wait if we're
  // shutting down, which is an aspect in common with other StreamControl waits
  // so it's convenient to share the condition var.
  std::queue<fit::closure> sysmem_completion_queue_;

  // Avoid re-posting to StreamControl to run sysmem_completion_queue_ items if
  // there's already a posted runner lambda that'll notice a newly-added item.
  bool is_sysmem_runner_pending_ = false;

  //
  // Adapter-related
  //

  // This is called on Output ordering domain (FIDL thread) any time a message
  // is received which would be able to start a new stream.
  //
  // More complete protocol validation happens on StreamControl ordering domain.
  // The validation here is just to validate to degree needed to not break our
  // stream_queue_ and future_stream_lifetime_ordinal_.
  //
  // Returns true if it worked.  Returns false if FailLocked() has already been
  // called, in which case the caller probably wants to just return.
  __WARN_UNUSED_RESULT bool EnsureFutureStreamSeenLocked(uint64_t stream_lifetime_ordinal);

  // This is called on Output ordering domain (FIDL thread) any time a message
  // is received which would close a stream.
  //
  // More complete protocol validation happens on StreamControl ordering domain.
  // The validation here is just to validate to degree needed to not break our
  // stream_queue_ and future_stream_lifetime_ordinal_.
  //
  // Returns true if it worked.  Returns false if FailLocked() has already been
  // called, in which case the caller probably wants to just return.
  __WARN_UNUSED_RESULT bool EnsureFutureStreamCloseSeenLocked(uint64_t stream_lifetime_ordinal);

  // This is called on Output ordering domain (FIDL thread) any time a flush is
  // seen.
  //
  // More complete protocol validation happens on StreamControl ordering domain.
  // The validation here is just to validate to degree needed to not break our
  // stream_queue_ and future_stream_lifetime_ordinal_.
  //
  // Returns true if it worked.  Returns false if FailLocked() has already been
  // called, in which case the caller probably wants to just return.
  __WARN_UNUSED_RESULT bool EnsureFutureStreamFlushSeenLocked(uint64_t stream_lifetime_ordinal);

  void StartIgnoringClientOldOutputConfig(std::unique_lock<std::mutex>& lock);

  void GenerateAndSendNewOutputConstraints(std::unique_lock<std::mutex>& lock,
                                           bool buffer_constraints_action_required);

  void MidStreamOutputConstraintsChange(uint64_t stream_lifetime_ordinal);

  bool FixupBufferCollectionConstraintsLocked(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings,
      fuchsia::sysmem::BufferCollectionConstraints* buffer_collection_constraints);

  void OnBufferCollectionInfo(CodecPort port, uint64_t buffer_lifetime_ordinal, zx_status_t status,
                              fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info);

  // When this method is called we know we're already on the correct thread per
  // the port.
  void OnBufferCollectionInfoInternal(
      CodecPort port, uint64_t buffer_lifetime_ordinal, zx_status_t allocate_status,
      fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info);

  // This is set if IsCoreCodecHwBased(), so CodecBuffer::Pin() can get the physical address info,
  // so DMA can be done directly from/to BufferCollection buffers.  We cache this just so we're not
  // constantly calling CoreCodecBti().
  zx::unowned_bti core_codec_bti_;

  fit::optional<FakeMapRange> fake_map_range_[kPortCount];

  // These are 1:1 with logical CodecBuffer(s).
  std::vector<std::unique_ptr<CodecBuffer>> all_buffers_[kPortCount];

  // For this bool to be true, there must be enough buffers in all_buffers_ and
  // the core codec must also be fully configured with regard to those buffers.
  bool is_port_buffers_configured_[kPortCount] = {};

  // This vector owns these buffers.
  //
  // TODO(dustingreen): Figure out if the HW has any particular interest in
  // packets (such as to avoid dynamic allocation to track queued parts of
  // buffers), or if HW portion of driver is fine just knowing about portions of
  // buffers.
  //
  // These are 1:1 with logical CodecPacket(s).
  std::vector<std::unique_ptr<CodecPacket>> all_packets_[kPortCount];

  //
  // Util aspects.
  //

  // For now, this is device_->driver()->shared_fidl_thread().  It could turn
  // out to be better to not share FIDL threads across Codec instances however.
  thrd_t fidl_thread();

  // Send OnFreeInputPacket() using shared_fidl_thread().  This can be called
  // on any thread other than shared_fidl_thread().
  void SendFreeInputPacketLocked(fuchsia::media::PacketHeader header);

  __WARN_UNUSED_RESULT bool IsInputConfiguredLocked();
  __WARN_UNUSED_RESULT bool IsOutputConfiguredLocked();
  __WARN_UNUSED_RESULT bool IsPortBuffersConfiguredCommonLocked(CodecPort port);

  // Either completely configured one way or another, or at least partially
  // configured using sysmem-style port settings.  Else the client isn't
  // behaving properly.
  __WARN_UNUSED_RESULT bool IsPortBuffersAtLeastPartiallyConfiguredLocked(CodecPort port);

  void vFail(bool is_fatal, const char* format, va_list args);
  void vFailLocked(bool is_fatal, const char* format, va_list args);

  void PostSerial(async_dispatcher_t* async, fit::closure to_run);
  // If |promise_not_on_previously_posted_fidl_thread_lambda| is true, the
  // caller is promising that it's not running in a lambda that was posted to
  // the fidl thread (running in a FIDL dispatch is fine).
  void PostToSharedFidl(fit::closure to_run);
  void PostToStreamControl(fit::closure to_run);
  __WARN_UNUSED_RESULT bool IsStoppingLocked();
  __WARN_UNUSED_RESULT bool IsStopping();

  __WARN_UNUSED_RESULT bool IsDecoder() const;
  __WARN_UNUSED_RESULT bool IsEncoder() const;
  __WARN_UNUSED_RESULT bool IsDecryptor() const;

  __WARN_UNUSED_RESULT const fuchsia::mediacodec::CreateDecoder_Params& decoder_params() const;
  __WARN_UNUSED_RESULT const fuchsia::mediacodec::CreateEncoder_Params& encoder_params() const;
  __WARN_UNUSED_RESULT const fuchsia::media::drm::DecryptorParams& decryptor_params() const;

  //
  // Core codec interfacing.
  //

  // true - maybe it's the core codec thread.
  // false - it's definitely not the core codec thread.
  __WARN_UNUSED_RESULT bool IsPotentiallyCoreCodecThread();

  void HandlePendingInputFormatDetails();

  // Only tell the core codec to ensure any current stream is stopped if
  // CoreCodecInit() was ever called.
  bool is_core_codec_init_called_ = false;

  bool is_core_codec_stream_started_ = false;

  //
  // For use by core codec:
  //

  // If the core codec needs to fail the whole CodecImpl, such as when/if new
  // FormatDetails are different than the initial FormatDetails and
  // the core codec doesn't support switching from the old to the new input
  // format details (for example due to needing different input buffer config).
  void onCoreCodecFailCodec(const char* format, ...) override;

  // The core codec should only call this method at times when there is a
  // current stream, not between streams.
  void onCoreCodecFailStream(fuchsia::media::StreamError error) override;

  void onCoreCodecResetStreamAfterCurrentFrame() override;

  // "Mid-stream" can mean at the start of a stream also - it's just required
  // that a stream be active currently.  The core codec must ensure that this
  // call is properly ordered with respect to onCoreCodecOutputPacket() and
  // onCoreCodecOutputEndOfStream() calls.
  //
  // A call to onCoreCodecMidStreamOutputConstraintsChange(true) must not be
  // followed by any more output (including EndOfStream) until the associated
  // output re-config is completed by a call to
  // CoreCodecMidStreamOutputBufferReConfigFinish().
  void onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) override;

  void onCoreCodecOutputFormatChange() override;

  void onCoreCodecInputPacketDone(CodecPacket* packet) override;

  void onCoreCodecOutputPacket(CodecPacket* packet, bool error_detected_before,
                               bool error_detected_during) override;

  void onCoreCodecOutputEndOfStream(bool error_detected_before) override;

  //
  // Core codec.
  //
  // These are here to cleanly do a few asserts as we call out to the
  // codec_adapter_, and to make call sites look a bit nicer.
  //

  __WARN_UNUSED_RESULT bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;

  __WARN_UNUSED_RESULT bool IsCoreCodecMappedBufferUseful(CodecPort port) override;

  __WARN_UNUSED_RESULT bool IsCoreCodecHwBased(CodecPort port) override;

  __WARN_UNUSED_RESULT zx::unowned_bti CoreCodecBti() override;

  void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) override;

  void CoreCodecSetSecureMemoryMode(
      CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) override;

  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override;

  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override;

  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override;

  void CoreCodecStartStream() override;

  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details) override;

  void CoreCodecQueueInputPacket(CodecPacket* packet) override;

  void CoreCodecQueueInputEndOfStream() override;

  void CoreCodecStopStream() override;

  void CoreCodecResetStreamAfterCurrentFrame() override;

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;

  void CoreCodecConfigureBuffers(CodecPort port,
                                 const std::vector<std::unique_ptr<CodecPacket>>& packets) override;

  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;

  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;

  __WARN_UNUSED_RESULT
  std::unique_ptr<const fuchsia::media::StreamBufferConstraints> CoreCodecBuildNewInputConstraints()
      override;

  __WARN_UNUSED_RESULT
  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> CoreCodecBuildNewOutputConstraints(
      uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
      bool buffer_constraints_action_required) override;

  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;

  void CoreCodecMidStreamOutputBufferReConfigFinish() override;

  CodecImpl() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecImpl);
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_IMPL_H_
