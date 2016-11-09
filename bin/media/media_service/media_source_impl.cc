// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_source_impl.h"

#include "apps/media/src/fidl/fidl_reader.h"
#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "apps/media/src/framework/formatting.h"
#include "apps/media/src/media_service/conversion_pipeline_builder.h"
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
      allowed_media_types_(allowed_media_types.Clone()) {
  FTL_DCHECK(reader);

  task_runner_ = mtl::MessageLoop::GetCurrent()->task_runner();
  FTL_DCHECK(task_runner_);

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        MediaSourceStatusPtr status = MediaSourceStatus::New();
        status->metadata = metadata_.Clone();
        status->problem = problem_.Clone();
        callback(version, std::move(status));
      });

  std::shared_ptr<Reader> reader_ptr = FidlReader::Create(std::move(reader));
  if (!reader_ptr) {
    FTL_LOG(ERROR) << "couldn't create reader";
    // TODO(dalesat): Add problem reporting.
    return;
  }

  demux_ = Demux::Create(reader_ptr);
  if (!demux_) {
    FTL_LOG(ERROR) << "couldn't create demux";
    // TODO(dalesat): Add problem reporting.
    return;
  }

  demux_->SetStatusCallback([this](const std::unique_ptr<Metadata>& metadata,
                                   const std::string& problem_type,
                                   const std::string& problem_details) {
    metadata_ = MediaMetadata::From(metadata);
    if (problem_type.empty()) {
      problem_.reset();
      status_publisher_.SendUpdates();
    } else {
      ReportProblem(problem_type, problem_details);
      // ReportProblem calls status_publisher_.SendUpdates();
    }
  });

  demux_->WhenInitialized([this](Result result) {
    task_runner_->PostTask([this, result]() { OnDemuxInitialized(result); });
  });
}

MediaSourceImpl::~MediaSourceImpl() {}

void MediaSourceImpl::OnDemuxInitialized(Result result) {
  demux_part_ = graph_.Add(demux_);

  const std::vector<Demux::DemuxStream*>& demux_streams = demux_->streams();
  for (Demux::DemuxStream* demux_stream : demux_streams) {
    streams_.push_back(std::unique_ptr<Stream>(new Stream(
        demux_part_.output(demux_stream->index()), demux_stream->stream_type(),
        allowed_media_types_.To<std::unique_ptr<
            std::vector<std::unique_ptr<media::StreamTypeSet>>>>(),
        &graph_)));
  }

  allowed_media_types_.reset();

  init_complete_.Occur();
}

void MediaSourceImpl::ReportProblem(const std::string& type,
                                    const std::string& details) {
  problem_ = Problem::New();
  problem_->type = type;
  problem_->details = details;
  status_publisher_.SendUpdates();
}

void MediaSourceImpl::GetStreams(const GetStreamsCallback& callback) {
  init_complete_.When([this, callback]() {
    fidl::Array<MediaSourceStreamDescriptorPtr> result =
        fidl::Array<MediaSourceStreamDescriptorPtr>::New(streams_.size());
    for (size_t i = 0; i < streams_.size(); i++) {
      MediaSourceStreamDescriptorPtr descriptor =
          MediaSourceStreamDescriptor::New();
      descriptor->index = i;
      descriptor->media_type = streams_[i]->media_type();
      descriptor->original_media_type = streams_[i]->original_media_type();
      result[i] = std::move(descriptor);
    }
    callback(std::move(result));
  });
}

void MediaSourceImpl::GetPacketProducer(
    uint32_t stream_index,
    fidl::InterfaceRequest<MediaPacketProducer> producer) {
  RCHECK(init_complete_.occurred());

  if (stream_index >= streams_.size()) {
    return;
  }

  streams_[stream_index]->GetPacketProducer(std::move(producer));
}

void MediaSourceImpl::GetStatus(uint64_t version_last_seen,
                                const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaSourceImpl::Prepare(const PrepareCallback& callback) {
  RCHECK(init_complete_.occurred());

  graph_.Prepare();
  callback();
  status_publisher_.SendUpdates();
}

void MediaSourceImpl::Flush(const FlushCallback& callback) {
  RCHECK(init_complete_.occurred());

  graph_.FlushAllOutputs(demux_part_);

  std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

  for (std::unique_ptr<Stream>& stream : streams_) {
    stream->FlushConnection(callback_joiner->NewCallback());
  }

  callback_joiner->WhenJoined(callback);
}

void MediaSourceImpl::Seek(int64_t position, const SeekCallback& callback) {
  RCHECK(init_complete_.occurred());

  demux_->Seek(position, [this, callback]() {
    task_runner_->PostTask([callback]() { callback(); });
  });
}

MediaSourceImpl::Stream::Stream(
    OutputRef output,
    std::unique_ptr<StreamType> stream_type,
    const std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>&
        allowed_stream_types,
    Graph* graph)
    : original_stream_type_(std::move(stream_type)), graph_(graph) {
  FTL_DCHECK(original_stream_type_);
  FTL_DCHECK(graph);

  output_ = output;

  if (allowed_stream_types == nullptr) {
    // No conversion requested.
    stream_type_ = SafeClone(original_stream_type_);
  } else if (!BuildConversionPipeline(*original_stream_type_,
                                      *allowed_stream_types, graph, &output_,
                                      &stream_type_)) {
    // Can't convert to any allowed type.
    // TODO(dalesat): Indicate this in some way other than blowing up.
    FTL_LOG(ERROR) << "can't convert to any allowed type";
    abort();
  }

  producer_ = FidlPacketProducer::Create();
  graph_->ConnectOutputToPart(output_, graph_->Add(producer_));
}

MediaSourceImpl::Stream::~Stream() {}

MediaTypePtr MediaSourceImpl::Stream::media_type() const {
  return MediaType::From(stream_type_);
}

MediaTypePtr MediaSourceImpl::Stream::original_media_type() const {
  return MediaType::From(original_stream_type_);
}

void MediaSourceImpl::Stream::GetPacketProducer(
    fidl::InterfaceRequest<MediaPacketProducer> producer) {
  FTL_DCHECK(producer_ != nullptr);
  producer_->Bind(std::move(producer));
}

void MediaSourceImpl::Stream::FlushConnection(
    const FidlPacketProducer::FlushConnectionCallback callback) {
  if (producer_ != nullptr) {
    producer_->FlushConnection(callback);
  } else {
    callback();
  }
}

}  // namespace media
