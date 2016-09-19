// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DEMUX_FULL_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DEMUX_FULL_H_

#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/media/logs/interfaces/media_demux_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

// Handler for MediaDemuxChannel messages.
class MediaDemuxFull : public ChannelHandler,
                       public mojo::media::logs::MediaDemuxChannel {
 public:
  MediaDemuxFull(const std::string& format);

  ~MediaDemuxFull() override;

  // ChannelHandler implementation.
  void HandleMessage(Message* message) override;

  // MediaDemuxChannel implementation.
  void NewStream(uint32_t index,
                 mojo::media::MediaTypePtr type,
                 uint64_t producer_address) override;

 private:
  mojo::media::logs::MediaDemuxChannelStub stub_;
  bool terse_;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_DEMUX_FULL_H_
