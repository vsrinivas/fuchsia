// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/flog_viewer/channel_handler.h"
#include "lib/media/fidl/logs/media_source_channel.fidl.h"

namespace flog {
namespace handlers {

class MediaSourceAccumulator;

// Handler for MediaSourceChannel messages.
class MediaSource : public ChannelHandler,
                    public media::logs::MediaSourceChannel {
 public:
  MediaSource(const std::string& format);

  ~MediaSource() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

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
  std::shared_ptr<MediaSourceAccumulator> accumulator_;
};

// Status of a media source as understood by MediaSource.
class MediaSourceAccumulator : public Accumulator {
 public:
  struct Stream {
    Stream();
    ~Stream();

    Stream(Stream&& other) {
      output_type_ = std::move(other.output_type_);
      converters_ = other.converters_;
    }

    explicit operator bool() const { return !output_type_.is_null(); };

    media::MediaTypePtr output_type_;
    std::vector<ChildBinding> converters_;
  };

  MediaSourceAccumulator();
  ~MediaSourceAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  ChildBinding demux_;
  std::vector<Stream> streams_;

  friend class MediaSource;
};

}  // namespace handlers
}  // namespace flog
