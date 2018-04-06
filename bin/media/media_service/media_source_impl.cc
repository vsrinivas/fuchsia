// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/media_source_impl.h"

#include "garnet/bin/media/demux/fidl_reader.h"
#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "garnet/bin/media/framework/formatting.h"
#include "garnet/bin/media/media_service/fidl_conversion_pipeline_builder.h"
#include "garnet/bin/media/util/callback_joiner.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/logging.h"

namespace media {

// static
std::shared_ptr<MediaSourceImpl> MediaSourceImpl::Create(
    fidl::InterfaceHandle<SeekingReader> reader,
    fidl::VectorPtr<MediaTypeSet> allowed_media_types,
    fidl::InterfaceRequest<MediaSource> request,
    MediaComponentFactory* owner) {
  return std::shared_ptr<MediaSourceImpl>(
      new MediaSourceImpl(std::move(reader), std::move(allowed_media_types),
                          std::move(request), owner));
}

MediaSourceImpl::MediaSourceImpl(
    fidl::InterfaceHandle<SeekingReader> reader,
    fidl::VectorPtr<MediaTypeSet> allowed_media_types,
    fidl::InterfaceRequest<MediaSource> request,
    MediaComponentFactory* owner)
    : MediaComponentFactory::Product<MediaSource>(this,
                                                  std::move(request),
                                                  owner) {
  FXL_DCHECK(reader);

  if (allowed_media_types) {
    for (auto it = allowed_media_types->begin();
         it != allowed_media_types->end(); ++it) {
      allowed_stream_types_.push_back(
          fxl::To<std::unique_ptr<media::StreamTypeSet>>(*it));
    }
  }

  status_publisher_.SetCallbackRunner(
      [this](GetStatusCallback callback, uint64_t version) {
        MediaSourceStatus status;
        if (demux_status_)
          fidl::Clone(*demux_status_, &status);
        callback(version, std::move(status));
      });

  owner->CreateDemux(std::move(reader), demux_.NewRequest());
  HandleDemuxStatusUpdates();

  demux_->Describe([this](fidl::VectorPtr<MediaType> stream_media_types) {
    std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

    size_t stream_index = 0;
    for (const MediaType& stream_media_type : *stream_media_types) {
      streams_.emplace_back(new Stream(
          stream_index, this->owner(),
          [this,
           stream_index](fidl::InterfaceRequest<MediaPacketProducer> request) {
            demux_->GetPacketProducer(stream_index, std::move(request));
          },
          fxl::To<std::unique_ptr<StreamType>>(stream_media_type),
          allowed_stream_types_, callback_joiner->NewCallback()));
      ++stream_index;
    }

    callback_joiner->WhenJoined([this]() {
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

void MediaSourceImpl::Describe(DescribeCallback callback) {
  init_complete_.When([this, callback]() {
    fidl::VectorPtr<MediaType> result;
    for (size_t i = 0; i < streams_.size(); i++) {
      MediaType media_type;
      fidl::Clone(*streams_[i]->media_type(), &media_type);
      result.push_back(std::move(media_type));
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
                                GetStatusCallback callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaSourceImpl::Flush(bool hold_frame, FlushCallback callback) {
  RCHECK(init_complete_.occurred());

  demux_->Flush(hold_frame, callback);
}

void MediaSourceImpl::Seek(int64_t position, SeekCallback callback) {
  RCHECK(init_complete_.occurred());

  demux_->Seek(position, callback);
}

void MediaSourceImpl::HandleDemuxStatusUpdates(uint64_t version,
                                               MediaSourceStatusPtr status) {
  if (status) {
    demux_status_ = std::move(status);
    status_publisher_.SendUpdates();
  }

  demux_->GetStatus(version, [this](uint64_t version,
                                    MediaSourceStatus status) {
    HandleDemuxStatusUpdates(version, fidl::MakeOptional(std::move(status)));
  });
}

MediaSourceImpl::Stream::Stream(
    size_t stream_index,
    MediaComponentFactory* factory,
    const ProducerGetter& producer_getter,
    std::unique_ptr<StreamType> stream_type,
    const fidl::VectorPtr<std::unique_ptr<StreamTypeSet>>& allowed_stream_types,
    const std::function<void()>& callback) {
  FXL_DCHECK(factory);
  FXL_DCHECK(producer_getter);
  FXL_DCHECK(stream_type);
  FXL_DCHECK(callback);

  if (!allowed_stream_types) {
    // No conversion requested.
    producer_getter_ = producer_getter;
    stream_type_ = std::move(stream_type);
    callback();
    return;
  }

  BuildFidlConversionPipeline(
      factory, *allowed_stream_types, producer_getter, nullptr,
      std::move(stream_type),
      [this, callback, stream_index](bool succeeded,
                                     const ConsumerGetter& consumer_getter,
                                     const ProducerGetter& producer_getter,
                                     std::unique_ptr<StreamType> stream_type) {
        FXL_DCHECK(!consumer_getter);
        if (succeeded) {
          FXL_DCHECK(producer_getter);
          producer_getter_ = producer_getter;
        }
        stream_type_ = std::move(stream_type);
        callback();
      });
}

MediaSourceImpl::Stream::~Stream() {}

MediaTypePtr MediaSourceImpl::Stream::media_type() const {
  return fxl::To<MediaTypePtr>(stream_type_);
}

void MediaSourceImpl::Stream::GetPacketProducer(
    fidl::InterfaceRequest<MediaPacketProducer> request) {
  FXL_DCHECK(producer_getter_);
  producer_getter_(std::move(request));
}

}  // namespace media
