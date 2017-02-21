// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/logs/media_source_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaSourceChannel messages.
class MediaSourceFull : public ChannelHandler,
                        public media::logs::MediaSourceChannel {
 public:
  MediaSourceFull(const std::string& format);

  ~MediaSourceFull() override;

  // ChannelHandler implementation.
  void HandleMessage(fidl::Message* message) override;

  // MediaSourceChannel implementation.
  void BoundAs(uint64_t koid) override;

  void CreatedDemux(uint64_t related_koid) override;

  void NewStream(uint32_t index,
                 media::MediaTypePtr output_type,
                 fidl::Array<uint64_t> converter_koids) override;

 private:
  media::logs::MediaSourceChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
