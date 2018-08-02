// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_OMX_CODEC_RUNNER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_OMX_CODEC_RUNNER_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "codec_runner.h"

#include <lib/async-loop/cpp/loop.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

#include "OMX_Component.h"
#include "OMX_Core.h"

#include <list>

namespace codec_runner {

// The OmxCodecRunner is an implementation of CodecRunner (and of Codec) which
// loads and uses an OMX codec .so lib to perform processing.

// The OMX spec is a bit wishy-washy when it comes to threading.  As a general
// rule (with zero known exceptions), we don't hold lock_ while calling OMX.
// The only calls to OMX where we know holding lock would be a problem for
// SimpleSoftOMXComponent.cpp are UseBuffer, AllocateBuffer, FreeBuffer as those
// call directly back into EventHandler on the same thread that calls them.  But
// for other OMX codec implementations, the OMX spec would permit calls directly
// back to EventHandler from _any_ call to OMX, AFAICT.  So we don't hold lock_
// when calling into OMX.
//
// We ensure proper ordering of calls to OMX using a combination of restricting
// which "ordering domain" can make a given call + queueing some calls to OMX
// (with ordering preserved) while holding lock_ but making the actual call to
// OMX outside lock_.
//
// In particular, we always queue calls to FillThisBuffer and SendCommand, even
// though SimpleSoftOMXComponent.cpp also posts them internally.  We need to
// ensure the SendCommand happens after the FillThisBuffer, and holding a lock
// while queueing them is an easy way to ensure the later-queued SendCommand
// will be after the earlier-queued FillThisBuffer, but rather than rely on the
// codec to queue them internally, we take the OMX spec seriously when it says a
// codec might just do everything immediately on the incoming thread, so we
// queue ourselves.  But...  For EmptyThisBuffer(), we don't queue ourselves.
// Instead, we know that all calls to EmptyThisBuffer() and SendCommand() are
// initiated by the StreamControl ordering domain, so we know that an
// EmptyThisBuffer() call to OMX from StreamControl will happen before a
// SendCommand() queued later by the StreamControl domain.  We don't care that
// the relative ordering of FillThisBuffer() and EmptyThisBuffer() calls is not
// preserved.

// For some mime type + omx codec lib combos, we might end up with a derived
// class implementation (derived from OmxCodecRunner) which overrides
// OmxCodecRunner behavior enough to work around the known open issues in an omx
// codec's handling of a particular format - the currently-known example is the
// OMX AAC decoder not handling split ADTS headers, and requiring an OOB codec
// config to be synthesized despite all needed information being present in-band
// in ADTS.
//
// This class expects the creating code to bind an instance of this class to
// a Codec interface request.  This class expects that binding to use the same
// async_t that's provided to the constructor of this class.
//
// These are the threads relevant to understanding this class:
//   * Caller of Load() + SetAudioDecoderParams() (or analogous).  Need not be
//     the FIDL thread, but we do expect these to be called in order and
//     complete before binding to the channel.
//   * "FIDL thread" - the thread backing the async_t passed to the constructor.
//     Obviously it's safe for this thread to call Codec methods implemented by
//     this class.  This class also uses the same thread for all sends to the
//     Codec channel.  We also use this thread to directly push the OMX codec
//     through state changes including waiting for those state changes to take
//     effect.  The direct state pushing on this thread is not expected to ever
//     take a long duration (except for FlushEndOfStreamAndCloseStream(),
//     which seems like a reasonable exception given that message's purpose),
//     and is not doing anything that would require round-trips to other
//     processes.  Input packets are the only queued work; aside from input
//     packets, there's not any additional queue of incoming messages after the
//     incoming channel messages.  There might be some minor Codec
//     responsiveness benefits to queueing state change work to a separate
//     state-driving thread, to do with being able to notice that
//     previously-dequeued messages can be safely ignored/skipped, but at the
//     moment any such benefits seem unlikely to be signficant enough to justify
//     the complexity increase that would imply.
//   * "OMX thread" - The OMX codec has its own primary thread.  Calls to
//     EventHandler() method of this class can come from the OMX thread or from
//     the FIDL thread (during calls into the OMX codec from the FIDL thread).
//     Since we want to use the async_t thread to push state changes of the OMX
//     codec, we can't post back to the async_t thread to handle events, as
//     processing events is part of OMX state changes.  The
//     callback-on-same-thread behavior of the OMX codec means we can't be
//     holding lock_ when calling into the OMX codec, as we need EventHandler()
//     to be able to acquire lock_ as needed.  Some OMX codecs use the OMX
//     primary thread as the data processing thread, so it's important that we
//     not stall the OMX thread by doing any long-duration work in
//     EventHandler().  The OMX spec also essentially says that EventHandler()
//     must return quickly.
//   * Any additional secondary threads created internally by the OMX codec.
//     These are for concurrent data processing and don't interact directly with
//     OmxCodecRunner.
//
// Queueing of input data and output data helps pipeline and avoids stalling
// data processing thread(s) unecessarily. Input packets arrive ordered on the
// channel and we call the OMX codec directly from there to queue those input
// packets over to the OMX primary thread.  Output data is emitted in order from
// the OMX codec using the OMX primary thread - we queue first to the async_t
// thread by posting a lambda (ordered with respect to others posted the same
// way), then that posted lambda sends an output message to the channel.
//
// TODO(dustingreen): We may be able to avoid posting emitted output over to the
// async_t thread for sending once we know how event sending works and what it
// does or doesn't guarantee.  For now we post over there to avoid being fragile
// across any event-related changes.

// Handling of OMX_EventPortSettingsChanged:
//
// It appears OMX is under-specified here.  In practice it looks like nData2
// being 0 or OMX_IndexParamPortDefinition means the OMX codec will be waiting
// for a port disable/enable before delivering more output data, else it'll
// keep delivering output data without interruption.
//
// The codec waiting for a disable/enable is how we know whether
// buffer_constraints_action_required.
//
// Either way, the output format may have changed, so we'll generate an
// OnOutputConfig() message in-order with respect to output data.
//
// The way that we achieve an output-ordered OnOutputConfig() varies depending
// on whether action is required.
//
// If action is not required, we just post over to the Output ordering domain
// (Codec FIDL thread) like we would for any emitted output packet.
//
// If action is required, we know the OMX codec will not be generating any
// subsequent output until action is taken, and we know action won't be taken
// until the client has caught up with this most recent config which we haven't
// sent to the client yet, and we know any fakery by the client that appears to
// be catching up to this most recent config impossibly soon will be blocked by
// protocol checking, because we haven't yet sent this latest config to the
// client.  We also must ensure that we don't send the OnOutputConfig() until
// _after_ we've de-configured the output buffers server-side.  We can also
// double check that the OMX codec is following the rules with regard to not
// trying to generate more output until action is taken.  So, in this case we
// can post over to the StreamControl ordering domain, and from there, if the
// output config is still relevant to the current stream, we can disable the
// output port and de-configure output buffers before posting over to the Output
// ordering domain to send the OnOutputConfig() message.
//
// Because the codec is free to _immediately_ change the output config again the
// moment we return from EventHandler(), we are forced to collect the relevant
// output config data from the OMX codec immediately during the call to
// EventHandler() - there is no other valid option.
//
// As with emitted output packets, the CodecOutputConfig has a
// stream_lifetime_ordinal, and the client is free to ignore a CodecOutputConfig
// with stale stream_lifetime_ordinal (though this can increase latency to
// emitted output data in some cases).  We rely on the OMX codec resetting its
// "mOutputPortSettingsChange AWAITING_DISABLED / AWAITING_ENABLED" state when
// the codec runs onReset() on dropping to OMX loaded state between streams, and
// we put the output port back to enabled state (despite it having no buffers at
// the time, which OMX is ok with when in OMX loaded state), but we need to give
// the OMX codec output buffers before we can put the codec back to OMX
// executing state and feed input.  This means we have to generate a new
// OnConfigChange() for the new stream without any triggering
// OMX_EventPortSettingsChanged if we get input data for a new stream without a
// complete output config yet.  This is very similar to the situation we're in
// when the Codec has just been created and we see input data before the client
// has finished configuring the output, so we treat the two situations the same
// way.
//
// As with emitted output packets, we don't necessarily need to generate or send
// a config if we know that the StreamControl ordering domain has already begun
// shutting down the current stream.  We know the StreamControl ordering domain
// hasn't finished shutting down the current stream by the fact that we're still
// running in EventHandler().  While it doesn't necessarily matter which we
// choose to do, what does matter is having a valid stream_lifetime_ordinal on
// any generated messages.  So if StreamControl has moved on to an even-numbered
// stream_lifetime_ordinal already, we can just elide any emitted output packet
// or output config rather than send a message with an even-numbered
// stream_lifetime_ordinal.
//
// The setting of buffer_constraints_action_required true vs false is driven
// directly from whether nData2 in OMX_EventPortSettingsChanged is 0 or is
// OMX_IndexParamPortDefinition.  If so, then action is required.
class OmxCodecRunner : public CodecRunner {
 public:
  OmxCodecRunner(async_dispatcher_t* fidl_dispatcher, thrd_t fidl_thread,
                 std::string_view mime_type, std::string_view lib_filename);

  //
  // CodecRunner
  //

  bool Load() override;

  // Only one of these is called, corresponding to which codec type was
  // requested via CodecFactory.
  void SetDecoderParams(
      fuchsia::mediacodec::CreateDecoder_Params decoder_params) override;
  // TODO(dustingreen):
  // virtual void SetAudioEncoderParams(...) override;
  // virtual void SetVideoEncoderParams(...) override;
  // (or combined)

  // These are called by CodecRunner at the appropriate times.
  void ComputeInputConstraints() override;
  void onInputConstraintsReady() override;
  void onSetupDone() override;

  //
  // Codec
  //

  void EnableOnStreamFailed() override;
  void SetInputBufferSettings(
      fuchsia::mediacodec::CodecPortBufferSettings input_settings) override;
  void SetInputBufferSettings_StreamControl(
      fuchsia::mediacodec::CodecPortBufferSettings input_settings);

  void AddInputBuffer(fuchsia::mediacodec::CodecBuffer buffer) override;
  void AddInputBuffer_StreamControl(fuchsia::mediacodec::CodecBuffer buffer);
  void SetOutputBufferSettings(
      fuchsia::mediacodec::CodecPortBufferSettings output_settings) override;
  void AddOutputBuffer(fuchsia::mediacodec::CodecBuffer buffer) override;
  void FlushEndOfStreamAndCloseStream(
      uint64_t stream_lifetime_ordinal) override;
  void FlushEndOfStreamAndCloseStream_StreamControl(
      uint64_t stream_lifetime_ordinal);
  void CloseCurrentStream(uint64_t stream_lifetime_ordinal,
                          bool release_input_buffers,
                          bool release_output_buffers) override;
  void CloseCurrentStream_StreamControl(uint64_t stream_lifetime_ordinal,
                                        bool release_input_buffers,
                                        bool release_output_buffers);
  void Sync(SyncCallback callback) override;
  void Sync_StreamControl(SyncCallback callback);
  void RecycleOutputPacket(
      fuchsia::mediacodec::CodecPacketHeader available_output_packet) override;
  void QueueInputFormatDetails(
      uint64_t stream_lifetime_ordinal,
      fuchsia::mediacodec::CodecFormatDetails format_details) override;
  void QueueInputFormatDetails_StreamControl(
      uint64_t stream_lifetime_ordinal,
      fuchsia::mediacodec::CodecFormatDetails format_details);
  void QueueInputPacket(fuchsia::mediacodec::CodecPacket packet) override;
  void QueueInputPacket_StreamControl(fuchsia::mediacodec::CodecPacket packet);
  void QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) override;
  void QueueInputEndOfStream_StreamControl(uint64_t stream_lifetime_ordinal);

 protected:
  // Constants for indexing into our own member variable arrays.
  using Port = uint32_t;
  static constexpr uint32_t kFirstPort = 0;
  static constexpr uint32_t kInput = 0;
  static constexpr uint32_t kOutput = 1;
  static constexpr uint32_t kPortCount = 2;

  // TODO(dustingreen): maybe supporting non-VMO buffers would justify having a
  // base class + a factory method in OmxCodecRunner maybe.
  //
  // These are tracked via shared_ptr<const Buffer>, to homogenize
  // buffer-per-packet mode vs. single-buffer mode.
  //
  // These are 1:1 with Codec buffers, but not necessarily 1:1 with OMX
  // "buffers".
  class Buffer {
   public:
    Buffer(OmxCodecRunner* parent, Port port,
           fuchsia::mediacodec::CodecBuffer buffer);
    ~Buffer();
    bool Init(bool input_require_write = false);

    uint64_t buffer_lifetime_ordinal() const;

    uint32_t buffer_index() const;

    uint8_t* buffer_base() const;

    size_t buffer_size() const;

   private:
    // The parent OmxCodecRunner instance.  Just so we can call parent_->Exit().
    // The parent_ OmxCodecRunner out-lives the OmxCodecRunner::Buffer.
    OmxCodecRunner* parent_;
    Port port_ = 0;
    // This msg still has the live vmo_handle.
    fuchsia::mediacodec::CodecBuffer buffer_;
    // This accounts for vmo_offset_begin.  The content bytes are not part of
    // a Buffer instance from a const-ness point of view.
    uint8_t* buffer_base_ = nullptr;
  };

  // OMX buffers are most closely analogous to Codec packets, so we call these
  // "Packet" despite them being 1:1 with OMX "buffers" while in OMX_StateIdle
  // or OMX_StateExecuting.  Our Codec buffers are represented by Buffer above.
  //
  // While a Packet instance continues to exist from stream to stream, an OMX
  // buffer header does not, because the only way to reset an OMX codec is to
  // drop it all the way to OMX_StateLoaded (OMX_CommandFlush isn't enough), and
  // the only way to get to OMX_StateLoaded is to call OMX FreeBuffer on each
  // OMX buffer header.
  class Packet {
   public:
    // The buffer ptr is not owned.  The buffer lifetime is slightly longer than
    // the Packet lifetime.
    Packet(uint64_t buffer_lifetime_ordinal, uint32_t packet_index,
           Buffer* buffer);

    uint64_t buffer_lifetime_ordinal() const;

    uint32_t packet_index() const;

    const Buffer& buffer() const;

    // This can be called more than once, but must always either be moving from
    // nullptr to non-nullptr, or from non-nullptr to nullptr.  This pointer is
    // not owned T lifetime of the omx_header pointer.
    void SetOmxHeader(OMX_BUFFERHEADERTYPE* omx_header);

    OMX_BUFFERHEADERTYPE* omx_header() const;

   private:
    uint64_t buffer_lifetime_ordinal_ = 0;
    uint32_t packet_index_ = 0;

    // not owned
    Buffer* buffer_ = nullptr;

    // not directly owned here - the caller uses this field as a place to stash
    // this header while the caller owns the header
    OMX_BUFFERHEADERTYPE* omx_header_ = nullptr;

    FXL_DISALLOW_COPY_AND_ASSIGN(Packet);
  };

