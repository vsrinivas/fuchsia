// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_SINK_DIGEST_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_SINK_DIGEST_H_

#include <vector>

#include "examples/flog_viewer/accumulator.h"
#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/media/logs/interfaces/media_sink_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

class MediaSinkAccumulator;

// Handler for MediaSinkChannel messages, digest format.
class MediaSinkDigest : public ChannelHandler,
                        public mojo::media::logs::MediaSinkChannel {
 public:
  MediaSinkDigest(const std::string& format);

  ~MediaSinkDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(Message* message) override;

 private:
  // MediaSinkChannel implementation.
  void Config(mojo::media::MediaTypePtr input_type,
              mojo::media::MediaTypePtr output_type,
              uint64_t consumer_address,
              uint64_t producer_address) override;

 private:
  mojo::media::logs::MediaSinkChannelStub stub_;
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
  mojo::media::MediaTypePtr input_type_;
  mojo::media::MediaTypePtr output_type_;
  std::shared_ptr<Channel> consumer_channel_;
  std::shared_ptr<Channel> producer_channel_;

  friend class MediaSinkDigest;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_SINK_DIGEST_H_
