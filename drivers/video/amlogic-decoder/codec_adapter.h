// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H_

#include "codec_adapter_events.h"
#include "codec_input_item.h"
#include "codec_port.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fxl/macros.h>

#include <list>

class CodecBuffer;

// The CodecAdapter abstract base class is used by CodecImpl to interface with
// a particular SW or HW codec.  At the layer of this interface, there's only
// ever up to one active stream to worry about, and Codec FIDL protocol
// enforcement has already been handled above.
//
// For HW-based codecs that need to share the HW, a CodecAdapter represents up
// to one active stream, and does not directly participate in sharing the HW;
// that's further down.
//
// The intent of this interface is to be as narrow an in-process codec interface
// as feasible between FIDL protocol aspects above, and codec-specific details
// below.
class CodecAdapter {
 public:
  // At least for now, the CodecImpl and CodecAdapter share their main lock.
  //
  // The CodecImpl won't call CodecAdapter methods with the lock_ held, mainly
  // to avoid building up dependencies on the lock sharing, and also to avoid
  // situations where the core codec code would just have to release the lock_
  // in order to acquire video_decoder_lock_ (which is "before" lock_, due to
  // calls from interrupt handlers that already have video_decoder_lock_ held).
  //
  // The CodecAdapter should never call CodecAdapterEvents methods with lock_
  // held.
  explicit CodecAdapter(std::mutex& lock,
                        CodecAdapterEvents* codec_adapter_events);
  virtual ~CodecAdapter();

  //
  // Core codec.
  //
  // For the moment, these methods are placeholders for calls to the core codec.
  //
  // TODO(dustingreen): Implement these.  Maybe switch them to
  // core_codec_->Method() calls at the call sites, if there's nothing relevant
  // to do in the wrapper methods.
  //

  // During format detection, a codec may be ok with null output config (true),
  // or may require an output config (false).
  virtual bool IsCoreCodecRequiringOutputConfigForFormatDetection() = 0;

  // The initial input format details and later input format details will
  // _often_ remain the same overall format, and only differ in ways that are
  // reasonable on a format-specific basis.  However, not always.  A core codec
  // should check that any new input format details is still fully compatible
  // with the core codec's initialized configuration (as set up during
  // CoreCodecInit()), and if not, fail the CodecImpl using
  // onCoreCodecFailCodec().  Core codecs may re-configure themselves based
  // on new input CodecFormatDetails to the degree that's reasonable for the
  // input format and the core codec's capabilities, but there's no particular
  // degree to which this is required (for now at least).  Core codecs are
  // discouraged from attempting to reconfigure themselves to process completely
  // different input formats that are better to think of as a completely
  // different Codec.
  //
  // A client that's using different CodecFormatDetails than the inital
  // CodecFormatDetails (to any degree) should try one more time with a fresh
  // Codec before giving up (giving up immediately only if the format details at
  // time of failure match the initial format details specified during Codec
  // creation).
  //
  // The core codec can copy the initial_input_format_details during this call
  // (using fidl::Clone() or similar), but as is the custom with references,
  // should not stash the passed-in reference.
  //
  // TODO(dustingreen): Re-visit the lifetime rule and required copy here, once
  // more is nailed down re. exactly how the core codec relates to CodecImpl.
  virtual void CoreCodecInit(const fuchsia::mediacodec::CodecFormatDetails&
                                 initial_input_format_details) = 0;

  // Stream lifetime:
  //
  // The CoreCodecStartStream() and CoreCodecStopStream() calls bracket the
  // lifetime of the current stream.  The CoreCodecQueue.* calls are
  // stream-specific and apply to the current stream.  There is only up to one
  // current stream, and CoreCodecQueue.* calls will only occur when there is a
  // current stream.
  //
  // At least for now, we don't use a separate object instance for the current
  // stream, for the following reasons:
  //   * This interface is the validated+de-async-ed version of the Codec FIDL
  //     interface and the Codec FIDL interface doesn't have a separate Stream
  //     object/channel, so not having a separate stream object here makes the
  //     correspondence closer.
  //   * While the stream is fairly separate, there are also aspects of stream
  //     behavior such as mid-stream output format change which can cause a
  //     stream to essentially re-configure codec-wide output buffers, so the
  //     separate-ness of a stream from the codec isn't complete (regardless of
  //     separate stream object or not).
  //
  // All that said, it certainly can be useful to think of the stream as a
  // logical lifetime of a thing, despite it not being a separate object (at
  // least for now).  Some implementations of CodecAdapter may find it
  // convenient to create their own up-to-one-at-a-time-per-CodecAdapter stream
  // object to model the current stream, and that's totally fine.

