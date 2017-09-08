// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/media_demux_impl.h"

#include "garnet/bin/media/demux/reader_cache.h"
#include "garnet/bin/media/fidl/fidl_reader.h"
#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "garnet/bin/media/util/callback_joiner.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace media {

// static
std::shared_ptr<MediaDemuxImpl> MediaDemuxImpl::Create(
    fidl::InterfaceHandle<SeekingReader> reader,
    fidl::InterfaceRequest<MediaSource> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaDemuxImpl>(
      new MediaDemuxImpl(std::move(reader), std::move(request), owner));
}

MediaDemuxImpl::MediaDemuxImpl(fidl::InterfaceHandle<SeekingReader> reader,
                               fidl::InterfaceRequest<MediaSource> request,
                               MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaSource>(this, std::move(request), owner),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      graph_(owner->multiproc_task_runner()) {
  FTL_DCHECK(reader);

  FLOG(log_channel_, BoundAs(FLOG_BINDING_KOID(binding())));

  FTL_DCHECK(task_runner_);

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        MediaSourceStatusPtr status = MediaSourceStatus::New();

        for (std::unique_ptr<Stream>& stream : streams_) {
          if (!stream->producer()->is_connected()) {
            continue;
          }

          switch (stream->stream_type()->medium()) {
            case StreamType::Medium::kAudio:
              status->audio_connected = true;
              break;
            case StreamType::Medium::kVideo:
              status->video_connected = true;
              break;
            default:
              break;
          }
        }

        status->metadata = metadata_.Clone();
        status->problem = problem_.Clone();
        callback(version, std::move(status));
      });

  std::shared_ptr<Reader> reader_ptr = FidlReader::Create(std::move(reader));
  if (!reader_ptr) {
    ReportProblem(Problem::kProblemInternal, "couldn't create reader");
    return;
  }

  std::shared_ptr<ReaderCache> reader_cache_ptr =
      ReaderCache::Create(reader_ptr);
  if (!reader_cache_ptr) {
    ReportProblem(Problem::kProblemInternal, "couldn't create reader cache");
    return;
  }

  demux_ = Demux::Create(std::shared_ptr<Reader>(reader_cache_ptr));
  if (!demux_) {
    ReportProblem(Problem::kProblemInternal, "couldn't create demux");
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

MediaDemuxImpl::~MediaDemuxImpl() {}

void MediaDemuxImpl::OnDemuxInitialized(Result result) {
  demux_node_ = graph_.Add(demux_);

  const std::vector<Demux::DemuxStream*>& demux_streams = demux_->streams();
  for (Demux::DemuxStream* demux_stream : demux_streams) {
    streams_.push_back(std::unique_ptr<Stream>(
        new Stream(demux_node_.output(demux_stream->index()),
                   demux_stream->stream_type(), &graph_)));

    streams_.back()->producer()->SetConnectionStateChangedCallback(
        [this]() { status_publisher_.SendUpdates(); });

    FLOG(log_channel_,
         NewStream(streams_.size() - 1,
                   MediaType::From(streams_.back()->stream_type()),
                   FLOG_ADDRESS(streams_.back()->producer().get())));
  }

  graph_.Prepare();

  status_publisher_.SendUpdates();

  init_complete_.Occur();
}

void MediaDemuxImpl::ReportProblem(const std::string& type,
                                   const std::string& details) {
  problem_ = Problem::New();
  problem_->type = type;
  problem_->details = details.empty() ? nullptr : fidl::String(details);
  status_publisher_.SendUpdates();
}

void MediaDemuxImpl::Describe(const DescribeCallback& callback) {
  init_complete_.When([this, callback]() {
    fidl::Array<MediaTypePtr> result =
        fidl::Array<MediaTypePtr>::New(streams_.size());
    for (size_t i = 0; i < streams_.size(); i++) {
      result[i] = MediaType::From(streams_[i]->stream_type());
    }

    callback(std::move(result));
  });
}

void MediaDemuxImpl::GetPacketProducer(
    uint32_t stream_index,
    fidl::InterfaceRequest<MediaPacketProducer> producer) {
  RCHECK(init_complete_.occurred());

  if (stream_index >= streams_.size()) {
    return;
  }

  streams_[stream_index]->BindPacketProducer(std::move(producer));
}

void MediaDemuxImpl::GetStatus(uint64_t version_last_seen,
                               const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaDemuxImpl::Flush(bool hold_frame, const FlushCallback& callback) {
  RCHECK(init_complete_.occurred());

  graph_.FlushAllOutputs(demux_node_, hold_frame);

  std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

  for (std::unique_ptr<Stream>& stream : streams_) {
    stream->FlushConnection(hold_frame, callback_joiner->NewCallback());
  }

  callback_joiner->WhenJoined(callback);
}

void MediaDemuxImpl::Seek(int64_t position, const SeekCallback& callback) {
  RCHECK(init_complete_.occurred());

  demux_->Seek(position, [this, callback]() {
    task_runner_->PostTask([callback]() { callback(); });
  });
}

MediaDemuxImpl::Stream::Stream(OutputRef output,
                               std::unique_ptr<StreamType> stream_type,
                               Graph* graph)
    : stream_type_(std::move(stream_type)), graph_(graph), output_(output) {
  FTL_DCHECK(stream_type_);
  FTL_DCHECK(graph);

  producer_ = FidlPacketProducer::Create();
  graph_->ConnectOutputToNode(output_, graph_->Add(producer_));
}

MediaDemuxImpl::Stream::~Stream() {}

void MediaDemuxImpl::Stream::BindPacketProducer(
    fidl::InterfaceRequest<MediaPacketProducer> producer) {
  FTL_DCHECK(producer_);
  producer_->Bind(std::move(producer));
}

void MediaDemuxImpl::Stream::FlushConnection(
    bool hold_frame,
    const FidlPacketProducer::FlushConnectionCallback callback) {
  FTL_DCHECK(producer_);
  producer_->FlushConnection(hold_frame, callback);
}

}  // namespace media
