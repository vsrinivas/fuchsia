// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/logs/media_demux_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaDemuxChannel messages.
class MediaDemuxFull : public ChannelHandler,
                       public media::logs::MediaDemuxChannel {
 public:
  MediaDemuxFull(const std::string& format);

  ~MediaDemuxFull() override;

  // ChannelHandler implementation.
  void HandleMessage(fidl::Message* message) override;

  // MediaDemuxChannel implementation.
  void NewStream(uint32_t index,
                 media::MediaTypePtr type,
                 uint64_t producer_address) override;

 private:
  media::logs::MediaDemuxChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
