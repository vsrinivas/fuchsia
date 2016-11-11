// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/services/logs/media_sink_channel.fidl.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

class MediaSinkAccumulator;

// Handler for MediaSinkChannel messages, digest format.
class MediaSinkDigest : public ChannelHandler,
                        public media::logs::MediaSinkChannel {
 public:
  MediaSinkDigest(const std::string& format);

  ~MediaSinkDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaSinkChannel implementation.
  void Config(media::MediaTypePtr input_type,
              media::MediaTypePtr output_type,
              uint64_t consumer_address,
              uint64_t producer_address) override;

 private:
  media::logs::MediaSinkChannelStub stub_;
  std::shared_ptr<MediaSinkAccumulator> accumulator_;
};

// Status of a media sink as understood by MediaSinkDigest.
class MediaSinkAccumulator : public Accumulator {
 public:
  MediaSinkAccumulator();
  ~MediaSinkAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  media::MediaTypePtr input_type_;
  media::MediaTypePtr output_type_;
  std::shared_ptr<Channel> consumer_channel_;
  std::shared_ptr<Channel> producer_channel_;

  friend class MediaSinkDigest;
};

}  // namespace handlers
}  // namespace flog
