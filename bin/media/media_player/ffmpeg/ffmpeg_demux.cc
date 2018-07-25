// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <condition_variable>
#include <map>
#include <thread>

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/bin/media/media_player/ffmpeg/av_codec_context.h"
#include "garnet/bin/media/media_player/ffmpeg/av_format_context.h"
#include "garnet/bin/media/media_player/ffmpeg/av_io_context.h"
#include "garnet/bin/media/media_player/ffmpeg/av_packet.h"
#include "garnet/bin/media/media_player/ffmpeg/ffmpeg_demux.h"
#include "garnet/bin/media/media_player/framework/formatting.h"
#include "garnet/bin/media/media_player/util/incident.h"
#include "garnet/bin/media/media_player/util/safe_clone.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {
namespace {

const std::unordered_map<std::string, std::string> kMetadataLabelMap{
    {"TITLE", fuchsia::mediaplayer::METADATA_LABEL_TITLE},
    {"ARTIST", fuchsia::mediaplayer::METADATA_LABEL_ARTIST},
    {"ALBUM", fuchsia::mediaplayer::METADATA_LABEL_ALBUM},
    {"PUBLISHER", fuchsia::mediaplayer::METADATA_LABEL_PUBLISHER},
    {"GENRE", fuchsia::mediaplayer::METADATA_LABEL_GENRE},
    {"COMPOSER", fuchsia::mediaplayer::METADATA_LABEL_COMPOSER},
};

const std::string kMetadataUnknownPropertyPrefix = "ffmpeg.";

}  // namespace

class FfmpegDemuxImpl : public FfmpegDemux {
 public:
  FfmpegDemuxImpl(std::shared_ptr<Reader> reader);

  ~FfmpegDemuxImpl() override;

  // Demux implementation.
  void SetStatusCallback(StatusCallback callback) override;

  void WhenInitialized(fit::function<void(Result)> callback) override;

  const std::vector<std::unique_ptr<DemuxStream>>& streams() const override;

  void Seek(int64_t position, SeekCallback callback) override;

  // AsyncNode implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

  void GetConfiguration(size_t* input_count, size_t* output_count) override;

  void FlushOutput(size_t output_index, fit::closure callback) override;

  void RequestOutputPacket() override;

 private:
  static constexpr int64_t kNotSeeking = std::numeric_limits<int64_t>::max();

  class FfmpegDemuxStream : public DemuxStream {
   public:
    FfmpegDemuxStream(const AVFormatContext& format_context, size_t index);

    ~FfmpegDemuxStream() override;

    // Demux::DemuxStream implementation.
    size_t index() const override;

    std::unique_ptr<StreamType> stream_type() const override;

    media::TimelineRate pts_rate() const override;

   private:
    AVStream* stream_;
    size_t index_;
    std::unique_ptr<StreamType> stream_type_;
    media::TimelineRate pts_rate_;
  };

  // Specialized packet implementation.
  class DemuxPacket : public Packet {
   public:
    static PacketPtr Create(ffmpeg::AvPacketPtr av_packet,
                            media::TimelineRate pts_rate) {
      return std::make_shared<DemuxPacket>(std::move(av_packet), pts_rate);
    }

    AVPacket& av_packet() { return *av_packet_; }

    DemuxPacket(ffmpeg::AvPacketPtr av_packet, media::TimelineRate pts_rate)
        : Packet(
              (av_packet->pts == AV_NOPTS_VALUE) ? kUnknownPts : av_packet->pts,
              pts_rate, av_packet->flags & AV_PKT_FLAG_KEY, false,
              static_cast<size_t>(av_packet->size),
              av_packet->size == 0 ? nullptr : av_packet->data),
          av_packet_(std::move(av_packet)) {
      FXL_DCHECK(av_packet_->size >= 0);
    }

   private:
    ffmpeg::AvPacketPtr av_packet_;
  };

  // Runs in the ffmpeg thread doing the real work.
  void Worker();

  // Waits on behalf of |Worker| for work to do.
  bool Wait(bool* packet_requested, int64_t* seek_position,
            SeekCallback* seek_callback);

  // Produces a packet. Called from the ffmpeg thread only.
  PacketPtr PullPacket(size_t* stream_index_out);