  // We keep a queue of Stream objects rather than just a single current stream
  // object, so we can track which streams are future-discarded and which are
  // not yet known to be future-discarded.  This difference matters because
  // clients are not required to process OnOutputConfig() with
  // stream_lifetime_ordinal of a stream that the client has since told the
  // server to discard, so we don't want StreamControl ordering domain getting
  // stuck waiting on a client to catch up to an output config that the client
  // won't process.  Instead, the StreamControl ordering domain can ignore any
  // additional messages related to the discarded stream until the stream
  // discarding message is reached at which point the OMX codec's mid-stream
  // output config change is cancelled/forgotten when we move the OMX codec to
  // OMX loaded state.
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
    // These mutations occur in Output ordering domain (FIDL thread):
    explicit Stream(uint64_t stream_lifetime_ordinal);
    uint64_t stream_lifetime_ordinal();
    void SetFutureDiscarded();
    bool future_discarded();
    void SetFutureFlushEndOfStream();
    bool future_flush_end_of_stream();

    // These mutations occur in StreamControl ordering domain:
    ~Stream();
    // This can be called 0-N times for a given stream, and each call replaces
    // any previously-set details.
    void SetInputFormatDetails(
        std::unique_ptr<fuchsia::mediacodec::CodecFormatDetails>
            input_format_details);
    // Can be nullptr if no per-stream details have been set, in which case the
    // caller should look at OmxCodecRunner::initial_input_format_details_
    // instead.  The returned pointer is only valid up until the next call to to
    // SetInputFormatDetails() or when the stream is deleted, whichever comes
    // first.  This is only meant to be called on stream_control_thread_.
    const fuchsia::mediacodec::CodecFormatDetails* input_format_details();
    // We send codec_oob_bytes (if any) to the OMX codec just before sending a
    // packet to the OMX codec, but only when the stream has OOB data pending.
    // A new stream has OOB data initially pending, and it becomes pending again
    // if SetInputFormatDetails() is used and the codec_oob_bytes don't match
    // the effective codec_oob_bytes before.  This way we avoid causing extra
    // OMX_EventPortSettingsChanged(s).
    void SetOobConfigPending(bool pending);
    bool oob_config_pending();
    void SetInputEndOfStream();
    bool input_end_of_stream();
    void SetOutputEndOfStream();
    bool output_end_of_stream();