  // The "Queue" methods will only be called in between CoreCodecStartStream()
  // and CoreCodecStopStream().
  virtual void CoreCodecStartStream() = 0;

  // The parameter includes the codec_oob_bytes. The core codec is free to call
  // onCoreCodecFailCodec() (immediately on this stack or async) if the
  // override input format details can't be accomodated (even in situations
  // where the override input format details would be ok as initial input format
  // details, such as when new input buffer config is needed).
  //
  // That said, the core codec should try to accomodate the change, especially
  // if the client has configured adequate input buffers, and the basic type of
  // the input data hasn't changed.
  //
  // TODO(dustingreen): Nail down the above sorta-vaguely-described rules
  // better.
  //
  // Only permitted between CoreCodecStartStream() and CoreCodecStopStream().
  virtual void CoreCodecQueueInputFormatDetails(
      const fuchsia::mediacodec::CodecFormatDetails&
          per_stream_override_format_details) = 0;

  // Only permitted between CoreCodecStartStream() and CoreCodecStopStream().
  virtual void CoreCodecQueueInputPacket(const CodecPacket* packet) = 0;

  // Only permitted between CoreCodecStartStream() and CoreCodecStopStream().
  virtual void CoreCodecQueueInputEndOfStream() = 0;

  // Stop the core codec from processing any more data for the stream that was
  // active and is now stopping.
  virtual void CoreCodecStopStream() = 0;

  // Add input or output buffer.
  //
  // A core codec may be able to fully configure a buffer during this call and
  // later ignore CoreCodecConfigureBuffers(), or a core codec may use
  // CoreCodecConfigureBuffers() to finish configuring buffers.
  virtual void CoreCodecAddBuffer(CodecPort port,
                                  const CodecBuffer* buffer) = 0;

  // Finish setting up input or output buffer(s).
  //
  // Consider doing as much as feasible in CoreCodecAddBuffer() instead, to be
  // _slightly_ nicer to shared_fidl_thread().
  //
  // TODO(dustingreen): Assuming a well-behaved client but time-consuming call
  // to this method, potentially another Codec instance could be disrupted due
  // to sharing the shared_fidl_thread().  If we see an example of that
  // happening, we could switch to not sharing any FIDL threads across Codec
  // instances.
  virtual void CoreCodecConfigureBuffers(
      CodecPort port,
      const std::vector<std::unique_ptr<CodecPacket>>& packets) = 0;

  // This method can be called at any time while output buffers are (fully)
  // configured, including while there's no active stream.
  //
  // This will also be called on each of the output packets shortly after
  // CoreCodecConfigureBuffers() is called.  This is implicit in the Codec
  // interface, but explicit (via calls to this method) in the CodecAdapter
  // interface.
  virtual void CoreCodecRecycleOutputPacket(CodecPacket* packet) = 0;

  // De-configure input or output buffers.  This will never occur at a time
  // when the core codec is expected to be processing data.  For input, this
  // can only be called while there's no active stream.  For output, this can
  // be called while there's no active stream, or after a stream is started but
  // before any input data is queued, or during processing shortly after the
  // core codec calling
  // onCoreCodecMidStreamOutputConfigChange(true), after
  // CoreCodecMidStreamOutputBufferReConfigPrepare() and before
  // CoreCodecMidStreamOutputBufferReConfigFinish().
  //
  // The "ensure" part of the name is because this needs to ensure that buffers
  // are fully de-configured, regardless of whether buffers are presently fully
  // de-configured already, or if CoreCodecAddBuffer() has been called 1-N times
  // but CoreCodecConfigureBuffers() hasn't been called yet (and won't be, if
  // this method is called instead), or if CoreCodecAddBuffer() has been called
  // N times and CoreCodecConfigureBuffers() has also been called.
  virtual void CoreCodecEnsureBuffersNotConfigured(CodecPort port) = 0;

