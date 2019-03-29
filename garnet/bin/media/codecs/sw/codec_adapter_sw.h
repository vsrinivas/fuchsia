// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_CODEC_ADAPTER_SW_H_
#define GARNET_BIN_MEDIA_CODECS_SW_CODEC_ADAPTER_SW_H_

#include <threads.h>
#include <optional>
#include <queue>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/synchronization/thread_annotations.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_input_item.h>

#include "mpsc_queue.h"

class CodecAdapterSW : public CodecAdapter {
 public:
  CodecAdapterSW(std::mutex& lock, CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterSW();

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  void CoreCodecInit(const fuchsia::media::FormatDetails&
                         initial_input_format_details) override;
  void CoreCodecStartStream() override;
  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details)
      override;
  void CoreCodecQueueInputPacket(CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;
  void CoreCodecStopStream() override;
  void CoreCodecConfigureBuffers(
      CodecPort port,
      const std::vector<std::unique_ptr<CodecPacket>>& packets) override;
  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;
  std::unique_ptr<const fuchsia::media::StreamOutputConfig>
  CoreCodecBuildNewOutputConfig(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      uint64_t new_output_format_details_version_ordinal,
      bool buffer_constraints_action_required) override;

 protected:
  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  virtual void ProcessInputLoop() = 0;

  // Releases any references to an output packet..
  virtual void UnreferenceOutputPacket(CodecPacket* packet) = 0;

  // Unreferences all buffers in use by client.
  virtual void UnreferenceClientBuffers() = 0;

  // Gracefully stops the input processing thread.
  virtual void BeginStopInputProcessing() = 0;

  // Releases any resources from the just-ended stream.
  virtual void CleanUpAfterStream() = 0;

  // Returns the format details of the output and the bytes needed to store each
  // output packet.
  virtual std::pair<fuchsia::media::FormatDetails, size_t>
  OutputFormatDetails() = 0;

  void WaitForInputProcessingLoopToEnd();

  BlockingMpscQueue<CodecInputItem> input_queue_;
  BlockingMpscQueue<CodecPacket*> free_output_packets_;

  uint64_t input_format_details_version_ordinal_;

  async::Loop input_processing_loop_;
  thrd_t input_processing_thread_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_CODEC_ADAPTER_SW_H_
