// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/media/cpp/flog.h"
#include "apps/media/services/logs/media_sink_channel.fidl.h"
#include "apps/media/services/media_sink.fidl.h"
#include "apps/media/services/timeline_controller.fidl.h"
#include "apps/media/src/decode/decoder.h"
#include "apps/media/src/fidl/fidl_packet_consumer.h"
#include "apps/media/src/fidl/fidl_packet_producer.h"
#include "apps/media/src/framework/graph.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "apps/media/src/util/incident.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace media {

// Fidl agent that consumes a stream and delivers it to a destination specified
// by URL.
class MediaSinkImpl : public MediaServiceImpl::Product<MediaSink>,
                      public MediaSink {
 public:
  static std::shared_ptr<MediaSinkImpl> Create(
      fidl::InterfaceHandle<MediaRenderer> renderer,
      MediaTypePtr media_type,
      fidl::InterfaceRequest<MediaSink> request,
      MediaServiceImpl* owner);

  ~MediaSinkImpl() override;

  // MediaSink implementation.
  void GetPacketConsumer(
      fidl::InterfaceRequest<MediaPacketConsumer> consumer) override;

  void GetTimelineControlPoint(
      fidl::InterfaceRequest<MediaTimelineControlPoint> req) override;

 private:
  MediaSinkImpl(fidl::InterfaceHandle<MediaRenderer> renderer,
                MediaTypePtr media_type,
                fidl::InterfaceRequest<MediaSink> request,
                MediaServiceImpl* owner);

  Incident ready_;
  Graph graph_;
  std::shared_ptr<FidlPacketConsumer> consumer_;
  std::shared_ptr<FidlPacketProducer> producer_;
  MediaRendererPtr renderer_;
  // The following fields are just temporaries used to solve lambda capture
  // problems.
  std::unique_ptr<StreamType> input_stream_type_;

  FLOG_INSTANCE_CHANNEL(logs::MediaSinkChannel, log_channel_);
};

}  // namespace media
