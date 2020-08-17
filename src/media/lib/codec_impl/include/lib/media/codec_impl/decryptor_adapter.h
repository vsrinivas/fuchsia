// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_DECRYPTOR_ADAPTER_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_DECRYPTOR_ADAPTER_H_

#include <fuchsia/media/drm/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/media/codec_impl/codec_adapter.h>

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "src/media/lib/mpsc_queue/mpsc_queue.h"

class DecryptorAdapter : public CodecAdapter {
 public:
  explicit DecryptorAdapter(std::mutex& lock, CodecAdapterEvents* codec_adapter_events);
  explicit DecryptorAdapter(std::mutex& lock, CodecAdapterEvents* codec_adapter_events,
                            inspect::Node inspect_node);

  ~DecryptorAdapter() = default;

  // CodecAdapter implementations
  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  bool IsCoreCodecMappedBufferUseful(CodecPort port) override;
  bool IsCoreCodecHwBased(CodecPort port) override;

  void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) override;
  void CoreCodecSetSecureMemoryMode(
      CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) override;
  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override;
  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override;
  void CoreCodecStartStream() override;
  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details) override;
  void CoreCodecQueueInputPacket(CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;
  void CoreCodecStopStream() override;
  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;
  void CoreCodecConfigureBuffers(CodecPort port,
                                 const std::vector<std::unique_ptr<CodecPacket>>& packets) override;
  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;
  std::unique_ptr<const fuchsia::media::StreamBufferConstraints> CoreCodecBuildNewInputConstraints()
      override;
  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> CoreCodecBuildNewOutputConstraints(
      uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
      bool buffer_constraints_action_required) override;
  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;

  // Disallow move, copy and assign.
  DecryptorAdapter(const DecryptorAdapter&) = delete;
  DecryptorAdapter(DecryptorAdapter&&) = delete;
  DecryptorAdapter& operator=(const DecryptorAdapter&) = delete;
  DecryptorAdapter& operator=(DecryptorAdapter&&) = delete;

 protected:
  struct EncryptionParams {
    std::string scheme;
    std::vector<uint8_t> key_id;
    std::vector<uint8_t> init_vector;
    std::optional<fuchsia::media::EncryptionPattern> pattern;
    std::vector<fuchsia::media::SubsampleEntry> subsamples;
  };

  struct InputBuffer {
    const uint8_t* data;
    uint32_t data_length;
  };

  struct ClearOutputBuffer {
    uint8_t* data;
    uint32_t data_length;
  };

  struct SecureOutputBuffer {
    zx::unowned_vmo vmo;
    uint32_t data_offset;
    uint32_t data_length;
  };

  using OutputBuffer = std::variant<ClearOutputBuffer, SecureOutputBuffer>;

  // Decryptor interface
  virtual std::optional<fuchsia::media::StreamError> Decrypt(const EncryptionParams& params,
                                                             const InputBuffer& input,
                                                             const OutputBuffer& output,
                                                             CodecPacket* output_packet) = 0;

  // GetSecureOutputMemoryConstraints
  //
  // If the specialized Decryptor supports working with secure memory, it should override this
  // method to provide the proper coherency and permitted heaps that it can support.
  virtual fuchsia::sysmem::BufferMemoryConstraints GetSecureOutputMemoryConstraints() const;

  bool is_secure() const { return secure_mode_; }

 private:
  struct InspectProperties {
    struct PortProperties {
      PortProperties(inspect::Node& parent, const std::string& port_name);

      inspect::Node node;

      inspect::UintProperty buffer_count;
      inspect::UintProperty packet_count;
    };

    explicit InspectProperties(inspect::Node node);

    inspect::Node node;

    inspect::BoolProperty secure_mode;
    inspect::StringProperty scheme;
    inspect::ByteVectorProperty key_id;
    std::optional<PortProperties> input;
    std::optional<PortProperties> output;
  };

  void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);
  void PostToInputProcessingThread(fit::closure to_run);
  void QueueInputItem(CodecInputItem input_item);
  CodecInputItem DequeueInputItem();
  void ProcessInput();
  bool UpdateEncryptionParams(const fuchsia::media::EncryptedFormat& encrypted_format);
  void OnCoreCodecFailStream(fuchsia::media::StreamError error);

  std::optional<InspectProperties> inspect_properties_;
  EncryptionParams encryption_params_;
  bool secure_mode_ = false;

  // Only StreamControl ever adds anything to input_queue_.  Only
  // processing_thread_ ever removes anything from input_queue_, including when
  // stopping.
  async::Loop input_processing_loop_;
  thrd_t input_processing_thread_ = 0;
  bool is_process_input_queued_ = false;

  // Skip any further processing in ProcessInput().
  bool is_cancelling_input_processing_ = false;

  std::vector<const CodecBuffer*> all_output_buffers_;
  BlockingMpscQueue<const CodecBuffer*> free_output_buffers_;
  BlockingMpscQueue<CodecPacket*> free_output_packets_;

  bool is_stream_failed_ = false;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_DECRYPTOR_ADAPTER_H_
