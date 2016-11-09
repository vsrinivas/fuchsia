// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_sink_impl.h"

#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "apps/media/src/media_service/conversion_pipeline_builder.h"
#include "lib/ftl/logging.h"

namespace media {

// static
std::shared_ptr<MediaSinkImpl> MediaSinkImpl::Create(
    fidl::InterfaceHandle<MediaRenderer> renderer,
    MediaTypePtr media_type,
    fidl::InterfaceRequest<MediaSink> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaSinkImpl>(new MediaSinkImpl(
      std::move(renderer), std::move(media_type), std::move(request), owner));
}

MediaSinkImpl::MediaSinkImpl(fidl::InterfaceHandle<MediaRenderer> renderer,
                             MediaTypePtr media_type,
                             fidl::InterfaceRequest<MediaSink> request,
                             MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaSink>(this, std::move(request), owner),
      consumer_(FidlPacketConsumer::Create()),
      producer_(FidlPacketProducer::Create()),
      renderer_(MediaRendererPtr::Create(std::move(renderer))) {
  FTL_DCHECK(renderer_);
  FTL_DCHECK(media_type);

  PartRef consumer_ref = graph_.Add(consumer_);
  PartRef producer_ref = graph_.Add(producer_);

  consumer_->SetFlushRequestedCallback(
      [this, consumer_ref](const MediaPacketConsumer::FlushCallback& callback) {
        ready_.When([this, consumer_ref, callback]() {
          FTL_DCHECK(producer_);
          graph_.FlushOutput(consumer_ref.output());
          producer_->FlushConnection(callback);
        });
      });

  // TODO(dalesat): Once we have c++14, get rid of this shared pointer hack.
  input_stream_type_ = media_type.To<std::unique_ptr<StreamType>>();

  renderer_->GetSupportedMediaTypes([this, consumer_ref, producer_ref](
      fidl::Array<MediaTypeSetPtr> supported_media_types) {
    std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>
        supported_stream_types = supported_media_types.To<std::unique_ptr<
            std::vector<std::unique_ptr<media::StreamTypeSet>>>>();
    std::unique_ptr<StreamType> producer_stream_type;

    // Add transforms to the pipeline to convert from stream_type to a
    // type supported by the track.
    OutputRef out = consumer_ref.output();
    bool result =
        BuildConversionPipeline(*input_stream_type_, *supported_stream_types,
                                &graph_, &out, &producer_stream_type);
    if (!result) {
      // Failed to build conversion pipeline.
      FTL_LOG(WARNING) << "failed to build conversion pipeline";
      // TODO(dalesat): Add problem reporting.
      return;
    }

    FLOG(log_channel_,
         Config(MediaType::From(input_stream_type_),
                MediaType::From(producer_stream_type),
                FLOG_ADDRESS(consumer_.get()), FLOG_ADDRESS(producer_.get())));

    graph_.ConnectOutputToPart(out, producer_ref);

    renderer_->SetMediaType(MediaType::From(std::move(producer_stream_type)));
    MediaPacketConsumerPtr consumer;
    renderer_->GetPacketConsumer(GetProxy(&consumer));
    producer_->Connect(std::move(consumer), [this]() {
      graph_.Prepare();
      ready_.Occur();
    });
  });
}

MediaSinkImpl::~MediaSinkImpl() {}

void MediaSinkImpl::GetPacketConsumer(
    fidl::InterfaceRequest<MediaPacketConsumer> consumer) {
  consumer_->Bind(std::move(consumer));
}

void MediaSinkImpl::GetTimelineControlPoint(
    fidl::InterfaceRequest<MediaTimelineControlPoint> req) {
  FTL_DCHECK(renderer_);
  renderer_->GetTimelineControlPoint(std::move(req));
}

}  // namespace media
