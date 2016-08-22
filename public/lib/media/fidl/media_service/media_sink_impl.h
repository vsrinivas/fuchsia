// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_SINK_IMPL_H_
#define SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_SINK_IMPL_H_

#include <memory>

#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/flog/cpp/flog.h"
#include "mojo/services/media/control/interfaces/media_sink.mojom.h"
#include "mojo/services/media/core/interfaces/timeline_controller.mojom.h"
#include "mojo/services/media/logs/interfaces/media_sink_channel.mojom.h"
#include "services/media/factory_service/factory_service.h"
#include "services/media/framework/graph.h"
#include "services/media/framework/parts/decoder.h"
#include "services/media/framework_mojo/mojo_packet_consumer.h"
#include "services/media/framework_mojo/mojo_packet_producer.h"
#include "services/util/cpp/incident.h"

namespace mojo {
namespace media {

// Mojo agent that consumes a stream and delivers it to a destination specified
// by URL.
class MediaSinkImpl : public MediaFactoryService::Product<MediaSink>,
                      public MediaSink {
 public:
  static std::shared_ptr<MediaSinkImpl> Create(
      InterfaceHandle<MediaRenderer> renderer,
      MediaTypePtr media_type,
      InterfaceRequest<MediaSink> request,
      MediaFactoryService* owner);

  ~MediaSinkImpl() override;

  // MediaSink implementation.
  void GetPacketConsumer(
      InterfaceRequest<MediaPacketConsumer> consumer) override;

  void GetTimelineControlPoint(
      InterfaceRequest<MediaTimelineControlPoint> req) override;

 private:
  MediaSinkImpl(InterfaceHandle<MediaRenderer> renderer,
                MediaTypePtr media_type,
                InterfaceRequest<MediaSink> request,
                MediaFactoryService* owner);

  Incident ready_;
  Graph graph_;
  std::shared_ptr<MojoPacketConsumer> consumer_;
  std::shared_ptr<MojoPacketProducer> producer_;
  MediaRendererPtr renderer_;
  // The following fields are just temporaries used to solve lambda capture
  // problems.
  std::unique_ptr<StreamType> input_stream_type_;

  FLOG_INSTANCE_CHANNEL(logs::MediaSinkChannel, log_channel_);
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_SINK_IMPL_H_
