// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/logs/media_renderer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaRendererChannel messages.
class MediaRendererFull : public ChannelHandler,
                          public media::logs::MediaRendererChannel {
 public:
  MediaRendererFull(const std::string& format);

  ~MediaRendererFull() override;

  // ChannelHandler implementation.
  void HandleMessage(fidl::Message* message) override;

  // MediaRendererChannel implementation.
  void BoundAs(uint64_t koid) override;

  void Config(fidl::Array<media::MediaTypeSetPtr> supported_types,
              uint64_t consumer_address) override;

  void SetMediaType(media::MediaTypePtr type) override;

 private:
  media::logs::MediaRendererChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
