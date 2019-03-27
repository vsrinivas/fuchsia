// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FIDL_FIDL_DECODER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FIDL_FIDL_DECODER_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include "lib/fxl/synchronization/thread_checker.h"
#include "src/media/playback/mediaplayer_tmp/decode/decoder.h"
#include "src/media/playback/mediaplayer_tmp/fidl/buffer_set.h"

namespace media_player {

// Fidl decoder as exposed by the codec factory service.
class FidlDecoder : public Decoder {
 public:
  // Creates a fidl decoder factory. Calls the callback with the initalized
  // decoder on success. Calls the callback with nullptr on failure.
  static void Create(const StreamType& stream_type,
                     fuchsia::media::FormatDetails input_format_details,
                     fuchsia::media::StreamProcessorPtr decoder,
                     fit::function<void(std::shared_ptr<Decoder>)> callback);

  FidlDecoder(const StreamType& stream_type,
              fuchsia::media::FormatDetails input_format_details);

  ~FidlDecoder() override;

  void Init(fuchsia::media::StreamProcessorPtr decoder,
            fit::function<void(bool)> callback);

  // Decoder implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

  void ConfigureConnectors() override;

  void OnInputConnectionReady(size_t input_index) override;

  void FlushInput(bool hold_frame, size_t input_index,
                  fit::closure callback) override;

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

  // Configures the input as appropriate. |ConfigureConnectors| calls this as
  // does |OnInputConstraints|. If |constraints| is supplied, this method will
  // either cache the value (if the node isn't ready) or use it to configure
  // the input (if the node is ready). If |constraints| is null, there are
  // cached constraints and the node is ready, this method will configure the
  // input with the cached constraints and clear the cache.
  void MaybeConfigureInput(
      fuchsia::media::StreamBufferConstraints* constraints);

  // Adds input buffers to the outboard decoder. This method must not be called
  // until the input connection is ready.
  void AddInputBuffers();

  // Configures the output as appropriate. |ConfigureConnectors| calls this as
  // does |OnOutputConfig|. If |constraints| is supplied, this method will
  // either cache the value (if the node isn't ready) or use it to configure
  // the output (if the node is ready). If |constraints| is null, there are
  // cached constraints and the node is ready, this method will configure the
  // output with the cached constraints and clear the cache.
  void MaybeConfigureOutput(
      fuchsia::media::StreamBufferConstraints* constraints);

  // Adds output buffers to the outboard decoder. This method must not be called
  // until the output connection is ready.
  void AddOutputBuffers();

  // Requests an input packet when appropriate.
  void MaybeRequestInputPacket();

  // Handles failure of the connection to the outboard decoder.
  void OnConnectionFailed(zx_status_t error);

  // Handles the |OnStreamFailed| event from the outboard decoder.
  void OnStreamFailed(uint64_t stream_lifetime_ordinal);

  // Handles the |OnInputConstraints| event from the outboard decoder after
  // |ConfigureConnectors| is called.
  void OnInputConstraints(fuchsia::media::StreamBufferConstraints constraints);

  // Handles the |OnOutputConfig| event from the outboard decoder.
  void OnOutputConfig(fuchsia::media::StreamOutputConfig config);

  // Handles the |OnOutputPacket| event from the outboard decoder.
  void OnOutputPacket(fuchsia::media::Packet output_packet,
                      bool error_detected_before, bool error_detected_during);

  // Handles the |OnOutputEndOfStream| event from the outboard decoder.
  void OnOutputEndOfStream(uint64_t stream_lifetime_ordinal,
                           bool error_detected_before);

  // Handles the |OnFreeInputPacket| event from the outboard decoder.
  void OnFreeInputPacket(fuchsia::media::PacketHeader packet_header);

  // Determines if the output stream type has changed and takes action if it
  // has.
  void HandlePossibleOutputStreamTypeChange(const StreamType& old_type,
                                            const StreamType& new_type);

  FXL_DECLARE_THREAD_CHECKER(thread_checker_);

  StreamType::Medium medium_;
  fuchsia::media::StreamProcessorPtr outboard_decoder_;
  fuchsia::media::FormatDetails input_format_details_;
  fit::function<void(bool)> init_callback_;
  bool have_real_output_stream_type_ = false;
  uint32_t pre_stream_type_packet_requests_remaining_ = 10;
  std::unique_ptr<StreamType> output_stream_type_;
  std::unique_ptr<StreamType> revised_output_stream_type_;
  bool add_input_buffers_pending_ = false;
  bool add_output_buffers_pending_ = false;
  bool output_vmos_physically_contiguous_ = false;
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

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FIDL_FIDL_DECODER_H_