   private:
    const uint64_t stream_lifetime_ordinal_ = 0;
    bool future_discarded_ = false;
    bool future_flush_end_of_stream_ = false;
    // Starts as nullptr for each new stream with implicit fallback to
    // initial_input_format_details_, but can be overriden on a per-stream basis
    // with QueueInputFormatDetails().
    std::unique_ptr<fuchsia::mediacodec::CodecFormatDetails>
        input_format_details_;
    // This defaults to _true_, so that we send a OMX_BUFFERFLAG_CODECCONFIG
    // buffer to OMX for each stream, if we have any codec_oob_bytes to send.
    bool oob_config_pending_ = true;
    bool input_end_of_stream_ = false;
    bool output_end_of_stream_ = false;
  };

  // TODO(dustingreen): For now this is essentially just a combined version of
  // the OMX format structures for audio and video, but this is not how we want
  // the codec interface to describe format (at least not in terms of the field
  // names and inner struct names if nothing else), so this won't be the way the
  // client sees the format.
  struct OMX_GENERIC_PORT_FORMAT {
    // While this is the same structure as out_port_def_, for uncompressed video
    // output at least, it's important that this copy is filled out after the
    // output port format has changed so that dimensions etc are known.  For
    // audio it's less important when this is filled out but we try to treat
    // video and audio similarly where it's reasonable to do so.
    OMX_PARAM_PORTDEFINITIONTYPE definition;
    // Depends on which definition.eDomain, so never need more than one of these
    // concurrently.
    union {
      struct {
        // Mainly for the eEncoding field.
        OMX_AUDIO_PARAM_PORTFORMATTYPE format;
        // Depends on which format.eEncoding, so never need more than one of
        // these concurrently.
        union {
          OMX_AUDIO_PARAM_AACPROFILETYPE aac;
          OMX_AUDIO_PARAM_PCMMODETYPE pcm;
        };
        // Potentially for the eProfile field, to at least consider whether we
        // should plumb that up to the codec client as potentially relevant
        // info.  The eProfile field is apparently OMX_AUDIO_AACPROFILETYPE or
        // OMX_AUDIO_WMAPROFILETYPE depending on "context" which is presumably
        // depending on format.eEncoding.
        //
        // TODO(dustingreen): We'll need to have a list of eProfile values here
        // if we really want to plumb all the info - we're not currently
        // sweeping nProfileIndex until we hit OMX_ErrorNoMore.
        // OMX_AUDIO_PARAM_ANDROID_PROFILETYPE android_profile;
      } audio;
      struct {
        // TODO(dustingreen): video
      } video;
    };
  };

  // Set AAC ADTS mode - called from SetAudioDecoderParams()
  void SetInputAacAdts();

  void onOmxStateSetComplete(OMX_STATETYPE state_reached);

  // things that don't need to be protected by lock_
  const std::string mime_type_;
  const std::string lib_filename_;

  // See codec.md "ordering domain" comments for why we have more than one
  // async_t.

  // The FIDL thread's async_t is in CodecRunner::dispatcher_ (parent class).

  bool is_setup_done_ = false;
  std::condition_variable is_setup_done_condition_;

  // We don't run any FIDL interfaces on this thread - it's a way to queue
  // stream control items such that FlushEndOfStreamAndCloseStream() can block
  // on this thread while waiting for previously-queued input data to finish
  // processing without getting in the way of recycling output buffers or
  // required mid-stream output re-configs.
  //
  // If we find ourselves trying to get rid of as many thread switches as
  // possible, we could refactor this class's implementation of the
  // StreamControl ordering domain to not always use a separate thread (some
  // complexity cost), or even to never use a separate thread (more complextiy
  // cost).
  //
  // We also handle OMX_EventPortSettingsChanged on the stream_control_ thread
  // when buffer_constraints_action_required true.
  std::unique_ptr<async::Loop> stream_control_;
  async_dispatcher_t* stream_control_dispatcher_ = nullptr;
  thrd_t stream_control_thread_ = 0;

  //
  // We separate state into two chunks - one for Codec-related state and one for
  // OMX-related state.
  //
  // TODO(dustingreen): Decide whether to split this class into two classes.
  // However, it's likely more fruitful to treat the Codec client as if it will
  // always behave perfectly, and move any Codec interface usage validation into
  // a separate process that sits in between a Codec client and each Codec
  // implementation. Once we do that, the hope is that there would remain little
  // point in a separate class to handle Codec interface aspects since those
  // aspects would be pretty much 1:1 with incoming Codec method calls, and
  // there's not really any fundamentally better middle interface with any
  // better representation than the Codec interface itself is already providing.
  // The fact is, the translation between Codec interface and OMX interface
  // isn't super simple, and it has to happen somewhere.  This class is that
  // somewhere.
  //

  //
  // Codec-related.
  //
  // Stuff that's here because of needing to implement the Codec iface.  In the
  // long run, this shouldn't be much - see above re. potentially moving any
  // protocol validation / enforcement to a separate process to avoid each Codec
  // implementation (and potentially each Codec client) needing to re-implement
  // that part.
  //

  bool IsActiveStream();

  // Some common handling for SetOutputBufferSettings() and
  // SetInputBufferSettings()
  void SetBufferSettingsCommonLocked(
      Port port, const fuchsia::mediacodec::CodecPortBufferSettings& settings,
      const fuchsia::mediacodec::CodecBufferConstraints& constraints);

  // Returns true if adding this buffer completed the input or output
  // configuration.  For output we need to know this so we can wake up the
  // StreamControl ordering domain.
  bool AddBufferCommon(Port port, fuchsia::mediacodec::CodecBuffer buffer);

  void EnsureFutureStreamSeenLocked(uint64_t stream_lifetime_ordinal);
  void EnsureFutureStreamCloseSeenLocked(uint64_t stream_lifetime_ordinal);
  void EnsureFutureStreamFlushSeenLocked(uint64_t stream_lifetime_ordinal);
  bool IsStreamActiveLocked();
  void CheckStreamLifetimeOrdinalLocked(uint64_t stream_lifetime_ordinal);
  void CheckOldBufferLifetimeOrdinalLocked(Port port,
                                           uint64_t buffer_lifetime_ordinal);
  bool IsInputConfiguredLocked();
  bool IsOutputConfiguredLocked();
  bool IsPortConfiguredCommonLocked(Port port);
  void SendFreeInputPacketLocked(fuchsia::mediacodec::CodecPacketHeader header);
  // During processing of a buffer_constraints_action_required true server-side,
  // the server will be de-configuring output buffers unilaterally.  Meanwhile,
  // the client can concurrently be trying to configure output with an old
  // buffer_constraints_version_ordinal.  We call this method fairly early in
  // the server-side processing to start ignoring any ongoing messages from the
  // client re. stale output config.
  void StartIgnoringClientOldOutputConfigLocked();

  void ValidateBufferSettingsVsConstraints(
      Port port, const fuchsia::mediacodec::CodecPortBufferSettings& settings,
      const fuchsia::mediacodec::CodecBufferConstraints& constraints);

  bool enable_on_stream_failed_ = false;

  std::unique_ptr<fuchsia::mediacodec::CreateDecoder_Params> decoder_params_;
  // TODO(dustingreen): Add these - consider whether/how to factor out strategy,
  // consistent with overall factoring to compensate for particular OMX codec
  // quirks. audio_encoder_params_ video_encoder_params_ (or combined)

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
  // This field must not be nullptr beyond the codec-type-specific method such
  // as SetAudioDecoderParams().  The codec_oob_bytes field can be null if the
  // codec type or specific format does not require codec_oob_bytes.
  std::unique_ptr<fuchsia::mediacodec::CodecFormatDetails>
      initial_input_format_details_;

  // This is the most recent settings recieved from the client and accepted,
  // received via SetInputBufferSettings() or SetOutputBufferSettings().  The
  // settings are as-received from the client.
  std::unique_ptr<const fuchsia::mediacodec::CodecPortBufferSettings>
      port_settings_[kPortCount];
  // The server's buffer_lifetime_ordinal, per port.  In contrast to
  // port_settings_[port].buffer_lifetime_ordinal, this value is allowed to be
  // even when the previous odd buffer_lifetime_ordinal is over, due to buffer
  // de-allocation.
  uint64_t buffer_lifetime_ordinal_[kPortCount] = {};

  // I am not absolutely certain that ignoring OMX's
  // OMX_EventPortSettingsChanged would be safe in the case where a client
  // moves on to a new stream before we've processed
  // OMX_EventPortSettingsChanged the normal way.  So in that case, we use this
  // field to force the client's next stream start to generate a new
  // OnOutputConfig() based on current OMX output config.  We do this at next
  // stream start rather than between streams so the client is forced to pay
  // attention to the OnOutputConfig().  The client might de-configure and
  // re-configure a few times based on the most recent OnOuputConfig(), so we
  // need to associate this with the buffer_constraints_ordinal that OMX said
  // meh to.
  //
  // TODO(dustingreen): Prove that this is needed or not needed, and keep or
  // remove.
  uint64_t omx_meh_output_buffer_constraints_version_ordinal_ = 0;

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
  uint64_t sent_buffer_constraints_version_ordinal_[kPortCount] = {0};
  uint64_t sent_format_details_version_ordinal_[kPortCount] = {0};

  uint64_t last_required_buffer_constraints_version_ordinal_[kPortCount] = {0};

  // For OmxCodecRunner, the initial CodecOutputConfig is sent immediately after
  // the input CodecBufferConstraints.  The CodecOutputConfig is likely to
  // change again before any output data is emitted, but it _may not_.
  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig> output_config_;

  // We ignore the part of the OMX spec where it says we should free the
  // underlying buffer "before" calling OMX FreeBuffer().  For one thing there's
  // no valid/reasonable way for OMX to actually detect whether that occurred or
  // not, and for another, it's none of OMX's business anyway.  This allows us
  // to move the OMX codec back to OMX loaded state to get
  // SimpleSoftOMXComponent.cpp to call onReset(), which we need to happen
  // between streams, without forcing us to free underlying buffers.  It still
  // requires us to call OMX FreeBuffer() for every input packet and every
  // output packet between streams, but there's nothing we can do about that
  // above OMX.

  std::vector<std::unique_ptr<Buffer>> all_buffers_[kPortCount];

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
  // queue, and the StreamControl ordering domain removes items from the head
  // of this queue.  This queue is how the StreamControl ordering domain knows
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

  // True means free at protocol level.  False means in-flight at protocol
  // level.  A size of 0 means not-allocated at protocol level.  This is used
  // to check for nonsense from the client.
  //
  // TODO(dustingreen): Consider moving these into Packet, despite probably
  // losing on packing efficiency.
  std::vector<bool> packet_free_bits_[kPortCount];

  // This is the buffer_lifetime_ordinal from SetOutputBufferSettings() or
  // SetInputBufferSettings().  This is used for protocol enforcement, to
  // enforce that AddOutputBuffer() or AddInputBuffer() is part of the same
  // buffer_lifetime_ordinal.
  uint64_t protocol_buffer_lifetime_ordinal_[kPortCount] = {};

  //
  // Adapter-related.
  //
  // Stuff that's in the middle between Codec iface and OMX.
  //

  void StartNewStream(std::unique_lock<std::mutex>& lock,
                      uint64_t stream_lifetime_ordinal);
  void EnsureStreamClosed(std::unique_lock<std::mutex>& lock);
  // Only EnsureStreamClosed() should call this.  All other callers want
  // EnsureStreamClosed() instead.
  void EnsureCodecStreamClosedLockedInternal();

  // Query OMX for output config info, convert that into CodecOutputConfig, and
  // send that to the client with OnOutputConfig().  This can be called on the
  // setup ordering domain, or on StreamControl ordering domain.
  void GenerateAndSendNewOutputConfig(std::unique_lock<std::mutex>& lock,
                                      bool buffer_constraints_action_required);

  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
  BuildNewOutputConfig(uint64_t stream_lifetime_ordinal,
                       uint64_t new_output_buffer_constraints_version_ordinal,
                       uint64_t new_output_format_details_version_ordinal,
                       bool buffer_constraints_action_required);
  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
  CreateNewOutputConfigFromOmxOutputFormat(
      std::unique_ptr<const OmxCodecRunner::OMX_GENERIC_PORT_FORMAT>
          omx_output_format,
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      uint64_t new_output_format_details_version_ordinal,
      bool buffer_constraints_action_required);
  void PopulateFormatDetailsFromOmxOutputFormat_Audio(
      const OmxCodecRunner::OMX_GENERIC_PORT_FORMAT& omx_output_format,
      fuchsia::mediacodec::CodecFormatDetails* format_details);

  void EnsureBuffersNotConfiguredLocked(Port port);

  fuchsia::mediacodec::AudioChannelId AudioChannelIdFromOmxAudioChannelType(
      OMX_AUDIO_CHANNELTYPE omx_audio_channeltype);

  // These Packet(s) are both Codec packets and OMX "buffers".
  //
  // These vectors own these buffers.
  std::vector<std::unique_ptr<Packet>> all_packets_[kPortCount];

  uint32_t omx_output_buffer_with_omx_count_ = 0;
  std::condition_variable omx_output_buffers_done_returning_condition_;

  // This OMX input buffer is special because it does not correspond to any
  // CodecPacket, and is reserved by this class for sending an empty OMX buffer
  // with EOS set to OMX.  For this purpose, we don't need the buffer's data
  // space to really be there, but just in case any OMX SW codec reads from an
  // input buffer despite zero valid data indicated, we point the extra buffer
  // at the same data space as packet 0 in all_packets_.  Zero codecs should be
  // writing to any input buffer, and we'll detect any violation of that rule
  // since we map our input VMOs read-only.
  //
  // We wrap this in a "Packet" primarily so that we can use "Packet*" as the
  // void* we pass to/from OMX re. an OMX buffer, without forcing an
  // OmxBuffer layer of abstraction to exist.
  //
  // The "eos" is "end of stream".
  std::unique_ptr<Packet> omx_input_packet_eos_;
  bool omx_input_packet_eos_free_ = true;

  // This OMX input buffer is special because it does not correspond to any
  // CodecPacket, and is reserved by this class for sending OMX the
  // OMX_BUFFERFLAG_CODECCONFIG omx buffer, which contains the
  // CodecFormatDetails.codec_oob_bytes.  Unlike omx_input_packet_eos_, this
  // packet's buffer corresponds to a VMO allocated server-side, since we do
  // need it to be able to hold real data, unlike the eos packet.
  //
  // We wrap this in a "Packet" primarily so that we can use "Packet*" as the
  // void* we pass to/from OMX re. an OMX buffer, without forcing an
  // OmxBuffer layer of abstraction to exist.
  //
  // The "oob" is "out of band".
  std::unique_ptr<Packet> omx_input_packet_oob_;
  std::unique_ptr<Buffer> omx_input_buffer_oob_;
  bool omx_input_packet_oob_free_ = true;
  std::condition_variable omx_input_packet_oob_free_condition_;

  // This condition variable notifies all waiting threads when any of the
  // following occur:
  //   * Output goes from not configured to configured.
  //   * A stream gets marked as discarded.
  // Note that input going from not configured to configured does not trigger
  // this, as that's already inherently in the StreamControl ordering domain.
  //
  // This allows the StreamControl ordering domain to wait for a stream's output
  // config to catch up _or_ for the stream to get discarded by the client, in
  // which case the client won't necessarily ever be catching up to the stream's
  // output config.
  //
  // As is typical with condition variables, we don't worry about spurious
  // wakes.  We might be waking re. a different stream than the StreamControl is
  // actually working on currently, for example.
  std::condition_variable wake_stream_control_;

  // This is set when stream_.output_end_of_stream is set.
  std::condition_variable output_end_of_stream_seen_;

  //
  // OMX-related.
  //
  // Stuff that's here to deal with OMX.  This stuff would pretty much need to
  // exist even if were just trying to use an OMX codec without adapting to
  // the Codec interface.
  //

  // OMX handlers

  // Shim handler that calls pAppData->EventHandler() essentially.
  static OMX_ERRORTYPE omx_EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                                        OMX_IN OMX_PTR pAppData,  // this
                                        OMX_IN OMX_EVENTTYPE eEvent,
                                        OMX_IN OMX_U32 nData1,
                                        OMX_IN OMX_U32 nData2,
                                        OMX_IN OMX_PTR pEventData);
  static OMX_ERRORTYPE omx_EmptyBufferDone(
      OMX_IN OMX_HANDLETYPE hComponent, OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
  static OMX_ERRORTYPE omx_FillBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                                          OMX_IN OMX_PTR pAppData,
                                          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

  // These are called by omx_EventHanlder (or corresponding callback) with
  // this == pAppData, and with hComponent not a parameter, because that's
  // available as a member variable.
  OMX_ERRORTYPE EventHandler(OMX_IN OMX_EVENTTYPE eEvent, OMX_IN OMX_U32 nData1,
                             OMX_IN OMX_U32 nData2, OMX_IN OMX_PTR pEventData);
  OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
  OMX_ERRORTYPE FillBufferDone(OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer);

  std::unique_ptr<const OMX_GENERIC_PORT_FORMAT> OmxGetOutputFormat();

  void OmxTryRecycleOutputPacketLocked(OMX_BUFFERHEADERTYPE* header);
  void OmxQueueInputPacket(const fuchsia::mediacodec::CodecPacket& packet);
  void EnsureOmxStateExecuting(std::unique_lock<std::mutex>& lock);
  void EnsureOmxBufferCountCurrent(std::unique_lock<std::mutex>& lock);
  void EnsureOmxStateLoaded(std::unique_lock<std::mutex>& lock);
  void OmxStartStateSetLocked(OMX_STATETYPE omx_state_desired);
  void OmxWaitForState(std::unique_lock<std::mutex>& lock,
                       OMX_STATETYPE from_state, OMX_STATETYPE desired_state);
  void OmxFreeAllBufferHeaders(std::unique_lock<std::mutex>& lock);
  void OmxFreeAllPortBufferHeaders(std::unique_lock<std::mutex>& lock,
                                   Port port);
  void OmxFreeBufferHeader(std::unique_lock<std::mutex>& lock, Port port,
                           Packet* packet);
  void OmxPortUseBuffers(std::unique_lock<std::mutex>& lock, Port port);
  OMX_BUFFERHEADERTYPE* OmxUseBuffer(std::unique_lock<std::mutex>& lock,
                                     Port port, const Packet& packet);
  void OmxOutputStartSetEnabledLocked(bool enable);
  void OmxWaitForOutputEnableStateChangeDone(
      std::unique_lock<std::mutex>& lock);
  void OmxFillThisBufferLocked(OMX_BUFFERHEADERTYPE* header);
  void OmxQueueInputOOB();
  void OmxQueueInputEOS();
  void onOmxStreamFailed(uint64_t stream_lifetime_ordinal);
  void onOmxEventPortSettingsChanged(uint64_t stream_lifetime_ordinal);
  void OmxWaitForOutputBuffersDoneReturning(std::unique_lock<std::mutex>& lock);

  // nullptr if not created yet, or the OMX component handle if created
  OMX_COMPONENTTYPE* omx_component_;
  OMX_CALLBACKTYPE omx_callbacks_;
  // what OMX state - starts in OMX_StateInvalid until the component is created
  // at which point the component will be in OMX_StateLoaded initially.  See OMX
  // IL spec for more on how component states and state transitions work.  We
  // don't handle all possible OMX state transitions in this class, only the
  // ones that are relevant/possible for the android SW codecs.  For example we
  // don't do anything with the "wait for resources" state.
  OMX_STATETYPE omx_state_;
  // This tracks the latest state that we've tried to move the codec to, and
  // is updated as soon as we start trying to move the codec to the new state.
  // Keeping this around helps us avoid pestering the OMX codec if/when client
  // code is trying to ask for something unreasonable/impossible given an
  // ongoing state change, without leaning on the OMX codec to do this check
  // for us.  At the least, this achieves a nicer error message than a generic
  // failure message re. the OMX codec returning failure.
  OMX_STATETYPE omx_state_desired_;
  // Any time the omx_state_ changes this condition variable signals all
  // waiters.  See WaitForStateOrError() for a way to wait for a specific state
  // to be reached.  This condition variable must only be waited on using a
  // unique_lock on lock_ (not on any other lock).
  std::condition_variable omx_state_changed_;

  bool omx_output_enabled_ = true;
  bool omx_output_enabled_desired_ = true;
  std::condition_variable omx_output_enabled_changed_;

  bool is_omx_recycle_enabled_ = false;

  OMX_U32 omx_port_index_[kPortCount] = {};

  // These are the _initial_ OMX port defs.  This initial-ness is important
  // because OMX allows nBufferSize to change, but we'd like to use the initial
  // nBufferSize as the Codec min buffer bytes per packet for input, and not
  // require the Codec client to ever re-configure input buffers.  This seems
  // reasonable because even OMX doesn't actually force re-configuration of
  // input buffers during dynamic output format detection when starting a
  // stream, so we'll see if we can get the up-front input nBufferSize to work
  // as a Codec input min buffer size.
  //
  // We'll smooth over when OMX nBufferSize is larger then Codec nBufferSize on
  // input by reporting nBufferSize to OMX but not filling buffers beyond their
  // actual Codec size.  If any codec tries to read beyond the valid data bytes
  // of an input packet, we'll have to re-evaluate this strategy...
  //
  // On output, OMX is allowed to change output format, and we'll let OMX
  // nBufferSize drive per_packet_buffer_bytes_min.
  //
  // We never force nBufferSize to increase via SetParameter() with
  // OMX_IndexParamPortDefinition since the OMX spec says nBufferSize is
  // read-only, despite SimpleSoftOMXComponent.cpp permitting nBufferSize to
  // increase but not decrease.  But OMX may unilaterially change nBufferSize
  // both up or down based on other parameters being set, or based on output
  // format detection.  For input, it isn't changed during format detection, but
  // still can change up or down based on other parameters being set.  See above
  // re. how we smooth over any such input nBufferSize changes.
  //
  // TODO(dustingreen): We should only really be using this for asserts to do
  // with the input buffer sizes - if that remains true then we could replace
  // this with OMX_U32 omx_initial_input_nBufferSize_ or similar.
  OMX_PARAM_PORTDEFINITIONTYPE omx_initial_port_def_[kPortCount] = {};

  // This isn't the latest we've seen from OMX via any GetParameter call.
  // Instead, this is the latest we've seen from OMX via any path where we
  // expect OMX to potentially change OMX buffer constraints, in a way that we
  // are forced to reflect via the Codec interface.
  //
  // For input, if nBufferSize increases in OMX unilaterally (we have no such
  // cases yet but could in future), we report larger-than-actual input buffers
  // to OMX and then don't fill them beyond their actual size.
  //
  // For output, we only expect nBufferSize to change when OMX triggers
  // OMX_EventPortSettingsChanged.  We update the output part of this member
  // var during OmxGetOutputFormat(), just because that's the common path
  // involved.
  OMX_PARAM_PORTDEFINITIONTYPE omx_port_def_[kPortCount] = {};

  //
  // Overall behavior.
  //

  // Post to dispatcher in a way that's guaranteed to run the posted work in the
  // same order as the posting order.
  void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(OmxCodecRunner);
};

}  // namespace codec_runner

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_OMX_CODEC_RUNNER_H_
