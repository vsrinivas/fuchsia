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

void MediaRendererFull::PrimeRequested() {
  std::cout << entry() << "MediaRenderer.PrimeRequested" << std::endl;
}

void MediaRendererFull::CompletingPrime() {
  std::cout << entry() << "MediaRenderer.CompletingPrime" << std::endl;
}

void MediaRendererFull::ScheduleTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  std::cout << entry() << "MediaRenderer.ScheduleTimelineTransform"
            << std::endl;
  std::cout << indent;
  std::cout << begl << "timeline_transform: " << timeline_transform
            << std::endl;
  std::cout << outdent;
}

void MediaRendererFull::ApplyTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  std::cout << entry() << "MediaRenderer.ApplyTimelineTransform" << std::endl;
  std::cout << indent;
  std::cout << begl << "timeline_transform: " << timeline_transform
            << std::endl;
  std::cout << outdent;
}

void MediaRendererFull::EngagePacket(int64_t current_pts,
                                     int64_t packet_pts,
                                     uint64_t packet_label) {
  std::cout << entry() << "MediaRenderer.EngagePacket" << std::endl;
  std::cout << indent;
  std::cout << begl << "current_pts: " << AsTime(current_pts) << std::endl;
  std::cout << begl << "packet_pts: " << AsTime(packet_pts) << std::endl;
  std::cout << begl << "packet_label: " << packet_label << std::endl;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
