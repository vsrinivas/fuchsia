// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FIDL_FIDL_DECODER_H_
#define GARNET_BIN_MEDIAPLAYER_FIDL_FIDL_DECODER_H_

#include <memory>

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "garnet/bin/mediaplayer/decode/decoder.h"
#include "garnet/bin/mediaplayer/fidl/buffer_set.h"

namespace media_player {

// Fidl decoder as exposed by the codec factory service.
class FidlDecoder : public Decoder {
 public:
  // Creates a fidl decoder factory. Calls the callback with the initalized
  // decoder on success. Calls the callback with nullptr on failure.
  static void Create(
      fuchsia::mediacodec::CodecFormatDetails input_format_details,
      fuchsia::mediacodec::CodecPtr decoder,
      fit::function<void(std::shared_ptr<Decoder>)> callback);

  FidlDecoder(fuchsia::mediacodec::CodecFormatDetails input_format_details);

  ~FidlDecoder() override;

  void Init(fuchsia::mediacodec::CodecPtr decoder,
            fit::function<void(bool)> callback);

  // Decoder implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

  void GetConfiguration(size_t* input_count, size_t* output_count) override;

  void FlushInput(bool hold_frame, size_t input_index,
                  fit::closure callback) override;

  std::shared_ptr<PayloadAllocator> allocator_for_input(
      size_t input_index) override;

  void PutInputPacket(PacketPtr packet, size_t input_index) override;

  void FlushOutput(size_t output_index, fit::closure callback) override;

  void RequestOutputPacket() override;

  std::unique_ptr<StreamType> output_stream_type() const override;

 private:
  class DecoderPacket : public Packet {
   public:
    static PacketPtr Create(int64_t pts, media::TimelineRate pts_rate,
                            size_t size, void* payload,
                            uint64_t buffer_lifetime_ordinal,
                            uint32_t buffer_index, FidlDecoder* owner) {
      return std::make_shared<DecoderPacket>(pts, pts_rate, size, payload,
                                             buffer_lifetime_ordinal,
                                             buffer_index, owner);
    }

    ~DecoderPacket() override;

    DecoderPacket(int64_t pts, media::TimelineRate pts_rate, size_t size,
                  void* payload, uint64_t buffer_lifetime_ordinal,
                  uint32_t buffer_index, FidlDecoder* owner)
        : Packet(pts, pts_rate, true, false, size, payload),
          buffer_lifetime_ordinal_(buffer_lifetime_ordinal),
          buffer_index_(buffer_index),
          owner_(owner) {
      FXL_DCHECK(size > 0);
      FXL_DCHECK(payload);
    }

   private:
    uint64_t buffer_lifetime_ordinal_;
    uint32_t buffer_index_;
    FidlDecoder* owner_;
  };

  // Notifies that the decoder is viable. This method does nothing after the
  // first time it or |InitFailed| is called.
  void InitSucceeded();

  // Notifies that the decoder is not viable. This method does nothing after the
  // first time it or |InitSucceeded| is called.
  void InitFailed();

  // Handles failure of the connection to the outboard decoder.
  void OnConnectionFailed();

  // Handles the |OnStreamFailed| event from the outboard decoder.
  void OnStreamFailed(uint64_t stream_lifetime_ordinal);

  // Handles the |OnInputConstraints| event from the outboard decoder.
  void OnInputConstraints(
      fuchsia::mediacodec::CodecBufferConstraints constraints);

  // Handles the |OnOutputConfig| event from the outboard decoder.
  void OnOutputConfig(fuchsia::mediacodec::CodecOutputConfig config);

  // Handles the |OnOutputPacket| event from the outboard decoder.
  void OnOutputPacket(fuchsia::mediacodec::CodecPacket output_packet,
                      bool error_detected_before, bool error_detected_during);

  // Handles the |OnOutputEndOfStream| event from the outboard decoder.
  void OnOutputEndOfStream(uint64_t stream_lifetime_ordinal,
                           bool error_detected_before);

  // Handles the |OnFreeInputPacket| event from the outboard decoder.
  void OnFreeInputPacket(fuchsia::mediacodec::CodecPacketHeader packet_header);

  // Recycles an output packet back to the outboard decoder.
  void RecycleOutputPacket(uint64_t buffer_ordinal, uint32_t buffer_index);

  // Determines if the output stream type has changed and takes action if it
  // has.
  void HandlePossibleOutputStreamTypeChange(const StreamType& old_type,
                                            const StreamType& new_type);

  fuchsia::mediacodec::CodecPtr outboard_decoder_;
  fuchsia::mediacodec::CodecFormatDetails input_format_details_;
  fit::function<void(bool)> init_callback_;
  std::unique_ptr<StreamType> stream_type_;
  std::unique_ptr<StreamType> revised_stream_type_;
  uint64_t stream_lifetime_ordinal_ = 1;
  uint64_t output_format_details_version_ordinal_ = 0;
  bool end_of_input_stream_ = false;
  BufferSetManager input_buffers_;
  BufferSetManager output_buffers_;
  bool update_oob_bytes_ = false;
  media::TimelineRate pts_rate_;
  int64_t next_pts_ = 0;
  bool flushing_ = true;

  // Disallow copy and assign.
  FidlDecoder(const FidlDecoder&) = delete;
  FidlDecoder& operator=(const FidlDecoder&) = delete;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FIDL_FIDL_DECODER_H_
