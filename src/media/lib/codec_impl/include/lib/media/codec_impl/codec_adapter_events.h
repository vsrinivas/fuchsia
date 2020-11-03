// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADAPTER_EVENTS_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADAPTER_EVENTS_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/media/codec_impl/codec_metrics.h>

class CodecPacket;

//
// For use by CodecAdapter implementations to report async events.
//

// We use an abstract base class here instead of letting CodecAdapter
// sub-classes directly call CodecImpl, just to make sure the CodecAdapter uses
// the intended interface to the CodecImpl.

class CodecAdapterEvents {
 public:
  // If the core codec needs to fail the whole CodecImpl, such as when/if new
  // FormatDetails are different than the initial FormatDetails and
  // the core codec doesn't support switching from the old to the new input
  // format details (for example due to needing different input buffer config).
  virtual void onCoreCodecFailCodec(const char* format, ...) = 0;

  // The core codec should only call this method at times when there is a
  // current stream, not between streams.
  virtual void onCoreCodecFailStream(fuchsia::media::StreamError error) = 0;

  // The core codec should only call this method at times when there is a
  // current stream, not between streams.  If the core codec calls this method,
  // the core codec must also override CoreCodecResetStreamAfterCurrentFrame(),
  // which _may_ be called async from the StreamControl thread, if the client
  // hasn't already moved onto a new stream by then.
  //
  // This call requests a call to CoreCodecResetStreamAfterCurrentFrame() on the
  // StreamControl thread (async with respect to this method call), if the
  // current stream isn't obsoleted first.
  virtual void onCoreCodecResetStreamAfterCurrentFrame() = 0;

  // "Mid-stream" can mean at the start of a stream also - it's just required
  // that a stream be active currently.  The core codec must ensure that this
  // call is propertly ordered with respect to onCoreCodecOutputPacket() and
  // onCoreCodecOutputEndOfStream() calls.
  //
  // A call to onCoreCodecMidStreamOutputConstraintsChange(true) must not be
  // followed by any more output (including EndOfStream) until the associated
  // output re-config is completed by a call to
  // CoreCodecMidStreamOutputBufferReConfigFinish().
  virtual void onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) = 0;

  // When the core codec calls this method, the CodecImpl will note that the
  // format has changed, and on next onCoreCodecOutputPacket(), the CodecImpl
  // will ask the core codec for the format and generate and send an
  // OnOutputformat() message before that output packet.  This way, the core
  // codec is free to call onCoreCodecOutputFormat() repeatedly without any
  // packet in between, with CodecImpl collapsing these into one
  // OnOutputFormat() to avoid the extra message (so it doesn't have to be sent
  // and doesn't have to be handled by clients).
  virtual void onCoreCodecOutputFormatChange() = 0;

  virtual void onCoreCodecInputPacketDone(CodecPacket* packet) = 0;

  virtual void onCoreCodecOutputPacket(CodecPacket* packet, bool error_detected_before,
                                       bool error_detected_during) = 0;

  virtual void onCoreCodecOutputEndOfStream(bool error_detected_before) = 0;

  // If the CodecAdapter sub-class ever calls this method, the CodecAdapter sub-class must also
  // overide CoreCodecMetricsImplementation(), and not return std::nullopt from that method.
  virtual void onCoreCodecLogEvent(
      media_metrics::StreamProcessorEvents2MetricDimensionEvent event_code) = 0;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_ADAPTER_EVENTS_H_
