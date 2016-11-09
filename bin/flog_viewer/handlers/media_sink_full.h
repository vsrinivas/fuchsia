// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/interfaces/logs/media_sink_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaSinkChannel messages.
class MediaSinkFull : public ChannelHandler,
                      public media::logs::MediaSinkChannel {
 public:
  MediaSinkFull(const std::string& format);

  ~MediaSinkFull() override;

  // ChannelHandler implementation.
  void HandleMessage(fidl::Message* message) override;

  // MediaSinkChannel implementation.
  void Config(media::MediaTypePtr input_type,
              media::MediaTypePtr output_type,
              uint64_t consumer_address,
              uint64_t producer_address) override;

 private:
  media::logs::MediaSinkChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
