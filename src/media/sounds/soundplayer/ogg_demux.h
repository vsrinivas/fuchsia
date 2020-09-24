// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_SOUNDS_SOUNDPLAYER_OGG_DEMUX_H_
#define SRC_MEDIA_SOUNDS_SOUNDPLAYER_OGG_DEMUX_H_

#include <lib/fit/result.h>

#include <memory>

#include "src/media/sounds/soundplayer/opus_decoder.h"
#include "third_party/ogg/include/ogg/ogg.h"

namespace soundplayer {

class OggDemux {
 public:
  OggDemux();

  ~OggDemux();

  // Processes the file. |fd| must be positioned at the beginning of the file. This method does
  // not close |fd| regardless of the result, but will leave |fd| at an arbitrary position. The
  // first valid stream in the file is decoded.
  fit::result<Sound, zx_status_t> Process(int fd);

 private:
  class Stream {
   public:
    static std::unique_ptr<Stream> Create(int serial_number);

    explicit Stream(int serial_number);

    ~Stream();

    int serial_number() const { return serial_number_; }

    ogg_stream_state& state() { return state_; }

    OpusDecoder* decoder() { return decoder_.get(); }

    void SetDecoder(std::unique_ptr<OpusDecoder> decoder) { decoder_ = std::move(decoder); }

   private:
    int serial_number_;
    ogg_stream_state state_;
    std::unique_ptr<OpusDecoder> decoder_;
  };

  // Reads a page into |page_|. Returns true if a page is read successfully, false if the read
  // fails or we've reached end-of-file. If we've reached end-of-file, this method sets
  // |end_of_file_| to true.
  bool ReadPage(int fd);

  // Gets or creates the |Stream| for the given serial number. Returns null if the stream should be
  // ignored.
  Stream* GetOrCreateStream(int serial_number);

  // Gets the |Stream| for the given serial number. Returns null if the stream doesn't exist.
  Stream* GetStream(int serial_number);

  // Rejects the specified stream. Returns true if processing should continue, false if not.
  bool RejectStream(Stream* stream);

  // Handles a complete packet for the stream identified by |serial_number|. Returns false if the
  // packet was rejected and file processing should stop.
  bool OnPacket(const ogg_packet& packet, int serial_number);

  ogg_sync_state sync_state_;
  ogg_page page_;
  bool end_of_file_ = false;

  // For now, we only support one stream.
  std::unique_ptr<Stream> stream_;
};

}  // namespace soundplayer

#endif  // SRC_MEDIA_SOUNDS_SOUNDPLAYER_OGG_DEMUX_H_
