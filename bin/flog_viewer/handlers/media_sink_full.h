// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_MEDIA_SINK_FULL_H_
#define APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_MEDIA_SINK_FULL_H_

#include "apps/media/interfaces/logs/media_sink_channel.mojom.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace mojo {
namespace flog {
namespace handlers {

// Handler for MediaSinkChannel messages.
class MediaSinkFull : public ChannelHandler,
                      public mojo::media::logs::MediaSinkChannel {
 public:
  MediaSinkFull(const std::string& format);

  ~MediaSinkFull() override;

  // ChannelHandler implementation.
  void HandleMessage(Message* message) override;

  // MediaSinkChannel implementation.
  void Config(mojo::media::MediaTypePtr input_type,
              mojo::media::MediaTypePtr output_type,
              uint64_t consumer_address,
              uint64_t producer_address) override;

 private:
  mojo::media::logs::MediaSinkChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
}  // namespace mojo

#endif  // APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_MEDIA_SINK_FULL_H_
