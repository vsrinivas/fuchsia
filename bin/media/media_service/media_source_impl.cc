// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_source_impl.h"

#include "apps/media/src/fidl/fidl_conversion_pipeline_builder.h"
#include "apps/media/src/fidl/fidl_reader.h"
#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "apps/media/src/framework/formatting.h"
#include "apps/media/src/util/callback_joiner.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace media {

// static
std::shared_ptr<MediaSourceImpl> MediaSourceImpl::Create(
    fidl::InterfaceHandle<SeekingReader> reader,
    const fidl::Array<MediaTypeSetPtr>& allowed_media_types,
    fidl::InterfaceRequest<MediaSource> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaSourceImpl>(new MediaSourceImpl(
      std::move(reader), allowed_media_types, std::move(request), owner));
}

MediaSourceImpl::MediaSourceImpl(
    fidl::InterfaceHandle<SeekingReader> reader,
    const fidl::Array<MediaTypeSetPtr>& allowed_media_types,
    fidl::InterfaceRequest<MediaSource> request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaSource>(this, std::move(request), owner),
      allowed_stream_types_(
          allowed_media_types.To<std::unique_ptr<
              std::vector<std::unique_ptr<media::StreamTypeSet>>>>()) {
  FTL_DCHECK(reader);

  FLOG(log_channel_, BoundAs(FLOG_BINDING_KOID(binding())));

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        MediaSourceStatusPtr status = MediaSourceStatus::New();
        status->metadata = metadata_.Clone();
        status->problem = problem_.Clone();
        callback(version, std::move(status));
      });

  media_service_ = owner->ConnectToEnvironmentService<MediaService>();

  media_service_->CreateDemux(std::move(reader), demux_.NewRequest());
  FLOG(log_channel_, CreatedDemux(FLOG_PTR_KOID(demux_)));
  HandleDemuxStatusUpdates();

  demux_->Describe([this](fidl::Array<MediaTypePtr> stream_media_types) {
    std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

    size_t stream_index = 0;
    for (MediaTypePtr& stream_media_type : stream_media_types) {
      streams_.emplace_back(new Stream(
          stream_index,
#ifdef NDEBUG
          nullptr,
#else
          log_channel_.get(),
#endif
          media_service_,
          [this,
           stream_index](fidl::InterfaceRequest<MediaPacketProducer> request) {
            demux_->GetPacketProducer(stream_index, std::move(request));
          },
          stream_media_type.To<std::unique_ptr<StreamType>>(),
          allowed_stream_types_, callback_joiner->NewCallback()));
      ++stream_index;
    }

    callback_joiner->WhenJoined([this]() {
      media_service_.reset();

      // Remove invalid streams.
      for (auto iter = streams_.begin(); iter != streams_.end();) {
        if ((*iter)->valid()) {
          ++iter;
        } else {
          iter = streams_.erase(iter);
        }
      }

      init_complete_.Occur();
    });
  });
}

MediaSourceImpl::~MediaSourceImpl() {}

void MediaSourceImpl::Describe(const DescribeCallback& callback) {
  init_complete_.When([this, callback]() {
    fidl::Array<MediaTypePtr> result =
        fidl::Array<MediaTypePtr>::New(streams_.size());
    for (size_t i = 0; i < streams_.size(); i++) {
      result[i] = streams_[i]->media_type();
    }

    callback(std::move(result));
  });
}

void MediaSourceImpl::GetPacketProducer(
    uint32_t stream_index,
    fidl::InterfaceRequest<MediaPacketProducer> request) {
  RCHECK(init_complete_.occurred());

  if (stream_index >= streams_.size()) {
    return;
  }

  streams_[stream_index]->GetPacketProducer(std::move(request));
}

void MediaSourceImpl::GetStatus(uint64_t version_last_seen,
                                const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaSourceImpl::Flush(const FlushCallback& callback) {
  RCHECK(init_complete_.occurred());

  demux_->Flush(callback);
}

void MediaSourceImpl::Seek(int64_t position, const SeekCallback& callback) {
  RCHECK(init_complete_.occurred());

  demux_->Seek(position, callback);
}

void MediaSourceImpl::HandleDemuxStatusUpdates(uint64_t version,
                                               MediaSourceStatusPtr status) {
  if (status) {
    metadata_ = std::move(status->metadata);
    problem_ = std::move(status->problem);
    status_publisher_.SendUpdates();
  }

  demux_->GetStatus(version,
                    [this](uint64_t version, MediaSourceStatusPtr status) {
                      HandleDemuxStatusUpdates(version, std::move(status));
                    });
}

MediaSourceImpl::Stream::Stream(
    size_t stream_index,
    flog::FlogProxy<logs::MediaSourceChannel>* log_channel,
    const MediaServicePtr& media_service,
    const ProducerGetter& producer_getter,
    std::unique_ptr<StreamType> stream_type,
    const std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>&
        allowed_stream_types,
    const std::function<void()>& callback) {
  FTL_DCHECK(media_service);
  FTL_DCHECK(producer_getter);
  FTL_DCHECK(stream_type);
  FTL_DCHECK(callback);

  if (allowed_stream_types == nullptr) {
    // No conversion requested.
    producer_getter_ = producer_getter;
    FLOG(log_channel, NewStream(static_cast<uint32_t>(stream_index),
                                MediaType::From(stream_type),
                                fidl::Array<uint64_t>::New(0)));
    stream_type_ = std::move(stream_type);
    callback();
    return;
  }

  BuildFidlConversionPipeline(
      media_service, *allowed_stream_types, producer_getter, nullptr,
      std::move(stream_type),
      [this, callback, stream_index, log_channel](
          bool succeeded, const ConsumerGetter& consumer_getter,
          const ProducerGetter& producer_getter,
          std::unique_ptr<StreamType> stream_type,
          std::vector<mx_koid_t> converter_koids) {
        FTL_DCHECK(!consumer_getter);
        if (succeeded) {
          FTL_DCHECK(producer_getter);
          producer_getter_ = producer_getter;

          FLOG(log_channel,
               NewStream(static_cast<uint32_t>(stream_index),
                         MediaType::From(stream_type),
                         fidl::Array<uint64_t>::From(converter_koids)));
        }
        stream_type_ = std::move(stream_type);
        callback();
      });
}

MediaSourceImpl::Stream::~Stream() {}

MediaTypePtr MediaSourceImpl::Stream::media_type() const {
  return MediaType::From(stream_type_);
}

void MediaSourceImpl::Stream::GetPacketProducer(
    fidl::InterfaceRequest<MediaPacketProducer> request) {
  FTL_DCHECK(producer_getter_);
  producer_getter_(std::move(request));
}

}  // namespace media