  // Produces an end-of-stream packet for next_stream_to_end_. Called from the
  // ffmpeg thread only.
  PacketPtr PullEndOfStreamPacket(size_t* stream_index_out);

  // Copies metadata from the specified source into map.
  void CopyMetadata(AVDictionary* source, Metadata& map);

  // Calls the status callback, if there is one.
  void SendStatus();

  // Sets the problem values and sends status.
  void ReportProblem(const std::string& type, const std::string& details);

  mutable std::mutex mutex_;
  std::condition_variable condition_variable_ FXL_GUARDED_BY(mutex_);
  std::thread ffmpeg_thread_;

  int64_t seek_position_ FXL_GUARDED_BY(mutex_) = kNotSeeking;
  SeekCallback seek_callback_ FXL_GUARDED_BY(mutex_);
  bool packet_requested_ FXL_GUARDED_BY(mutex_) = false;
  bool terminating_ FXL_GUARDED_BY(mutex_) = false;
  int64_t duration_ns_ FXL_GUARDED_BY(mutex_);
  Metadata metadata_ FXL_GUARDED_BY(mutex_);
  std::string problem_type_ FXL_GUARDED_BY(mutex_);
  std::string problem_details_ FXL_GUARDED_BY(mutex_);

  // These should be stable after init until the desctructor terminates.
  std::shared_ptr<Reader> reader_;
  std::vector<std::unique_ptr<DemuxStream>> streams_;
  Incident init_complete_;
  Result result_;
  async_dispatcher_t* dispatcher_;

  // After Init, only the ffmpeg thread accesses these.
  AvFormatContextPtr format_context_;
  AvIoContextPtr io_context_;
  int64_t next_pts_;
  int next_stream_to_end_ = -1;  // -1: don't end, streams_.size(): stop.

  StatusCallback status_callback_;
};

// static
std::shared_ptr<Demux> FfmpegDemux::Create(std::shared_ptr<Reader> reader) {
  return std::make_shared<FfmpegDemuxImpl>(reader);
}

FfmpegDemuxImpl::FfmpegDemuxImpl(std::shared_ptr<Reader> reader)
    : reader_(reader), dispatcher_(async_get_default_dispatcher()) {
  FXL_DCHECK(dispatcher_);
  ffmpeg_thread_ = std::thread([this]() { Worker(); });
}

FfmpegDemuxImpl::~FfmpegDemuxImpl() {
  {
    std::lock_guard<std::mutex> locker(mutex_);
    terminating_ = true;
    condition_variable_.notify_all();
  }

  if (ffmpeg_thread_.joinable()) {
    ffmpeg_thread_.join();
  }
}

void FfmpegDemuxImpl::SetStatusCallback(StatusCallback callback) {
  status_callback_ = std::move(callback);
}

void FfmpegDemuxImpl::WhenInitialized(fit::function<void(Result)> callback) {
  init_complete_.When(
      [this, callback = std::move(callback)]() { callback(result_); });
}

const std::vector<std::unique_ptr<Demux::DemuxStream>>&
FfmpegDemuxImpl::streams() const {
  return streams_;
}

void FfmpegDemuxImpl::Seek(int64_t position, SeekCallback callback) {
  std::lock_guard<std::mutex> locker(mutex_);
  seek_position_ = std::move(position);
  seek_callback_ = std::move(callback);
  condition_variable_.notify_all();
}

const char* FfmpegDemuxImpl::label() const { return "demux"; }

void FfmpegDemuxImpl::Dump(std::ostream& os) const {
  os << label() << fostr::Indent;
  os << fostr::NewLine << "stream types per output:";

  {
    std::lock_guard<std::mutex> locker(mutex_);

    for (auto& stream : streams_) {
      os << fostr::NewLine << "[" << stream->index() << "] "
         << stream->stream_type();
    }
  }

  stage()->Dump(os);
  os << fostr::Outdent;
}

void FfmpegDemuxImpl::GetConfiguration(size_t* input_count,
                                       size_t* output_count) {
  FXL_DCHECK(input_count);
  FXL_DCHECK(output_count);
  *input_count = 0;
  *output_count = streams_.size();
}

void FfmpegDemuxImpl::FlushOutput(size_t output_index, fit::closure callback) {
  callback();
}

void FfmpegDemuxImpl::RequestOutputPacket() {
  std::lock_guard<std::mutex> locker(mutex_);
  packet_requested_ = true;
  condition_variable_.notify_all();
}

