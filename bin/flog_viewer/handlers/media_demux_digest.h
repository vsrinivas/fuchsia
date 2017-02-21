// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/services/logs/media_demux_channel.fidl.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

class MediaDemuxAccumulator;

// Handler for MediaDemuxChannel messages, digest format.
class MediaDemuxDigest : public ChannelHandler,
                         public media::logs::MediaDemuxChannel {
 public:
  MediaDemuxDigest(const std::string& format);

  ~MediaDemuxDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaDemuxChannel implementation.
  void BoundAs(uint64_t koid) override;

  void NewStream(uint32_t index,
                 media::MediaTypePtr type,
                 uint64_t producer_address) override;

 private:
  media::logs::MediaDemuxChannelStub stub_;
  std::shared_ptr<MediaDemuxAccumulator> accumulator_;
};

// Status of a media demux as understood by MediaDemuxDigest.
class MediaDemuxAccumulator : public Accumulator {
 public:
  struct Stream {
    Stream();
    ~Stream();

    Stream(Stream&& other) {
      type_ = std::move(other.type_);
      producer_channel_ = other.producer_channel_;
    }

    explicit operator bool() const { return !type_.is_null(); };

    media::MediaTypePtr type_;
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

}  // namespace handlers
}  // namespace flog
