// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_sink_impl.h"

#include "apps/media/src/fidl/fidl_conversion_pipeline_builder.h"
#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace media {

// static
std::shared_ptr<MediaSinkImpl> MediaSinkImpl::Create(
    fidl::InterfaceHandle<MediaRenderer> renderer_handle,
    MediaTypePtr media_type,
    fidl::InterfaceRequest<MediaSink> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaSinkImpl>(
      new MediaSinkImpl(std::move(renderer_handle), std::move(media_type),
                        std::move(request), owner));
}

MediaSinkImpl::MediaSinkImpl(
    fidl::InterfaceHandle<MediaRenderer> renderer_handle,
    MediaTypePtr media_type,
    fidl::InterfaceRequest<MediaSink> request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaSink>(this, std::move(request), owner),
      renderer_(MediaRendererPtr::Create(std::move(renderer_handle))),
      original_media_type_(std::move(media_type)),
      stream_type_(original_media_type_.To<std::unique_ptr<StreamType>>()) {
  FTL_DCHECK(renderer_);
  FTL_DCHECK(original_media_type_);

  FLOG(log_channel_, BoundAs(FLOG_BINDING_KOID(binding())));

  media_service_ = owner->ConnectToEnvironmentService<MediaService>();

  renderer_->GetSupportedMediaTypes([this](
      fidl::Array<MediaTypeSetPtr> supported_media_types) {
    FTL_DCHECK(supported_media_types);

    supported_stream_types_ = supported_media_types.To<
        std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>>();

    BuildFidlConversionPipeline(
        media_service_, *supported_stream_types_, nullptr,
        [this](fidl::InterfaceRequest<MediaPacketConsumer> request) {
          renderer_->GetPacketConsumer(std::move(request));
        },
        std::move(stream_type_),
        [this](bool succeeded, const ConsumerGetter& consumer_getter,
               const ProducerGetter& producer_getter,
               std::unique_ptr<StreamType> stream_type,
               std::vector<mx_koid_t> converter_koids) {
          FTL_DCHECK(!producer_getter);
          RCHECK(succeeded);
          FTL_DCHECK(consumer_getter);

          consumer_getter_ = consumer_getter;
          stream_type_ = std::move(stream_type);

          renderer_->SetMediaType(MediaType::From(stream_type_));

          FLOG(log_channel_,
               Config(std::move(original_media_type_),
                      MediaType::From(stream_type_),
                      fidl::Array<uint64_t>::From(converter_koids),
                      FLOG_PTR_KOID(renderer_)));

          // Not needed anymore.
          original_media_type_.reset();
          supported_stream_types_.reset();
          media_service_.reset();

          ready_.Occur();
        });
  });
}

MediaSinkImpl::~MediaSinkImpl() {}

void MediaSinkImpl::GetPacketConsumer(
    fidl::InterfaceRequest<MediaPacketConsumer> request) {
  ready_.When(
      ftl::MakeCopyable([ this, request = std::move(request) ]() mutable {
        RCHECK(consumer_getter_);
        consumer_getter_(std::move(request));
      }));
}

void MediaSinkImpl::GetTimelineControlPoint(
    fidl::InterfaceRequest<MediaTimelineControlPoint> request) {
  FTL_DCHECK(renderer_);
  renderer_->GetTimelineControlPoint(std::move(request));
}

}  // namespace media
