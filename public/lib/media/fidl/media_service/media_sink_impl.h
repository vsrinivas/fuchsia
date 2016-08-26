// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_SINK_IMPL_H_
#define APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_SINK_IMPL_H_

#include <memory>

#include "apps/media/cpp/flog.h"
#include "apps/media/interfaces/logs/media_sink_channel.mojom.h"
#include "apps/media/interfaces/media_sink.mojom.h"
#include "apps/media/interfaces/timeline_controller.mojom.h"
#include "apps/media/services/framework/graph.h"
#include "apps/media/services/framework/parts/decoder.h"
#include "apps/media/services/framework_mojo/mojo_packet_consumer.h"
#include "apps/media/services/framework_mojo/mojo_packet_producer.h"
#include "apps/media/services/media_service/media_service_impl.h"
#include "apps/media/services/common/incident.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace mojo {
namespace media {

// Mojo agent that consumes a stream and delivers it to a destination specified
// by URL.
class MediaSinkImpl : public MediaServiceImpl::Product<MediaSink>,
                      public MediaSink {
 public:
  static std::shared_ptr<MediaSinkImpl> Create(
      InterfaceHandle<MediaRenderer> renderer,
      MediaTypePtr media_type,
      InterfaceRequest<MediaSink> request,
      MediaServiceImpl* owner);

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
                MediaServiceImpl* owner);

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

#endif  // APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_SINK_IMPL_H_
