// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/logs/media_type_converter_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaTypeConverterChannel messages.
class MediaTypeConverterFull : public ChannelHandler,
                               public media::logs::MediaTypeConverterChannel {
 public:
  MediaTypeConverterFull(const std::string& format);

  ~MediaTypeConverterFull() override;

  // ChannelHandler implementation.
  void HandleMessage(fidl::Message* message) override;

  // MediaTypeConverterChannel implementation.
  void BoundAs(uint64_t koid, const fidl::String& converter_type) override;

  void Config(media::MediaTypePtr input_type,
              media::MediaTypePtr output_type,
              uint64_t consumer_address,
              uint64_t producer_address) override;

 private:
  media::logs::MediaTypeConverterChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
