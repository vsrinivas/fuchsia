// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_IMPL_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_IMPL_H_

#include "codec_adapter.h"
#include "codec_adapter_events.h"
#include "codec_admission_control.h"
#include "codec_buffer.h"
#include "codec_packet.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/synchronization/thread_annotations.h>
#include <zircon/compiler.h>

#include <list>

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

class DeviceCtx;

class CodecImpl : public fuchsia::mediacodec::Codec,
                  public CodecAdapterEvents,
                  private CodecAdapter {
 public:
  CodecImpl(std::unique_ptr<CodecAdmission> codec_admission, DeviceCtx* device,
            std::unique_ptr<fuchsia::mediacodec::CreateDecoder_Params>
                video_decoder_params,
            fidl::InterfaceRequest<fuchsia::mediacodec::Codec> codec_request);
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
  // gets a Codec failure having overriden the input format on a stream of a
  // Codec such that the stream's input format doesn't exactly match the Codec's
  // input format (at least for now).
  void SetCoreCodecAdapter(std::unique_ptr<CodecAdapter> codec_adapter);

  // BindAsync()
  //
  // This enables serving Codec (soon).  Up to the start of this call,
  // ~CodecImpl() is fine to just call.  After the start of this call, Unbind()
  // must fully complete before ~CodecImpl.
  //
  // The core codec initialization and actual binding occur shortly later async
  // after the start of this call, possibly after this call has returned.  This
  // is to avoid core codec initialization slowing down the shared_fidl_thread()
  // which may be handling other stream data for a different CodecImpl instance.
  //
  // TODO(dustingreen): If stuff like the previous paragraph becomes too onerous
  // we can potentially switch to not sharing one FIDL thread across CodecImpl
  // instances - for now it still seems plausible enough, but we'll want to
  // re-visit this choice when we have this class working and can run a couple
  // decodes concurrently and see how well it runs.  There are of course core
  // codec sharing aspects also such as switching the decoder HW between two
  // streams - if those aspects tend to be even longer interference intervals
  // than having a single shared_fidl_thread() causes, then sharing one
  // shared_fidl_thread() is probably fine for now.
  //
  // Any error, including those encountered before binding is fully complete,
  // will call error_handler on a clean stack on shared_fidl_thread(), after
  // this call (also on shared_fidl_thread()) returns.
  void BindAsync(fit::closure error_handler);

  //
  // Codec interface
  //
  void EnableOnStreamFailed() override;
  void SetInputBufferSettings(
      fuchsia::mediacodec::CodecPortBufferSettings input_settings) override;
  void AddInputBuffer(fuchsia::mediacodec::CodecBuffer buffer) override;
  void SetOutputBufferSettings(
      fuchsia::mediacodec::CodecPortBufferSettings output_settings) override;
  void AddOutputBuffer(fuchsia::mediacodec::CodecBuffer buffer) override;
  void FlushEndOfStreamAndCloseStream(
      uint64_t stream_lifetime_ordinal) override;
  void CloseCurrentStream(uint64_t stream_lifetime_ordinal,
                          bool release_input_buffers,
                          bool release_output_buffers) override;
  void Sync(SyncCallback callback) override;
  void RecycleOutputPacket(
      fuchsia::mediacodec::CodecPacketHeader available_output_packet) override;
  void QueueInputFormatDetails(
      uint64_t stream_lifetime_ordinal,
      fuchsia::mediacodec::CodecFormatDetails format_details) override;
  void QueueInputPacket(fuchsia::mediacodec::CodecPacket packet) override;
  void QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) override;

 private:
  // For FailFatalLocked().
  //
  // Tradeoff: sharing more code vs. having a fatal error not depend on calling
  // through CodecImpl.
  friend class CodecBuffer;

  // We keep a queue of Stream objects rather than just a single current stream
  // object, so we can track which streams are future-discarded and which are
  // not yet known to be future-discarded.  This difference matters because
  // clients are not required to process OnOutputConfig() with
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
    void SetInputFormatDetails(
        std::unique_ptr<fuchsia::mediacodec::CodecFormatDetails>
            input_format_details);
    // Can be nullptr if no per-stream details have been set, in which case the
    // caller should look at CodecImpl::initial_input_format_details_
    // instead.  The returned pointer is only valid up until the next call to to
    // SetInputFormatDetails() or when the stream is deleted, whichever comes
    // first.  This is only meant to be called on stream_control_thread_.
    const fuchsia::mediacodec::CodecFormatDetails* input_format_details();
    // We send codec_oob_bytes (if any) to the core codec just before sending a
    // packet to the core codec, but only when the stream has OOB data pending.
    // A new stream has OOB data initially pending, and it becomes pending again
    // if SetInputFormatDetails() is used and the codec_oob_bytes don't match
    // the effective codec_oob_bytes before.  This way we avoid causing extra
    // input format changes for the core codec.
    void SetOobConfigPending(bool pending);
    __WARN_UNUSED_RESULT bool oob_config_pending();
    void SetInputEndOfStream();
    __WARN_UNUSED_RESULT bool input_end_of_stream();
    void SetOutputEndOfStream();
    __WARN_UNUSED_RESULT bool output_end_of_stream();

   private:
    const uint64_t stream_lifetime_ordinal_ = 0;
    bool future_discarded_ = false;
    bool future_flush_end_of_stream_ = false;
    // Starts as nullptr for each new stream with implicit fallback to
    // initial_input_format_details_, but can be overriden on a per-stream basis
    // with QueueInputFormatDetails().
    std::unique_ptr<fuchsia::mediacodec::CodecFormatDetails>
        input_format_details_;
    // This defaults to _true_, so that we send the OOB bytes to the HW for each
    // stream, if we have any codec_oob_bytes to send.
    bool oob_config_pending_ = true;
    bool input_end_of_stream_ = false;
    bool output_end_of_stream_ = false;
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

  // Parts of CodecImpl are accessed from shared_fidl_thread(),
  // stream_control_thread_, and decoder thread(s) such as interrupt handling
  // thread(s).
  std::mutex lock_;

  //
  // Setup/teardown aspects.
  //

  // Will send an initial Codec.OnOutputConfig() if the codec can't tolerate
  // null output config during format detection.
  void onInputConstraintsReady();

  // This starts unbinding.  When unbinding is done and CodecImpl is ready to
  // be destructed, client_error_handler_ is called.
  //
  // UnbindLocked() can be called in response to a channel error (in which case
  // the binding_ itself is already unbound), or can be called in response to a
  // protocol error.  It can be called on any thread.
  //
  // On the caller's release of lock_ after this call, "this" may be
  // deallocated, if UnbindLocked() was called on a thread other than
  // shared_fidl_thread().  For consistency and simplicity, all callers should
  // avoid touching any part of "this" after return from this method other than
  // releasing lock_.
  //
  // All calls to UnbindLocked() are some form of error; the
  // client_error_handler_ will eventually run async later at some time after
  // the start of this call.
  //
  // Most potential callers probably want Fail() or FailLocked() instead, which
  // report an error before calling UnbindLocked() at the end..
  void UnbindLocked();
  // Like UnbindLocked(), but acquires the lock so the caller doesn't have to.
  // On return from this method, "this" can be deallocated.
  void Unbind();

  // TODO(dustingreen): Factor out the dependency on DeviceCtx so that CodecImpl
  // can be separate.
  DeviceCtx* device_ = nullptr;

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

  // Using unique_ptr<> for its optional-ness here.
  const std::unique_ptr<const fuchsia::mediacodec::CreateDecoder_Params>
      decoder_params_;

  // Regardless of which type of codec was created, these track the input
  // CodecFormatDetails.
  //
  // We keep a copy of the format details used to create the codec, and on a
  // per-stream basis those details are used as the default details, but can be
  // overridden with QueueInputFormatDetails().  A new stream will default back
  // to the CodecFormatDetails used to create the codec unless that stream uses
  // QueueInputFormatDetails().  The QueueInputFormatDetails() is not persistent
  // across streams.
  //
  // The codec_oob_bytes field can be null if the codec type or specific format
  // does not require codec_oob_bytes.
  //
  // This points directly to a field of decoder_params_ (or encoder_params_),
  // which out-last all usages of this pointer.
  const fuchsia::mediacodec::CodecFormatDetails* initial_input_format_details_;

  // Held here temporarily until DeviceFidl is ready to handle errors so we can
  // bind.
  fidl::InterfaceRequest<fuchsia::mediacodec::Codec> tmp_interface_request_;

  // This binding doesn't channel-own this CodecImpl.  The DeviceFidl owns all
  // the CodecImpl(s).  The DeviceFidl will SetErrorHandler() such that its
  // ownership drops if the channel fails.  The CodecImpl takes care of cleaning
  // itself up before calling the DeviceFidl's error handler, so that CodecImpl
  // is ready for destruction by the time DeviceFidl's error handler is called.
  fidl::Binding<fuchsia::mediacodec::Codec, CodecImpl*> binding_;
  bool was_bind_async_called_ = false;
  // This being true means BindAsync() reached the point where we can and must
  // fail via UnbindLocked() instead of just running the owner's error handler
  // directly.
  bool was_logically_bound_ = false;
  async::Loop stream_control_loop_;
  thrd_t stream_control_thread_ = 0;
  fit::closure owner_error_handler_;
  bool was_unbind_started_ = false;
  bool was_unbind_completed_ = false;
  std::condition_variable wake_stream_control_condition_;

  //
  // Codec protocol aspects.
  //

  // Some of the FIDL messages get handled or partly handled on the
  // StreamControl thread.
  void SetInputBufferSettings_StreamControl(
      fuchsia::mediacodec::CodecPortBufferSettings input_settings);
  void AddInputBuffer_StreamControl(fuchsia::mediacodec::CodecBuffer buffer);
  void FlushEndOfStreamAndCloseStream_StreamControl(
      uint64_t stream_lifetime_ordinal);
  void CloseCurrentStream_StreamControl(uint64_t stream_lifetime_ordinal,
                                        bool release_input_buffers,
                                        bool release_output_buffers);
  void Sync_StreamControl(SyncCallback callback);
  void QueueInputFormatDetails_StreamControl(
      uint64_t stream_lifetime_ordinal,
      fuchsia::mediacodec::CodecFormatDetails format_details);
  void QueueInputPacket_StreamControl(fuchsia::mediacodec::CodecPacket packet);
  void QueueInputEndOfStream_StreamControl(uint64_t stream_lifetime_ordinal);

  __WARN_UNUSED_RESULT bool IsStreamActiveLocked();
  void SetBufferSettingsCommonLocked(
      CodecPort port,
      const fuchsia::mediacodec::CodecPortBufferSettings& settings,
      const fuchsia::mediacodec::CodecBufferConstraints& constraints);
  void EnsureBuffersNotConfiguredLocked(CodecPort port);
  // Returns true if validation passed.  Returns false if validation failed and
  // FailLocked() has already been called with a specific error string (in which
  // case the caller will likely want to just return).
  __WARN_UNUSED_RESULT bool ValidateBufferSettingsVsConstraintsLocked(
      CodecPort port,
      const fuchsia::mediacodec::CodecPortBufferSettings& settings,
      const fuchsia::mediacodec::CodecBufferConstraints& constraints);

  // Returns true if the port is done configuring (last buffer was added).
  // Returns false if the port is not done configuring or if Fail() was called;
  // currently the caller doesn't need to tell the difference between these two
  // very different cases.
  __WARN_UNUSED_RESULT bool AddBufferCommon(
      CodecPort port, fuchsia::mediacodec::CodecBuffer buffer);

  // Return value of false means FailLocked() has already been called.
  __WARN_UNUSED_RESULT bool CheckOldBufferLifetimeOrdinalLocked(
      CodecPort port, uint64_t buffer_lifetime_ordinal);

  // Return value of false means FailLocked() has already been called.
  __WARN_UNUSED_RESULT bool CheckStreamLifetimeOrdinalLocked(
      uint64_t stream_lifetime_ordinal);

  // Return value of false means FailLocked() has already been called.
  __WARN_UNUSED_RESULT bool StartNewStream(std::unique_lock<std::mutex>& lock,
                                           uint64_t stream_lifetime_ordinal);
  void EnsureStreamClosed(std::unique_lock<std::mutex>& lock);
  void EnsureCodecStreamClosedLockedInternal();

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
  // OnOutputConfig() for that stream.  If the stream has been discarded, then
  // StreamControl ordering domain cannot expect the client to ever process
  // OnOutputConfig() for the stream, and the StreamControl ordering domain can
  // instead move on to the next stream.
  //
  // In addition, this can allow the StreamControl ordering domain to skip past
  // stream-specific items for a stream that's already known to be discarded by
  // the client.
  std::list<std::unique_ptr<Stream>> stream_queue_;
  // When no current stream, this is nullptr.  When there is a current stream,
  // this points to that stream, owned by stream_queue_.
  Stream* stream_ = nullptr;

  std::unique_ptr<const fuchsia::mediacodec::CodecBufferConstraints>
      input_constraints_;

  // This is the most recent settings recieved from the client and accepted,
  // received via SetInputBufferSettings() or SetOutputBufferSettings().  The
  // settings are as-received from the client.
  std::unique_ptr<const fuchsia::mediacodec::CodecPortBufferSettings>
      port_settings_[kPortCount];

  // The most recent fully-configured input or output buffers had this
  // buffer_constraints_version_ordinal.  Even when !port_settings_[port], this
  // is used to detect whether the client has yet caught up to the
  // last_required_buffer_constraints_version_ordinal_[port].
  uint64_t last_provided_buffer_constraints_version_ordinal_[kPortCount] = {};

  // For CodecImpl, the initial CodecOutputConfig can be the first sent message.
  // If sent that early, the CodecOutputConfig is likely to change again before
  // any output data is emitted, but it _may not_.
  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig> output_config_;

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
  // For the OMX adapter, if the buffer constraints change, then the format
  // details ordinal also changes (since there's not really any benefit to
  // detecting lack of change).  But for format-only changes that don't require
  // buffer re-allocation, we can just increment the format details ordinal.
  uint64_t next_output_format_details_version_ordinal_ = 1;

  // Separately from ordinal allocation, we track the most recent ordinal that
  // we've actually sent to the client, to allow tigher protocol enforcement in
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

  //
  // Adapter-related
  //
  // TODO(dustingreen): Try to generalize this section more or move anything
  // core-codec-specific to a different class, to make fully common between HW
  // and OMX cases at least, if not fully general.
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
  __WARN_UNUSED_RESULT bool EnsureFutureStreamSeenLocked(
      uint64_t stream_lifetime_ordinal);

  // This is called on Output ordering domain (FIDL thread) any time a message
  // is received which would close a stream.
  //
  // More complete protocol validation happens on StreamControl ordering domain.
  // The validation here is just to validate to degree needed to not break our
  // stream_queue_ and future_stream_lifetime_ordainal_.
  //
  // Returns true if it worked.  Returns false if FailLocked() has already been
  // called, in which case the caller probably wants to just return.
  __WARN_UNUSED_RESULT bool EnsureFutureStreamCloseSeenLocked(
      uint64_t stream_lifetime_ordinal);

  // This is called on Output ordering domain (FIDL thread) any time a flush is
  // seen.
  //
  // More complete protocol validation happens on StreamControl ordering domain.
  // The validation here is just to validate to degree needed to not break our
  // stream_queue_ and future_stream_lifetime_ordainal_.
  //
  // Returns true if it worked.  Returns false if FailLocked() has already been
  // called, in which case the caller probably wants to just return.
  __WARN_UNUSED_RESULT bool EnsureFutureStreamFlushSeenLocked(
      uint64_t stream_lifetime_ordinal);

  void StartIgnoringClientOldOutputConfigLocked();

  void GenerateAndSendNewOutputConfig(std::unique_lock<std::mutex>& lock,
                                      bool buffer_constraints_action_required);

  void onStreamFailed_StreamControl(uint64_t stream_lifetime_ordinal);

  void MidStreamOutputConfigChange(uint64_t stream_lifetime_ordinal);

  // These are 1:1 with logical CodecBuffer(s).
  std::vector<std::unique_ptr<CodecBuffer>> all_buffers_[kPortCount];

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
  void SendFreeInputPacketLocked(fuchsia::mediacodec::CodecPacketHeader header);

  __WARN_UNUSED_RESULT bool IsInputConfiguredLocked();
  __WARN_UNUSED_RESULT bool IsOutputConfiguredLocked();
  __WARN_UNUSED_RESULT bool IsPortConfiguredCommonLocked(CodecPort port);

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

  void vFail(bool is_fatal, const char* format, va_list args);
  void vFailLocked(bool is_fatal, const char* format, va_list args);

  void PostSerial(async_dispatcher_t* async, fit::closure to_run);
  void PostToSharedFidl(fit::closure to_run);
  void PostToStreamControl(fit::closure to_run);
  __WARN_UNUSED_RESULT bool IsStoppingLocked();
  __WARN_UNUSED_RESULT bool IsStopping();

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
  // CodecFormatDetails are different than the initial CodecFormatDetails and
  // the core codec doesn't support switching from the old to the new input
  // format details (for example due to needing different input buffer config).
  void onCoreCodecFailCodec(const char* format, ...) override;

  // The core codec should only call this method at times when there is a
  // current stream, not between streams.
  void onCoreCodecFailStream() override;

  // "Mid-stream" can mean at the start of a stream also - it's just required
  // that a stream be active currently.  The core codec must ensure that this
  // call is propertly ordered with respect to onCoreCodecOutputPacket() and
  // onCoreCodecOutputEndOfStream() calls.
  //
  // A call to onCoreCodecMidStreamOutputConfigChange(true) must not be
  // followed by any more output (including EndOfStream) until the associated
  // output re-config is completed by a call to
  // CoreCodecMidStreamOutputBufferReConfigFinish().
  void onCoreCodecMidStreamOutputConfigChange(
      bool output_re_config_required) override;

  void onCoreCodecInputPacketDone(const CodecPacket* packet) override;

  void onCoreCodecOutputPacket(CodecPacket* packet, bool error_detected_before,
                               bool error_detected_during) override;

  void onCoreCodecOutputEndOfStream(bool error_detected_before) override;

  //
  // Core codec.
  //
  // These are here to cleanly do a few asserts as we call out to the
  // codec_adapter_, and to make call sites look a bit nicer.
  //

  __WARN_UNUSED_RESULT bool IsCoreCodecRequiringOutputConfigForFormatDetection()
      override;

  void CoreCodecInit(const fuchsia::mediacodec::CodecFormatDetails&
                         initial_input_format_details) override;

  void CoreCodecStartStream(std::unique_lock<std::mutex>& lock) override;

  void CoreCodecQueueInputFormatDetails(
      const fuchsia::mediacodec::CodecFormatDetails&
          per_stream_override_format_details) override;

  void CoreCodecQueueInputPacket(const CodecPacket* packet) override;

  void CoreCodecQueueInputEndOfStream() override;

  void CoreCodecStopStream(std::unique_lock<std::mutex>& lock) override;

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;

  void CoreCodecConfigureBuffers(CodecPort port) override;

  void CoreCodecRecycleOutputPacketLocked(CodecPacket* packet) override;

  void CoreCodecEnsureBuffersNotConfiguredLocked(CodecPort port) override;

  __WARN_UNUSED_RESULT
  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
  CoreCodecBuildNewOutputConfig(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      uint64_t new_output_format_details_version_ordinal,
      bool buffer_constraints_action_required) override;

  void CoreCodecMidStreamOutputBufferReConfigPrepare(
      std::unique_lock<std::mutex>& lock) override;

  void CoreCodecMidStreamOutputBufferReConfigFinish(
      std::unique_lock<std::mutex>& lock) override;

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CodecImpl);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_IMPL_H_
