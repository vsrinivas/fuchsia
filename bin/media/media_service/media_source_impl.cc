// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/media_source_impl.h"

#include "garnet/bin/media/fidl/fidl_conversion_pipeline_builder.h"
#include "garnet/bin/media/fidl/fidl_reader.h"
#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "garnet/bin/media/framework/formatting.h"
#include "garnet/bin/media/util/callback_joiner.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

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
  FXL_DCHECK(reader);

  FLOG(log_channel_, BoundAs(FLOG_BINDING_KOID(binding())));

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        callback(version, demux_status_ ? demux_status_.Clone()
                                        : MediaSourceStatus::New());
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
#ifdef FLOG_ENABLED
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

void MediaSourceImpl::Flush(bool hold_frame, const FlushCallback& callback) {
  RCHECK(init_complete_.occurred());

  demux_->Flush(hold_frame, callback);
}

void MediaSourceImpl::Seek(int64_t position, const SeekCallback& callback) {
  RCHECK(init_complete_.occurred());

  demux_->Seek(position, callback);
}

void MediaSourceImpl::HandleDemuxStatusUpdates(uint64_t version,
                                               MediaSourceStatusPtr status) {
  if (status) {
    demux_status_ = std::move(status);
    status_publisher_.SendUpdates();
  }

  demux_->GetStatus(version,
                    [this](uint64_t version, MediaSourceStatusPtr status) {
                      HandleDemuxStatusUpdates(version, std::move(status));
                    });
}

MediaSourceImpl::Stream::Stream(
    size_t stream_index,
#ifdef FLOG_ENABLED
    flog::FlogProxy<logs::MediaSourceChannel>* log_channel,
#endif
    const MediaServicePtr& media_service,
    const ProducerGetter& producer_getter,
    std::unique_ptr<StreamType> stream_type,
    const std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>&
        allowed_stream_types,
    const std::function<void()>& callback) {
  FXL_DCHECK(media_service);
  FXL_DCHECK(producer_getter);
  FXL_DCHECK(stream_type);
  FXL_DCHECK(callback);

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

#ifndef FLOG_ENABLED
  int log_channel = 0;
#endif

  BuildFidlConversionPipeline(
      media_service, *allowed_stream_types, producer_getter, nullptr,
      std::move(stream_type),
      [this, callback, stream_index, log_channel](
          bool succeeded, const ConsumerGetter& consumer_getter,
          const ProducerGetter& producer_getter,
          std::unique_ptr<StreamType> stream_type,
          std::vector<mx_koid_t> converter_koids) {
        FXL_DCHECK(!consumer_getter);
        if (succeeded) {
          FXL_DCHECK(producer_getter);
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
  FXL_DCHECK(producer_getter_);
  producer_getter_(std::move(request));
}

}  // namespace media
