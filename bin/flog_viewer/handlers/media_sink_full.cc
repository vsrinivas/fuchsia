// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_sink_full.h"

#include <iostream>

#include "apps/media/interfaces/logs/media_sink_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaSinkFull::MediaSinkFull(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse) {
  stub_.set_sink(this);
}

MediaSinkFull::~MediaSinkFull() {}

void MediaSinkFull::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

void MediaSinkFull::Config(media::MediaTypePtr input_type,
                           media::MediaTypePtr output_type,
                           uint64_t consumer_address,
                           uint64_t producer_address) {
  std::cout << entry() << "MediaSink.Config" << std::endl;
  std::cout << indent;
  std::cout << begl << "input_type: " << input_type;
  std::cout << begl << "output_type: " << output_type;
  std::cout << begl << "consumer_address: " << *AsChannel(consumer_address)
            << std::endl;
  std::cout << begl << "producer_address: " << *AsChannel(producer_address)
            << std::endl;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
