// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_renderer_full.h"

#include <iostream>

#include "apps/media/services/logs/media_renderer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaRendererFull::MediaRendererFull(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse) {
  stub_.set_sink(this);
}

MediaRendererFull::~MediaRendererFull() {}

void MediaRendererFull::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

void MediaRendererFull::BoundAs(uint64_t koid) {
  std::cout << entry() << "MediaRenderer.BoundAs" << std::endl;
  std::cout << indent;
  std::cout << begl << "koid: " << AsKoid(koid) << std::endl;
  std::cout << outdent;
}

void MediaRendererFull::Config(
    fidl::Array<media::MediaTypeSetPtr> supported_types,
    uint64_t consumer_address) {
  std::cout << entry() << "MediaRenderer.Config" << std::endl;
  std::cout << indent;
  std::cout << begl << "supported_types: " << supported_types;
  std::cout << begl << "consumer_address: " << *AsChannel(consumer_address)
            << std::endl;
  std::cout << outdent;
}

void MediaRendererFull::SetMediaType(media::MediaTypePtr type) {
  std::cout << entry() << "MediaRenderer.SetMediaType" << std::endl;
  std::cout << indent;
  std::cout << begl << "type: " << type;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
