// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_demux_full.h"

#include <iostream>

#include "apps/media/interfaces/logs/media_demux_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaDemuxFull::MediaDemuxFull(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse) {
  stub_.set_sink(this);
}

MediaDemuxFull::~MediaDemuxFull() {}

void MediaDemuxFull::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

void MediaDemuxFull::NewStream(uint32_t index,
                               media::MediaTypePtr type,
                               uint64_t producer_address) {
  std::cout << entry() << "MediaDemux.NewStream" << std::endl;
  std::cout << indent;
  std::cout << begl << "index: " << index << std::endl;
  std::cout << begl << "type: " << type;
  std::cout << begl << "producer_address: " << *AsChannel(producer_address)
            << std::endl;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
