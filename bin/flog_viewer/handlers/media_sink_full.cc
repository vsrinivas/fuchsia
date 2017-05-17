// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_sink_full.h"

#include <iostream>

#include "apps/media/services/logs/media_sink_channel.fidl.h"
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

void MediaSinkFull::BoundAs(uint64_t koid) {
  std::cout << entry() << "MediaSink.BoundAs" << std::endl;
  std::cout << indent;
  std::cout << begl << "koid: " << AsKoid(koid) << std::endl;
  std::cout << outdent;
}

void MediaSinkFull::Config(media::MediaTypePtr input_type,
                           media::MediaTypePtr output_type,
                           fidl::Array<uint64_t> converter_koids,
                           uint64_t renderer_koid) {
  std::cout << entry() << "MediaSink.Config" << std::endl;
  std::cout << indent;
  std::cout << begl << "input_type: " << input_type;
  std::cout << begl << "output_type: " << output_type;
  std::cout << begl << "converter_koids: " << converter_koids;
  std::cout << begl << "renderer_koid: " << renderer_koid << std::endl;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
