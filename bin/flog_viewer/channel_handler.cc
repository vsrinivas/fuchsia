// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/channel_handler.h"

#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/default.h"
#include "apps/media/tools/flog_viewer/handlers/media_decoder_digest.h"
#include "apps/media/tools/flog_viewer/handlers/media_decoder_full.h"
#include "apps/media/tools/flog_viewer/handlers/media_demux_digest.h"
#include "apps/media/tools/flog_viewer/handlers/media_demux_full.h"
#include "apps/media/tools/flog_viewer/handlers/media_packet_consumer_digest.h"
#include "apps/media/tools/flog_viewer/handlers/media_packet_consumer_full.h"
#include "apps/media/tools/flog_viewer/handlers/media_packet_producer_digest.h"
#include "apps/media/tools/flog_viewer/handlers/media_packet_producer_full.h"
#include "apps/media/tools/flog_viewer/handlers/media_player_digest.h"
#include "apps/media/tools/flog_viewer/handlers/media_player_full.h"
#include "apps/media/tools/flog_viewer/handlers/media_sink_digest.h"
#include "apps/media/tools/flog_viewer/handlers/media_sink_full.h"

namespace flog {

// static
std::unique_ptr<ChannelHandler> ChannelHandler::Create(
    const std::string& type_name,
    const std::string& format) {
  ChannelHandler* handler = nullptr;

  // When implementing a new handler, add logic here for creating an instance.
  if (type_name == handlers::MediaPlayerFull::Name_) {
    if (format == FlogViewer::kFormatTerse ||
        format == FlogViewer::kFormatFull) {
      handler = new handlers::MediaPlayerFull(format);
    } else if (format == FlogViewer::kFormatDigest) {
      handler = new handlers::MediaPlayerDigest(format);
    }
  } else if (type_name == handlers::MediaDecoderFull::Name_) {
    if (format == FlogViewer::kFormatTerse ||
        format == FlogViewer::kFormatFull) {
      handler = new handlers::MediaDecoderFull(format);
    } else if (format == FlogViewer::kFormatDigest) {
      handler = new handlers::MediaDecoderDigest(format);
    }
  } else if (type_name == handlers::MediaDemuxFull::Name_) {
    if (format == FlogViewer::kFormatTerse ||
        format == FlogViewer::kFormatFull) {
      handler = new handlers::MediaDemuxFull(format);
    } else if (format == FlogViewer::kFormatDigest) {
      handler = new handlers::MediaDemuxDigest(format);
    }
  } else if (type_name == handlers::MediaPacketProducerFull::Name_) {
    if (format == FlogViewer::kFormatTerse ||
        format == FlogViewer::kFormatFull) {
      handler = new handlers::MediaPacketProducerFull(format);
    }
    if (format == FlogViewer::kFormatDigest) {
      handler = new handlers::MediaPacketProducerDigest(format);
    }
  } else if (type_name == handlers::MediaPacketConsumerFull::Name_) {
    if (format == FlogViewer::kFormatTerse ||
        format == FlogViewer::kFormatFull) {
      handler = new handlers::MediaPacketConsumerFull(format);
    } else if (format == FlogViewer::kFormatDigest) {
      handler = new handlers::MediaPacketConsumerDigest(format);
    }
  } else if (type_name == handlers::MediaSinkFull::Name_) {
    if (format == FlogViewer::kFormatTerse ||
        format == FlogViewer::kFormatFull) {
      handler = new handlers::MediaSinkFull(format);
    } else if (format == FlogViewer::kFormatDigest) {
      handler = new handlers::MediaSinkDigest(format);
    }
  }

  if (handler == nullptr) {
    handler = new handlers::Default(format);
  }

  return std::unique_ptr<ChannelHandler>(handler);
}

ChannelHandler::ChannelHandler() {}

ChannelHandler::~ChannelHandler() {}

void ChannelHandler::HandleMessage(
    uint32_t entry_index,
    const FlogEntryPtr& entry,
    fidl::Message* message,
    const ChannelLookupCallback& channel_lookup_callback) {
  entry_index_ = entry_index;
  entry_ = &entry;
  channel_lookup_callback_ = channel_lookup_callback;
  HandleMessage(message);
  entry_index_ = 0;
  entry_ = nullptr;
  channel_lookup_callback_ = nullptr;
}

std::shared_ptr<Accumulator> ChannelHandler::GetAccumulator() {
  return nullptr;
}

std::shared_ptr<Channel> ChannelHandler::AsChannel(uint64_t subject_address) {
  if (!channel_lookup_callback_) {
    return nullptr;
  }

  return channel_lookup_callback_(subject_address);
}

}  // namespace flog
