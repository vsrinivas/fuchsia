// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/interfaces/logs/media_decoder_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaDecoderChannel messages.
class MediaDecoderFull : public ChannelHandler,
                         public media::logs::MediaDecoderChannel {
 public:
  MediaDecoderFull(const std::string& format);

  ~MediaDecoderFull() override;

  // ChannelHandler implementation.
  void HandleMessage(fidl::Message* message) override;

  // MediaDecoderChannel implementation.
  void Config(media::MediaTypePtr input_type,
              media::MediaTypePtr output_type,
              uint64_t consumer_address,
              uint64_t producer_address) override;

 private:
  media::logs::MediaDecoderChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
