// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_DECODER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_DECODER_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "src/lib/fxl/synchronization/thread_checker.h"
#include "src/media/playback/mediaplayer/decode/decoder.h"
#include "src/media/playback/mediaplayer/fidl/buffer_set.h"

namespace media_player {

// Fidl decoder as exposed by the codec factory service.
class FidlDecoder : public Decoder {
 public:
  // Creates a fidl decoder factory. Calls the callback with the initalized
  // decoder on success. Calls the callback with nullptr on failure.
  static void Create(ServiceProvider* service_provider, const StreamType& stream_type,
                     fuchsia::media::FormatDetails input_format_details,
                     fuchsia::media::StreamProcessorPtr decoder,
                     fit::function<void(std::shared_ptr<Decoder>)> callback);

  FidlDecoder(ServiceProvider* service_provider, const StreamType& stream_type,
              fuchsia::media::FormatDetails input_format_details);

  ~FidlDecoder() override;

  void Init(fuchsia::media::StreamProcessorPtr decoder, fit::function<void(bool)> callback);

  // Decoder implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

  void ConfigureConnectors() override;

  void OnInputConnectionReady(size_t input_index) override;

  void FlushInput(bool hold_frame, size_t input_index, fit::closure callback) override;

  void PutInputPacket(PacketPtr packet, size_t input_index) override;

  void OnOutputConnectionReady(size_t output_index) override;

  void FlushOutput(size_t output_index, fit::closure callback) override;

  void RequestOutputPacket() override;

  std::unique_ptr<StreamType> output_stream_type() const override;

 private:
  // Notifies that the decoder is viable. This method does nothing after the
  // first time it or |InitFailed| is called.
  void InitSucceeded();

  // Notifies that the decoder is not viable. This method does nothing after the
  // first time it or |InitSucceeded| is called.
  void InitFailed();

  // Requests an input packet when appropriate.
  void MaybeRequestInputPacket();

  // Handles failure of the connection to the outboard decoder.
  void OnConnectionFailed(zx_status_t error);

  // Handles the |OnStreamFailed| event from the outboard decoder.
  void OnStreamFailed(uint64_t stream_lifetime_ordinal, fuchsia::media::StreamError error);

  // Handles the |OnInputConstraints| event from the outboard decoder after
  // |ConfigureConnectors| is called.
  void OnInputConstraints(fuchsia::media::StreamBufferConstraints constraints);

  // Handles the |OnOutputConstraints| event from the outboard decoder.
  void OnOutputConstraints(fuchsia::media::StreamOutputConstraints config);

  // Handles the |OnOutputFormat| event from the outboard decoder.
  void OnOutputFormat(fuchsia::media::StreamOutputFormat format);

  // Handles the |OnOutputPacket| event from the outboard decoder.
  void OnOutputPacket(fuchsia::media::Packet output_packet, bool error_detected_before,
                      bool error_detected_during);

  // Handles the |OnOutputEndOfStream| event from the outboard decoder.
  void OnOutputEndOfStream(uint64_t stream_lifetime_ordinal, bool error_detected_before);

  // Handles the |OnFreeInputPacket| event from the outboard decoder.
  void OnFreeInputPacket(fuchsia::media::PacketHeader packet_header);

  // Determines if the output stream type has changed and takes action if it
  // has.
  void HandlePossibleOutputStreamTypeChange(const StreamType& old_type, const StreamType& new_type);

  FXL_DECLARE_THREAD_CHECKER(thread_checker_);

  ServiceProvider* service_provider_;
  StreamType::Medium medium_;
  fuchsia::media::StreamProcessorPtr outboard_decoder_;
  fuchsia::media::FormatDetails input_format_details_;
  fit::function<void(bool)> init_callback_;
  bool have_real_output_stream_type_ = false;
  std::unique_ptr<StreamType> output_stream_type_;
  std::unique_ptr<StreamType> revised_output_stream_type_;
  bool allocate_output_buffers_for_decoder_pending_ = false;
  uint64_t stream_lifetime_ordinal_ = 1;
  uint64_t output_format_details_version_ordinal_ = 0;
  bool end_of_input_stream_ = false;
  BufferSetManager input_buffers_;
  BufferSetManager output_buffers_;
  media::TimelineRate pts_rate_;
  int64_t next_pts_ = 0;
  bool flushing_ = true;

  // These are used temporarily while their respective |Sync| calls are pending.
  fuchsia::sysmem::BufferCollectionTokenPtr output_sysmem_token_;
  fuchsia::sysmem::BufferCollectionTokenPtr input_sysmem_token_;

  // Disallow copy and assign.
  FidlDecoder(const FidlDecoder&) = delete;
  FidlDecoder& operator=(const FidlDecoder&) = delete;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_DECODER_H_
