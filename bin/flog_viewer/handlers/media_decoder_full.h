// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DECODER_FULL_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DECODER_FULL_H_

#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/media/logs/interfaces/media_decoder_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

// Handler for MediaDecoderChannel messages.
class MediaDecoderFull : public ChannelHandler,
                         public mojo::media::logs::MediaDecoderChannel {
 public:
  MediaDecoderFull(const std::string& format);

  ~MediaDecoderFull() override;

  // ChannelHandler implementation.
  void HandleMessage(Message* message) override;

  // MediaDecoderChannel implementation.
  void Config(mojo::media::MediaTypePtr input_type,
              mojo::media::MediaTypePtr output_type,
              uint64_t consumer_address,
              uint64_t producer_address) override;

 private:
  mojo::media::logs::MediaDecoderChannelStub stub_;
  bool terse_;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DECODER_FULL_H_
