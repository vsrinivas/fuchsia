// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADAPTER_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADAPTER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>

#include <list>
#include <random>

#include <fbl/macros.h>

#include "codec_adapter_events.h"
#include "codec_input_item.h"
#include "codec_port.h"

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
  CodecAdapter(std::mutex& lock, CodecAdapterEvents* codec_adapter_events);
  virtual ~CodecAdapter();

  // Core codec.
  //
  // For the moment, these methods are placeholders for calls to the core codec.
  //
  // TODO(dustingreen): Implement these.  Maybe switch them to
  // core_codec_->Method() calls at the call sites, if there's nothing relevant
  // to do in the wrapper methods.
  //

  // During format detection, a codec may be ok with null output config (false),
  // or may require an output config (true).
  virtual bool IsCoreCodecRequiringOutputConfigForFormatDetection() = 0;

  // If true, the codec can make use of VMOs that are mapped for direct access
  // by the CPU.
  //
  // If true, the CodecImpl will map the buffer VMOs unless buffers are secure
  // memory, and CodecBuffer::base() is usable for direct CPU access.
  //
  // If a codec doesn't support secure memory operation, then buffers won't be
  // secure memory and will be mapped if IsCoreCodecMappedBufferUseful().
  //
  // If buffers are secure, then CodecImpl won't actually map the buffer VMOs,
  // and CodecBuffer::base() isn't usable for direct CPU access.  Instead
  // CodecBuffer::base() will be a vaddr that will fault if accessed as if it
  // were buffer data, and preserves the low-order vmo_usable_start %
  // ZX_PAGE_SIZE bits.
  virtual bool IsCoreCodecMappedBufferUseful(CodecPort port) = 0;

  // If true, the codec is HW-based, in the sense that at least some of the
  // processing is performed by specialized processing HW running separately
  // from any CPU execution context.
  virtual bool IsCoreCodecHwBased(CodecPort port) = 0;
  // Any core codec that performs DMA that will potentially continue beyond the
  // lifetime of the process that holds open the VMO handles being DMA(ed)
  // should override this method to provide CodecImpl with the driver's BTI so
  // VMOs can be properly pinned for DMA.  If a core codec returns true from
  // IsCoreCodecHwBased(), the core codec should also override this method.
  //
  // TODO(38650): At least the VP9 decoder isn't overriding this method yet.
  // Also we should enforce that this method be overridden when
  // IsCoreCodecHwBased() true.
  virtual zx::unowned_bti CoreCodecBti() { return zx::unowned_bti(); }

  // The initial input format details and later input format details will
  // _often_ remain the same overall format, and only differ in ways that are
  // reasonable on a format-specific basis.  However, not always.  A core codec
  // should check that any new input format details is still fully compatible
  // with the core codec's initialized configuration (as set up during
  // CoreCodecInit()), and if not, fail the CodecImpl using
  // onCoreCodecFailCodec().  Core codecs may re-configure themselves based
  // on new input FormatDetails to the degree that's reasonable for the
  // input format and the core codec's capabilities, but there's no particular
  // degree to which this is required (for now at least).  Core codecs are
  // discouraged from attempting to reconfigure themselves to process completely
  // different input formats that are better to think of as a completely
  // different Codec.
  //
  // A client that's using different FormatDetails than the initial
  // FormatDetails (to any degree) should try one more time with a fresh
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
  virtual void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) = 0;

  // The default implementation silently accepts OFF.  On any other value, the
  // default implementation fails the codec.
  //
  // CodecAdapter sub-classes can accept ON if they support ON.  Same for
  // DYNAMIC if/when we add that.  The CodecFactory implementation should
  // already have information on which codecs support ON (or DYNAMIC) for output
  // or input, and CodecImpl will enforce consistency of BufferCollection
  // constraints and BufferCollectionInfo_2 with the SecureMemoryMode specified
  // during codec creation.
  virtual void CoreCodecSetSecureMemoryMode(
      CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode);

  // All codecs must implement this for both ports.  The returned structure will
  // be sent to sysmem in a SetConstraints() call.  This method can be called
  // on the FIDL thread or the StreamControl domain (thread for now).
  //
  // Input:
  //
  // For now, a core codec has no way to trigger being asked for new input
  // constraints, so the input constraints (for now) need to be generally
  // applicable to any potential setting/property of the input.
  //
  // A decoder should permit a fairly wide range of buffer space, without
  // worrying whether the min is enough to efficiently handle a high bitrate.
  // The CodecImpl will own bumping up the min based on approximate bitrate
  // provided in the initial decoder creation parameters and/or per-stream input
  // format details (this logic is shared because it can reasonably be shared).
  // A core codec that has special requirements for extra input buffer space
  // given a particular bitrate can take it upon itself to set the input min
  // buffer space, but the idea is that typically it won't be necessary for a
  // decoder to increase the min based on input bitrate beyond what CodecImpl
  // does, so needing to do this should be fairly rare.
  //
  // A video encoder which needs to vary it's input BufferCollectionConstraints
  // based on encoder settings can do so, using the data provided to
  // CoreCodecInit().  If later use of QueueInputFormatDetails() (per-stream)
  // results in an input packet that conforms to the old
  // BufferCollectionConstraints but does not conform to the effective new
  // BufferCollectionConstraints, the core codec can use
  // CodecAdapterEvents::onCoreCodecFailCodec() (the core codec should fail the
  // codec instance in this case rather than attempt to handle input data that
  // is outside the bounds that would have been indicated by the core codec had
  // the current input format details been used as the initial format details).
  // Clients that change the input format details on the fly should be willing
  // to re-request a new codec instance at least once starting with the new
  // input format details via the CodecFactory.  This is true for additional
  // reasons beyond this paragraph involving the possibility of accelerated but
  // partial codec implementations.  If a client needs to change input format
  // details but doesn't want to concern itself with tracking whether the
  // current codec was created with the current input format details, a client
  // can instead choose to always create a new codec via CodecFactory on any
  // change to the input format details.
  //
  // Output:
  //
  // A core codec can trigger this method to get called again by indicating an
  // output format detection/change with action_required true via
  // CoreCodecEvents::onCoreCodecMidStreamOutputConstraintsChange().
  //
  // Filling out the usage bits is optional.  If the usage bits are not filled
  // out (all still 0), the caller will fill them out based on
  // IsCoreCodecMappedBufferUseful() and IsCoreCodecHwBased().  The core codec
  // must either leave usage set to all 0, or completely fill them out.
  virtual fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) = 0;

  // There are no VMO handles in the buffer_collection_info.  Those are instead
  // provided via calls to CoreCodecAddBuffer(), as CodecImpl handles allocation
  // of CodecBuffer instances (each of which has a VMO).
  //
  // This method allows a core codec to know things like buffer_count, whether
  // sysmem selected CPU domain or RAM domain for sharing of buffers, whether
  // protected buffers were allocated, etc.
  //
  // This call occurs regardless of whether "settings" or "partial settings" are
  // set (regardless of whether the client is using sysmem), after the client
  // sets input or output settings, and before the first buffer is added.
  virtual void CoreCodecSetBufferCollectionInfo(
      CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) = 0;

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

  // The parameter includes the oob_bytes. The core codec is free to call
  // onCoreCodecFailCodec() (immediately on this stack or async) if the
  // override input format details can't be accommodated (even in situations
  // where the override input format details would be ok as initial input format
  // details, such as when new input buffer config is needed).
  //
  // That said, the core codec should try to accommodate the change, especially
  // if the client has configured adequate input buffers, and the basic type of
  // the input data hasn't changed.
  //
  // TODO(dustingreen): Nail down the above sorta-vaguely-described rules
  // better.
  //
  // Only permitted between CoreCodecStartStream() and CoreCodecStopStream().
  virtual void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details) = 0;

  // Only permitted between CoreCodecStartStream() and CoreCodecStopStream().
  virtual void CoreCodecQueueInputPacket(CodecPacket* packet) = 0;

  // Only permitted between CoreCodecStartStream() and CoreCodecStopStream().
  virtual void CoreCodecQueueInputEndOfStream() = 0;

  // Stop the core codec from processing any more data for the stream that was
  // active and is now stopping.
  virtual void CoreCodecStopStream() = 0;

  // Reset the stream.  Used in processing a watchdog.  If an adapter never generates a watchdog, it
  // doesn't need to override this method.
  //
  // Do not discard any input data or output data beyond what was being worked on at the time the
  // watchdog fired.
  virtual void CoreCodecResetStreamAfterCurrentFrame();

  // Add input or output buffer.
  //
  // The buffers added via this method correspond to the buffers of the buffer
  // collection - these are buffers of the collection most recently indicated
  // via a call to CoreCodecSetBufferCollectionInfo().  While the VMOs are
  // intentionally not included in that call, the VMOs are indicated here (this
  // lets the CodecImpl own allocation of CodecBuffer instances).
  //
  // A core codec may be able to fully configure a buffer during this call and
  // later ignore CoreCodecConfigureBuffers(), or a core codec may use
  // CoreCodecConfigureBuffers() to finish configuring buffers.
  virtual void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) = 0;

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
      CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) = 0;

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
  // onCoreCodecMidStreamOutputConstraintsChange(true), after
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

  // The core codec may need to specify what input constraints to be used.
  //
  // This is called on the StreamControl ordering domain after CoreCodecInit and
  // will not be called again after that as input constraints are static. Unlike
  // most CodecAdapter functions, the CodecAdapter provides a default
  // implementation that will work for most codecs. A codec-specific
  // CodecAdapter may override this if it has different constraints.
  virtual std::unique_ptr<const fuchsia::media::StreamBufferConstraints>
  CoreCodecBuildNewInputConstraints();

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
  // onCoreCodecMidStreamOutputConstraintsChange() (and with same stream still
  // active).
  virtual std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
  CoreCodecBuildNewOutputConstraints(uint64_t stream_lifetime_ordinal,
                                     uint64_t new_output_buffer_constraints_version_ordinal,
                                     bool buffer_constraints_action_required) = 0;

  // This will be called on the InputData domain, during the core codec's call
  // to onCoreCodecOutputPacket(), so that the format will be delivered at most
  // once before any packet which needs a new format to be indicated.  The core
  // codec can trigger this to occur during the next onCoreCodecOutputPacket()
  // by calling onCoreCodecOutputFormatChange().  The tracking of pending
  // output format is per-stream, and all streams start with a pending output
  // format, so a core codec need not call onCoreCodecOutputFormatChange()
  // unless the format change is mid-stream (but calling before the first packet
  // is allowed and not harmful).
  virtual fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) = 0;

  // CoreCodecMidStreamOutputBufferReConfigPrepare()
  //
  // For a mid-stream format change where output buffer re-configuration is
  // needed (as initiated async by the core codec calling
  // CodecAdapterEvents::onCoreCodecMidStreamOutputConstraintsChange(true)),
  // this method is called on the StreamControl thread before the client is
  // notified of the need for output buffer re-config (via OnOutputConstraints()
  // with buffer_constraints_action_required true).
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
  // permitted, but not encouraged, to discard some output frames. A codec is
  // also permitted to achieve a more seamless format switch despite output
  // buffer re-config by retaining references to old-format low-level buffers,
  // copying into temporary buffers and back out, or similar. However, core
  // codec implementers should note that the process of re-configuring output
  // buffers is not likely to be super-quick, and other parts of the system may
  // not go to so much effort to achieve seamless-ness across an output buffer
  // re-config, so ... it's probably best not to spend time trying to achieve
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
  //
  // The core codec must not onCoreCodecOutputPacket() or
  // onCoreCodecOutputEndOfStream() until this method has been called, or until
  // CoreCodecStartStream() is called and some input is available, should the
  // current stream be stopped before completing mid-stream output buffer
  // re-config.  This works partly because the CodecImpl guarantees that if a
  // mid-stream re-config didn't finish, there will be a complete output
  // re-config before the CoreCodecStartStream() - in other words this re-config
  // is abandoned and a new one takes its place and is fully complete prior to
  // the new stream starting.
  virtual void CoreCodecMidStreamOutputBufferReConfigFinish() = 0;

  // Returns a name for the codec that's used for debugging.
  virtual std::string CoreCodecGetName() { return ""; }

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

  // It's useful to have a source of random numbers that's compatible with std::
  // for purposes such as scrambling the order of free packets.  These are
  // instance-specific only because of thread-safety considerations, not because
  // of generated sequence considerations.
  std::random_device random_device_;
  std::mt19937 not_for_security_prng_;

 private:
  CodecAdapter() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecAdapter);
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADAPTER_H_
