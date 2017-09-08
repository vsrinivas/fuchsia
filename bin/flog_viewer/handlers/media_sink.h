// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "lib/media/fidl/logs/media_sink_channel.fidl.h"
#include "garent/bin/flog_viewer/accumulator.h"
#include "garent/bin/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

class MediaSinkAccumulator;

// Handler for MediaSinkChannel messages.
class MediaSink : public ChannelHandler, public media::logs::MediaSinkChannel {
 public:
  MediaSink(const std::string& format);

  ~MediaSink() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaSinkChannel implementation.
  void BoundAs(uint64_t koid) override;

  void Config(media::MediaTypePtr input_type,
              media::MediaTypePtr output_type,
              fidl::Array<uint64_t> converter_koids,
              uint64_t renderer_koid) override;

 private:
  media::logs::MediaSinkChannelStub stub_;
  std::shared_ptr<MediaSinkAccumulator> accumulator_;
};

// Status of a media sink as understood by MediaSink.
class MediaSinkAccumulator : public Accumulator {
 public:
  MediaSinkAccumulator();
  ~MediaSinkAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  media::MediaTypePtr input_type_;
  media::MediaTypePtr output_type_;
  std::vector<ChildBinding> converters_;
  ChildBinding renderer_;

  friend class MediaSink;
};

}  // namespace handlers
}  // namespace flog
