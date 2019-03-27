// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_SINK_FEEDER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_SINK_FEEDER_H_

#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>

namespace media_player {
namespace test {

class SinkFeeder {
 public:
  SinkFeeder() = default;

  ~SinkFeeder() = default;

  zx_status_t Init(fuchsia::media::SimpleStreamSinkPtr sink, size_t size,
                   uint32_t frame_size, uint32_t max_packet_size,
                   uint32_t max_packet_count);

 private:
  void MaybeSendPacket();

  fuchsia::media::SimpleStreamSinkPtr sink_;
  uint32_t frame_size_;
  uint32_t bytes_remaining_;
  uint32_t max_packet_size_;
  int64_t position_ = 0;
  bool end_of_stream_sent_ = false;
  fzl::VmoMapper vmo_mapper_;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_SINK_FEEDER_H_