void FfmpegDemuxImpl::Worker() {
  static constexpr uint64_t kNanosecondsPerMicrosecond = 1000;

  result_ = AvIoContext::Create(reader_, &io_context_);
  if (result_ != Result::kOk) {
    FXL_LOG(ERROR) << "AvIoContext::Create failed, result "
                   << static_cast<int>(result_);
    ReportProblem(result_ == Result::kNotFound
                      ? fuchsia::mediaplayer::kProblemAssetNotFound
                      : fuchsia::mediaplayer::kProblemInternal,
                  "");
    init_complete_.Occur();
    return;
  }

  FXL_DCHECK(io_context_);

  format_context_ = AvFormatContext::OpenInput(io_context_);
  if (!format_context_) {
    FXL_LOG(ERROR) << "AvFormatContext::OpenInput failed";
    result_ = Result::kUnsupportedOperation;
    ReportProblem(fuchsia::mediaplayer::kProblemContainerNotSupported, "");
    init_complete_.Occur();
    return;
  }

  int r = avformat_find_stream_info(format_context_.get(), nullptr);
  if (r < 0) {
    FXL_LOG(ERROR) << "avformat_find_stream_info failed, result " << r;
    result_ = Result::kInternalError;
    ReportProblem(fuchsia::mediaplayer::kProblemInternal,
                  "avformat_find_stream_info failed");
    init_complete_.Occur();
    return;
  }

  Metadata metadata;

  CopyMetadata(format_context_->metadata, metadata);
  for (uint32_t i = 0; i < format_context_->nb_streams; i++) {
    streams_.emplace_back(new FfmpegDemuxStream(*format_context_, i));
    CopyMetadata(format_context_->streams[i]->metadata, metadata);
  }

  {
    std::lock_guard<std::mutex> locker(mutex_);
    duration_ns_ = format_context_->duration * kNanosecondsPerMicrosecond;
    metadata_ = std::move(metadata);
  }

  result_ = Result::kOk;
  init_complete_.Occur();

  async::PostTask(dispatcher_, [this]() { SendStatus(); });

  while (true) {
    bool packet_requested;
    int64_t seek_position;
    SeekCallback seek_callback;

    if (!Wait(&packet_requested, &seek_position, &seek_callback)) {
      return;
    }

    if (seek_position != kNotSeeking) {
      // AVSEEK_FLAG_BACKWARD tells the demux to search backward from the
      // specified seek position to the first i-frame it finds. We'll start
      // producing packets from there so the decoder has the context it needs.
      // The renderers throw away the packets that occur between the i-frame
      // and the seek position.
      int r = av_seek_frame(format_context_.get(), -1, seek_position / 1000,
                            AVSEEK_FLAG_BACKWARD);
      if (r < 0) {
        FXL_LOG(WARNING) << "av_seek_frame failed, result " << r;
      }

      next_stream_to_end_ = -1;
      async::PostTask(dispatcher_, std::move(seek_callback));
    }

    if (packet_requested) {
      size_t stream_index;
      PacketPtr packet = PullPacket(&stream_index);
      FXL_DCHECK(packet);

      AsyncNodeStage* stage_ptr = stage();
      if (stage_ptr) {
        stage_ptr->PutOutputPacket(std::move(packet), stream_index);
      }
    }
  }
}

bool FfmpegDemuxImpl::Wait(bool* packet_requested, int64_t* seek_position,
                           SeekCallback* seek_callback)
    // TODO(US-452): Re-enable thread safety analysis once unique_lock
    // has proper annotations.
    FXL_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> locker(mutex_);
  while (!packet_requested_ && !terminating_ && seek_position_ == kNotSeeking) {
    condition_variable_.wait(locker);
  }

  if (terminating_) {
    return false;
  }

  *packet_requested = packet_requested_;
  packet_requested_ = false;

  *seek_position = seek_position_;
  seek_position_ = kNotSeeking;

  seek_callback_.swap(*seek_callback);
  return true;
}

