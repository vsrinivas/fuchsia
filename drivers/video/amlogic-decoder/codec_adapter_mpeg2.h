// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_MPEG2_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_MPEG2_H_

#include "codec_adapter.h"

class AmlogicVideo;
class CodecAdapterMpeg2 : public CodecAdapter {
 public:
  explicit CodecAdapterMpeg2(std::mutex& lock,
                             CodecAdapterEvents* codec_adapter_events,
                             AmlogicVideo* video);
  ~CodecAdapterMpeg2();

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
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

 private:
  AmlogicVideo* video_ = nullptr;

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CodecAdapterMpeg2);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_MPEG2_H_
