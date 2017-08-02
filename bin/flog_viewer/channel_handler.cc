// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/channel_handler.h"

#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/default.h"
#include "apps/media/tools/flog_viewer/handlers/media_demux.h"
#include "apps/media/tools/flog_viewer/handlers/media_packet_consumer.h"
#include "apps/media/tools/flog_viewer/handlers/media_packet_producer.h"
#include "apps/media/tools/flog_viewer/handlers/media_player.h"
#include "apps/media/tools/flog_viewer/handlers/media_renderer.h"
#include "apps/media/tools/flog_viewer/handlers/media_sink.h"
#include "apps/media/tools/flog_viewer/handlers/media_source.h"
#include "apps/media/tools/flog_viewer/handlers/media_timeline_control_point.h"
#include "apps/media/tools/flog_viewer/handlers/media_type_converter.h"

namespace flog {

// static
const std::string ChannelHandler::kFormatTerse = "terse";
// static
const std::string ChannelHandler::kFormatFull = "full";
// static
const std::string ChannelHandler::kFormatDigest = "digest";

// static
std::unique_ptr<ChannelHandler> ChannelHandler::Create(
    const std::string& type_name,
    const std::string& format,
    ChannelManager* manager) {
  ChannelHandler* handler = nullptr;

  // When implementing a new handler, add logic here for creating an instance.
  if (type_name == handlers::MediaPlayer::Name_) {
    handler = new handlers::MediaPlayer(format);
  } else if (type_name == handlers::MediaTypeConverter::Name_) {
    handler = new handlers::MediaTypeConverter(format);
  } else if (type_name == handlers::MediaDemux::Name_) {
    handler = new handlers::MediaDemux(format);
  } else if (type_name == handlers::MediaPacketProducer::Name_) {
    handler = new handlers::MediaPacketProducer(format);
  } else if (type_name == handlers::MediaPacketConsumer::Name_) {
    handler = new handlers::MediaPacketConsumer(format);
  } else if (type_name == handlers::MediaRenderer::Name_) {
    handler = new handlers::MediaRenderer(format);
  } else if (type_name == handlers::MediaSink::Name_) {
    handler = new handlers::MediaSink(format);
  } else if (type_name == handlers::MediaSource::Name_) {
    handler = new handlers::MediaSource(format);
  } else if (type_name == handlers::MediaTimelineControlPoint::Name_) {
    handler = new handlers::MediaTimelineControlPoint(format);
  }

  if (handler == nullptr) {
    handler = new handlers::Default(format);
  }

  handler->manager_ = manager;

  return std::unique_ptr<ChannelHandler>(handler);
}

ChannelHandler::ChannelHandler(const std::string& format) : format_(format) {}

ChannelHandler::~ChannelHandler() {}

void ChannelHandler::HandleMessage(std::shared_ptr<Channel> channel,
                                   uint32_t entry_index,
                                   const FlogEntryPtr& entry,
                                   fidl::Message* message) {
  channel_ = channel;
  entry_index_ = entry_index;
  entry_ = &entry;
  HandleMessage(message);
  channel_ = nullptr;
  entry_index_ = 0;
  entry_ = nullptr;
}

std::shared_ptr<Accumulator> ChannelHandler::GetAccumulator() {
  return nullptr;
}

std::shared_ptr<Channel> ChannelHandler::AsChannel(uint64_t subject_address) {
  FTL_DCHECK(manager_ != nullptr);
  return manager_->FindChannelBySubjectAddress(channel_->log_id(),
                                               subject_address);
}

void ChannelHandler::BindAs(uint64_t koid) {
  FTL_DCHECK(manager_ != nullptr);
  manager_->BindAs(channel_, koid);
}

void ChannelHandler::SetBindingKoid(Binding* binding, uint64_t koid) {
  FTL_DCHECK(binding != nullptr);
  FTL_DCHECK(koid != 0);
  FTL_DCHECK(manager_ != nullptr);

  manager_->SetBindingKoid(binding, koid);
}

}  // namespace flog