PacketPtr FfmpegDemuxImpl::PullPacket(size_t* stream_index_out) {
  FXL_DCHECK(stream_index_out);

  if (next_stream_to_end_ != -1) {
    // We're producing end-of-stream packets for all the streams.
    return PullEndOfStreamPacket(stream_index_out);
  }

  ffmpeg::AvPacketPtr av_packet = ffmpeg::AvPacket::Create();

  av_packet->data = nullptr;
  av_packet->size = 0;

  if (av_read_frame(format_context_.get(), av_packet.get()) < 0) {
    // End of stream. Start producing end-of-stream packets for all the streams.
    next_stream_to_end_ = 0;
    return PullEndOfStreamPacket(stream_index_out);
  }

  *stream_index_out = static_cast<size_t>(av_packet->stream_index);
  // TODO(dalesat): What if the packet has no PTS or duration?
  next_pts_ = av_packet->pts + av_packet->duration;
  // TODO(dalesat): Implement packet side data.
  FXL_DCHECK(av_packet->side_data == nullptr) << "side data not implemented";
  FXL_DCHECK(av_packet->side_data_elems == 0);

  return DemuxPacket::Create(std::move(av_packet),
                             streams_[*stream_index_out]->pts_rate());
}

PacketPtr FfmpegDemuxImpl::PullEndOfStreamPacket(size_t* stream_index_out) {
  FXL_DCHECK(next_stream_to_end_ >= 0);

  if (static_cast<std::size_t>(next_stream_to_end_) >= streams_.size()) {
    FXL_DCHECK(false) << "PullPacket called after all streams have ended";
    return nullptr;
  }

  *stream_index_out = next_stream_to_end_++;
  return Packet::CreateEndOfStream(next_pts_,
                                   streams_[*stream_index_out]->pts_rate());
}

void FfmpegDemuxImpl::CopyMetadata(AVDictionary* source, Metadata& metadata) {
  if (source == nullptr) {
    return;
  }

  for (AVDictionaryEntry* entry =
           av_dict_get(source, "", nullptr, AV_DICT_IGNORE_SUFFIX);
       entry != nullptr;
       entry = av_dict_get(source, "", entry, AV_DICT_IGNORE_SUFFIX)) {
    std::string label = entry->key;
    auto iter = kMetadataLabelMap.find(label);
    if (iter != kMetadataLabelMap.end()) {
      // Store the property under its fuchsia.mediaplayer label.
      label = iter->second;
    } else {
      // Store the property under "ffmpeg.<ffmpeg label>".
      std::string temp;
      temp.reserve(kMetadataUnknownPropertyPrefix.size() + label.size());
      temp += kMetadataUnknownPropertyPrefix;
      temp += label;
      label = std::move(temp);
    }

    if (metadata.find(label) == metadata.end()) {
      metadata.emplace(label, entry->value);
    }
  }
}

void FfmpegDemuxImpl::SendStatus() {
  if (!status_callback_) {
    return;
  }

  int64_t duration_ns;
  Metadata metadata;
  std::string problem_type;
  std::string problem_details;

  {
    std::lock_guard<std::mutex> locker(mutex_);
    duration_ns = duration_ns_;
    metadata = metadata_;
    problem_type = problem_type_;
    problem_details = problem_details_;
  }

  status_callback_(duration_ns, std::move(metadata), problem_type,
                   problem_details);
}

void FfmpegDemuxImpl::ReportProblem(const std::string& type,
                                    const std::string& details) {
  {
    std::lock_guard<std::mutex> locker(mutex_);
    problem_type_ = type;
    problem_details_ = details;
  }

  async::PostTask(dispatcher_, [this]() { SendStatus(); });
}

FfmpegDemuxImpl::FfmpegDemuxStream::FfmpegDemuxStream(
    const AVFormatContext& format_context, size_t index)
    : stream_(format_context.streams[index]), index_(index) {
  stream_type_ = AvCodecContext::GetStreamType(*stream_);
  pts_rate_ =
      media::TimelineRate(stream_->time_base.den, stream_->time_base.num);
}

FfmpegDemuxImpl::FfmpegDemuxStream::~FfmpegDemuxStream() {}

size_t FfmpegDemuxImpl::FfmpegDemuxStream::index() const { return index_; }

std::unique_ptr<StreamType> FfmpegDemuxImpl::FfmpegDemuxStream::stream_type()
    const {
  return SafeClone(stream_type_);
}

media::TimelineRate FfmpegDemuxImpl::FfmpegDemuxStream::pts_rate() const {
  return pts_rate_;
}

}  // namespace media_player
