// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_ENCODE_FILE_ENCODER_CLIENT_H_
#define SRC_MEDIA_CODEC_EXAMPLES_ENCODE_FILE_ENCODER_CLIENT_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/media/test/codec_buffer.h>

#include <list>
#include <unordered_map>

// Single-threaded StreamProcessor client that sets up an encoder instance and produces
// encoded packets via callback.
class EncoderClient {
 public:
  ~EncoderClient();
  static fpromise::result<std::unique_ptr<EncoderClient>, zx_status_t> Create(
      fuchsia::mediacodec::CodecFactoryHandle codec_factory,
      fuchsia::sysmem::AllocatorHandle allocator, uint32_t bitrate, uint32_t gop_size,
      const std::string& mime_type);

  // Connects to codec factory and sets up an encoder stream processor with the given
  // image format as input.
  zx_status_t Start(fuchsia::sysmem::ImageFormat_2 image_format, uint32_t frame_rate);

  using OutputPacketHandler = fit::function<void(uint8_t* buffer, size_t len)>;
  void SetOutputPacketHandler(OutputPacketHandler handler) {
    output_packet_handler_ = std::move(handler);
  }

  // Callback will be called whenever there is a free input buffer to be filled. Should return the
  // number of bytes written to the buffer. Return 0 to inidicate end of stream.
  using InputBufferReadyHandler = fit::function<size_t(uint8_t* buffer, size_t len)>;
  void SetInputBufferReadyHandler(InputBufferReadyHandler handler) {
    input_buffer_ready_handler_ = std::move(handler);
  }

  using OutputEndOfStreamHandler = fit::function<void(void)>;
  void SetOutputEndOfStreamHandler(OutputEndOfStreamHandler handler) {
    output_end_of_stream_handler_ = std::move(handler);
  }

 private:
  EncoderClient(uint32_t bitrate, uint32_t gop_size, const std::string& mime_type);

  using BoundBufferCollectionCallback =
      fit::callback<void(fuchsia::sysmem::BufferCollectionTokenHandle&&)>;

  // Allocate new buffer collection, duplicating a token to it, and passing both to the callback.
  void CreateAndSyncBufferCollection(fuchsia::sysmem::BufferCollectionPtr& buffer_collection,
                                     BoundBufferCollectionCallback callback);

  // Common helper function to wait for buffers
  void BindAndSyncBufferCollection(fuchsia::sysmem::BufferCollectionPtr& buffer_collection,
                                   fuchsia::sysmem::BufferCollectionTokenHandle token,
                                   fuchsia::sysmem::BufferCollectionTokenHandle duplicated_token,
                                   BoundBufferCollectionCallback callback);

  // On Ok, contains the buffer collection info and negotiated packet count.
  using BufferCollectionResult =
      fpromise::result<std::pair<fuchsia::sysmem::BufferCollectionInfo_2, uint32_t>, zx_status_t>;
  using ConfigurePortBufferCollectionCallback = fit::callback<void(BufferCollectionResult)>;
  void ConfigurePortBufferCollection(
      fuchsia::sysmem::BufferCollectionPtr& buffer_collection,
      fuchsia::sysmem::BufferCollectionTokenHandle codec_sysmem_token, bool is_output,
      uint64_t new_buffer_lifetime_ordinal, uint64_t buffer_constraints_version_ordinal,
      ConfigurePortBufferCollectionCallback callback);

  void OnInputBuffersReady(BufferCollectionResult result);
  void OnOutputBuffersReady(BufferCollectionResult result);
  void FillInputBuffer(uint32_t buffer_index);
  void QueueInputPacket(uint32_t buffer_index);

  //
  // Events:
  //
  void OnStreamFailed(uint64_t stream_lifetime_ordinal, fuchsia::media::StreamError error);
  void OnInputConstraints(fuchsia::media::StreamBufferConstraints input_constraints);
  void OnFreeInputPacket(fuchsia::media::PacketHeader free_input_packet);
  void OnOutputConstraints(fuchsia::media::StreamOutputConstraints output_config);
  void OnOutputFormat(fuchsia::media::StreamOutputFormat output_format);
  void OnOutputPacket(fuchsia::media::Packet output_packet, bool error_detected_before,
                      bool error_detected_during);
  void OnOutputEndOfStream(uint64_t stream_lifetime_ordinal, bool error_detected_before);

  fuchsia::mediacodec::CodecFactoryPtr codec_factory_;
  fuchsia::media::StreamProcessorPtr codec_;
  fuchsia::sysmem::AllocatorPtr sysmem_;

  OutputPacketHandler output_packet_handler_;
  InputBufferReadyHandler input_buffer_ready_handler_;
  OutputEndOfStreamHandler output_end_of_stream_handler_;

  fuchsia::sysmem::BufferCollectionPtr input_buffer_collection_;
  fuchsia::sysmem::BufferCollectionPtr output_buffer_collection_;

  std::optional<fuchsia::media::StreamBufferConstraints> input_constraints_;
  std::optional<fuchsia::media::StreamOutputConstraints> last_output_constraints_;

  // The index into the vector is the same as packet_id, since we're running in
  // buffer-per-packet mode.
  std::vector<std::unique_ptr<CodecBuffer>> all_input_buffers_;
  std::vector<std::unique_ptr<CodecBuffer>> all_output_buffers_;
  uint32_t input_packet_count_ = 0;
  uint32_t output_packet_count_ = 0;
  uint32_t input_frame_size_ = 0;
  bool input_ended_ = false;

  uint32_t bitrate_ = 0;
  uint32_t gop_size_ = 0;
  std::string mime_type_;

  EncoderClient(const EncoderClient&) = delete;
  EncoderClient(EncoderClient&&) = delete;
  EncoderClient& operator=(const EncoderClient&) = delete;
  EncoderClient& operator=(EncoderClient&&) = delete;
};

#endif  // SRC_MEDIA_CODEC_EXAMPLES_ENCODE_FILE_ENCODER_CLIENT_H_
