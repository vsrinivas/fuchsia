// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_EVENTS_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_EVENTS_H_

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
  // CodecFormatDetails are different than the initial CodecFormatDetails and
  // the core codec doesn't support switching from the old to the new input
  // format details (for example due to needing different input buffer config).
  virtual void onCoreCodecFailCodec(const char* format, ...) = 0;

  // The core codec should only call this method at times when there is a
  // current stream, not between streams.
  virtual void onCoreCodecFailStream() = 0;

  // "Mid-stream" can mean at the start of a stream also - it's just required
  // that a stream be active currently.  The core codec must ensure that this
  // call is propertly ordered with respect to onCoreCodecOutputPacket() and
  // onCoreCodecOutputEndOfStream() calls.
  //
  // A call to onCoreCodecMidStreamOutputConfigChange(true) must not be
  // followed by any more output (including EndOfStream) until the associated
  // output re-config is completed by a call to
  // CoreCodecMidStreamOutputBufferReConfigFinish().
  virtual void onCoreCodecMidStreamOutputConfigChange(
      bool output_re_config_required) = 0;

  virtual void onCoreCodecInputPacketDone(const CodecPacket* packet) = 0;

  virtual void onCoreCodecOutputPacket(CodecPacket* packet,
                                       bool error_detected_before,
                                       bool error_detected_during) = 0;

  virtual void onCoreCodecOutputEndOfStream(bool error_detected_before) = 0;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_EVENTS_H_
