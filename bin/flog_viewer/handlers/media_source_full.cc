// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_source_full.h"

#include <iostream>

#include "apps/media/services/logs/media_source_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaSourceFull::MediaSourceFull(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse) {
  stub_.set_sink(this);
}

MediaSourceFull::~MediaSourceFull() {}

void MediaSourceFull::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

void MediaSourceFull::BoundAs(uint64_t koid) {
  std::cout << entry() << "MediaSource.BoundAs" << std::endl;
  std::cout << indent;
  std::cout << begl << "koid: " << AsKoid(koid) << std::endl;
  std::cout << outdent;
}

void MediaSourceFull::CreatedDemux(uint64_t related_koid) {
  std::cout << entry() << "MediaSource.CreatedDemux" << std::endl;
  std::cout << indent;
  std::cout << begl << "related_koid: " << AsKoid(related_koid) << std::endl;
  std::cout << outdent;
}

void MediaSourceFull::NewStream(uint32_t index,
                                media::MediaTypePtr output_type,
                                fidl::Array<uint64_t> converter_koids) {
  std::cout << entry() << "MediaSource.NewStream" << std::endl;
  std::cout << indent;
  std::cout << begl << "index: " << index << std::endl;
  std::cout << begl << "output_type: " << output_type;
  std::cout << begl << "converter_koids: " << converter_koids;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
