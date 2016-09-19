// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DEMUX_DIGEST_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DEMUX_DIGEST_H_

#include <vector>

#include "examples/flog_viewer/accumulator.h"
#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/media/logs/interfaces/media_demux_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

class MediaDemuxAccumulator;

// Handler for MediaDemuxChannel messages, digest format.
class MediaDemuxDigest : public ChannelHandler,
                         public mojo::media::logs::MediaDemuxChannel {
 public:
  MediaDemuxDigest(const std::string& format);

  ~MediaDemuxDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(Message* message) override;

 private:
  // MediaDemuxChannel implementation.
  void NewStream(uint32_t index,
                 mojo::media::MediaTypePtr type,
                 uint64_t producer_address) override;

 private:
  mojo::media::logs::MediaDemuxChannelStub stub_;
  std::shared_ptr<MediaDemuxAccumulator> accumulator_;
};

// Status of a media demux as understood by MediaDemuxDigest.
class MediaDemuxAccumulator : public Accumulator {
 public:
  struct Stream {
    Stream();
    ~Stream();

    Stream(Stream&& other) {
      type_ = other.type_.Pass();
      producer_channel_ = other.producer_channel_;
    }

    explicit operator bool() const { return !type_.is_null(); };

    mojo::media::MediaTypePtr type_;
    std::shared_ptr<Channel> producer_channel_;
  };

  MediaDemuxAccumulator();
  ~MediaDemuxAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  std::vector<Stream> streams_;

  friend class MediaDemuxDigest;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DEMUX_DIGEST_H_