  // The core codec needs to specify what output config is needed.
  //
  // output_re_config_required true:
  //
  // This is called on StreamControl ordering domain - this can happen very soon
  // if CoreCodecStopStream() hasn't happened yet, or can happen much later when
  // the next stream is starting.  Or may not happen at all if CodecImpl fails
  // due to channel closure or any other reason.
  //
  // output_re_config_required false:
  //
  // This is called on the same thread and same stack as
  // onCoreCodecMidStreamOutputConfigChange() (and with same stream still
  // active).
  virtual std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
  CoreCodecBuildNewOutputConfig(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      uint64_t new_output_format_details_version_ordinal,
      bool buffer_constraints_action_required) = 0;

  // CoreCodecMidStreamOutputBufferReConfigPrepare()
  //
  // For a mid-stream format change where output buffer re-configuration is
  // needed (as initiated async by the core codec calling
  // CodecAdapterEvents::onCoreCodecMidStreamOutputConfigChange(true)), this
  // method is called on the StreamControl thread before the client is notified
  // of the need for output buffer re-config (via OnOutputConfig() with
  // buffer_constraints_action_required true).
  //
  // The core codec should do whatever is necessary to ensure that output
  // buffers are done de-configuring to the extent feasible by the time this
  // method returns.  See next paragraph for the only cases where retaining old
  // low-level buffers _might_ be justified (but for the most part, those
  // reasons aren't really pragmatic reasons to be retaining old low-level
  // buffers, at least for now).  If a core codec keeps old low-level buffer
  // handles/references around for a while to be more seamless (entirely
  // optional and not recommended per next paragraph), the core codec must drop
  // those handles/references as soon as they're no longer needed in trying to
  // achieve more seamless-ness.
  //
  // A core codec need only support seamless resolution/format changes if the
  // output buffers (considering separately, width, height, and any other
  // similar parameter like color depth) are already large enough for both the
  // before format and after format.  If this is not the case, a codec is
  // permitted, but not encouranged, to discard some output frames. A codec is
  // also permitted to achive a more seamless format switch despite output
  // buffer re-config by retaining references to old-format low-level buffers,
  // copying into temporary buffers and back out, or similar. However, core
  // codec implementers should note that the process of re-configuring output
  // buffers is not likely to be super-quick, and other parts of the system may
  // not go to so much effort to achieve seamless-ness across an output buffer
  // re-config, so ... it's probably best not to spend time trying to achive
  // seamless-ness for a situation which for other reasons might end up being
  // non-seamless at least in terms of timing consistency in any case.
  //
  // As always, calls to CodecAdapterEvents must not be made while holding
  // lock_.
  virtual void CoreCodecMidStreamOutputBufferReConfigPrepare() = 0;

  // This method is called when the mid-stream output buffer re-configuration
  // has completed.  This is called after all the calls to CoreCodecAddBuffer()
  // and the call to CoreCodecConfigureBuffers() are done.
  //
  // The core codec should do whatever is necessary to get back into normal
  // steady-state operation in this method.
  virtual void CoreCodecMidStreamOutputBufferReConfigFinish() = 0;

 protected:
  // See comment on the constructor re. sharing this lock with the caller of
  // CodecAdapter methods, at least for now.
  std::mutex& lock_;

  CodecAdapterEvents* events_ = nullptr;

  // For now all the sub-classes queue input here, so may as well be in the base
  // class for now.
  std::list<CodecInputItem> input_queue_;

  // A core codec will also want to track free output packets, but how best to
  // do that is sub-class-specific.

 private:
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CodecAdapter);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H_
