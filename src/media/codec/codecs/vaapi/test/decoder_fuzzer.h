// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_TEST_DECODER_FUZZER_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_TEST_DECODER_FUZZER_H_
#include <lib/fdio/directory.h>
#include <stdio.h>

#include <memory>
#include <thread>

#include "src/media/codec/codecs/test/test_codec_packets.h"
#include "src/media/codec/codecs/vaapi/codec_adapter_vaapi_decoder.h"
#include "src/media/codec/codecs/vaapi/codec_runner_app.h"
#include "src/media/codec/codecs/vaapi/vaapi_utils.h"
#include "vaapi_stubs.h"

class FakeCodecAdapterEvents : public CodecAdapterEvents {
 public:
  void onCoreCodecFailCodec(const char *format, ...) override;

  void onCoreCodecFailStream(fuchsia::media::StreamError error) override;

  void onCoreCodecResetStreamAfterCurrentFrame() override;

  void onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) override;

  void onCoreCodecOutputFormatChange() override;

  void onCoreCodecInputPacketDone(CodecPacket *packet) override;

  void onCoreCodecOutputPacket(CodecPacket *packet, bool error_detected_before,
                               bool error_detected_during) override;

  void onCoreCodecOutputEndOfStream(bool error_detected_before) override;

  void onCoreCodecLogEvent(
      media_metrics::StreamProcessorEvents2MetricDimensionEvent event_code) override;

  uint64_t fail_codec_count() const { return fail_codec_count_; }
  uint64_t fail_stream_count() const { return fail_stream_count_; }

  void set_codec_adapter(CodecAdapter *codec_adapter) { codec_adapter_ = codec_adapter; }

  void SetBufferInitializationCompleted();
  void WaitForIdle(size_t input_packet_count);

 private:
  CodecAdapter *codec_adapter_ = nullptr;
  uint64_t fail_codec_count_{};
  uint64_t fail_stream_count_{};

  std::mutex lock_;
  std::condition_variable cond_;

  std::vector<CodecPacket *> input_packets_done_;
  bool buffer_initialization_completed_ = false;
};

class VaapiFuzzerTestFixture {
 public:
  VaapiFuzzerTestFixture() = default;
  ~VaapiFuzzerTestFixture();

  void SetUp();
  void TearDown() { vaDefaultStubSetReturn(); }
  void RunFuzzer(std::string mime_type, const uint8_t *data, size_t size);

 protected:
  void CodecAndStreamInit(std::string mime_type);

  void CodecStreamStop();

  void ParseDataIntoInputPackets(const uint8_t *data, size_t size);

  void ConfigureOutputBuffers(uint32_t output_packet_count, size_t output_packet_size);

  std::mutex lock_;
  FakeCodecAdapterEvents events_;
  std::unique_ptr<CodecAdapterVaApiDecoder> decoder_;
  std::vector<std::unique_ptr<CodecPacketForTest>> input_packets_;
  std::vector<std::unique_ptr<CodecBufferForTest>> input_buffers_;
  TestBuffers test_buffers_;
  std::vector<std::unique_ptr<CodecPacket>> test_packets_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_TEST_DECODER_FUZZER_H_
