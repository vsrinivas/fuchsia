// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/flog_viewer/handlers/media/media_demux_full.h"

#include <iostream>

#include "examples/flog_viewer/flog_viewer.h"
#include "examples/flog_viewer/handlers/media/media_formatting.h"
#include "mojo/services/media/logs/interfaces/media_demux_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

MediaDemuxFull::MediaDemuxFull(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse) {
  stub_.set_sink(this);
}

MediaDemuxFull::~MediaDemuxFull() {}

void MediaDemuxFull::HandleMessage(Message* message) {
  stub_.Accept(message);
}

void MediaDemuxFull::NewStream(uint32_t index,
                               mojo::media::MediaTypePtr type,
                               uint64_t producer_address) {
  std::cout << entry() << "MediaDemux.NewStream" << std::endl;
  std::cout << indent;
  std::cout << begl << "index: " << index << std::endl;
  std::cout << begl << "type: " << type;
  std::cout << begl << "producer_address: " << *AsChannel(producer_address)
            << std::endl;
  std::cout << outdent;
}

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo
