// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/services/logs/media_renderer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

class MediaRendererAccumulator;

// Handler for MediaRendererChannel messages, digest format.
class MediaRendererDigest : public ChannelHandler,
                            public media::logs::MediaRendererChannel {
 public:
  MediaRendererDigest(const std::string& format);

  ~MediaRendererDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaRendererChannel implementation.
  void BoundAs(uint64_t koid) override;

  void Config(fidl::Array<media::MediaTypeSetPtr> supported_types,
              uint64_t consumer_address) override;

  void SetMediaType(media::MediaTypePtr type) override;

 private:
  media::logs::MediaRendererChannelStub stub_;
  std::shared_ptr<MediaRendererAccumulator> accumulator_;
};

// Status of a media type converter as understood by MediaRendererDigest.
class MediaRendererAccumulator : public Accumulator {
 public:
  MediaRendererAccumulator();
  ~MediaRendererAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  fidl::Array<media::MediaTypeSetPtr> supported_types_;
  std::shared_ptr<Channel> consumer_channel_;
  media::MediaTypePtr type_;

  friend class MediaRendererDigest;
};

}  // namespace handlers
}  // namespace flog
