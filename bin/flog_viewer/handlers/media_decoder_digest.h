// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DECODER_DIGEST_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DECODER_DIGEST_H_

#include <vector>

#include "examples/flog_viewer/accumulator.h"
#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/media/logs/interfaces/media_decoder_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

class MediaDecoderAccumulator;

// Handler for MediaDecoderChannel messages, digest format.
class MediaDecoderDigest : public ChannelHandler,
                           public mojo::media::logs::MediaDecoderChannel {
 public:
  MediaDecoderDigest(const std::string& format);

  ~MediaDecoderDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(Message* message) override;

 private:
  // MediaDecoderChannel implementation.
  void Config(mojo::media::MediaTypePtr input_type,
              mojo::media::MediaTypePtr output_type,
              uint64_t consumer_address,
              uint64_t producer_address) override;

 private:
  mojo::media::logs::MediaDecoderChannelStub stub_;
  std::shared_ptr<MediaDecoderAccumulator> accumulator_;
};

// Status of a media decoder as understood by MediaDecoderDigest.
class MediaDecoderAccumulator : public Accumulator {
 public:
  MediaDecoderAccumulator();
  ~MediaDecoderAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  mojo::media::MediaTypePtr input_type_;
  mojo::media::MediaTypePtr output_type_;
  std::shared_ptr<Channel> consumer_channel_;
  std::shared_ptr<Channel> producer_channel_;

  friend class MediaDecoderDigest;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DECODER_DIGEST_H_
