// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/ffmpeg/ffmpeg_demux.h"

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/playback/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <condition_variable>
#include <map>
#include <optional>
#include <thread>

#include "lib/media/cpp/timeline_rate.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/playback/mediaplayer/ffmpeg/av_codec_context.h"
#include "src/media/playback/mediaplayer/ffmpeg/av_format_context.h"
#include "src/media/playback/mediaplayer/ffmpeg/av_io_context.h"
#include "src/media/playback/mediaplayer/ffmpeg/av_packet.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/graph/thread_priority.h"
#include "src/media/playback/mediaplayer/util/incident.h"
#include "src/media/playback/mediaplayer/util/safe_clone.h"

namespace media_player {
namespace {

const std::unordered_map<std::string, std::string> kMetadataLabelMap{
    {"TITLE", fuchsia::media::METADATA_LABEL_TITLE},
    {"ARTIST", fuchsia::media::METADATA_LABEL_ARTIST},
    {"ALBUM", fuchsia::media::METADATA_LABEL_ALBUM},
    {"PUBLISHER", fuchsia::media::METADATA_LABEL_PUBLISHER},
    {"GENRE", fuchsia::media::METADATA_LABEL_GENRE},
    {"COMPOSER", fuchsia::media::METADATA_LABEL_COMPOSER},
};

const std::string kMetadataUnknownPropertyPrefix = "ffmpeg.";

constexpr size_t kBitsPerByte = 8;

constexpr uint32_t kMaxPayloadCount = 1;

// TODO(dalesat): Refine this function.
uint64_t MaxPayloadSize(StreamType* stream_type) {
  FX_DCHECK(stream_type);
  constexpr uint64_t kMaxPayloadSizeAudio = (64 * 1024);
  constexpr uint64_t kMaxPayloadSizeVideo = (512 * 1024);
  return stream_type->medium() == StreamType::Medium::kVideo ? kMaxPayloadSizeVideo
                                                             : kMaxPayloadSizeAudio;
}

}  // namespace

class FfmpegDemuxImpl : public FfmpegDemux {
 public:
  FfmpegDemuxImpl(std::shared_ptr<ReaderCache> reader_cache);

  ~FfmpegDemuxImpl() override;

  // Demux implementation.
  void SetStatusCallback(StatusCallback callback) override;

  void SetCacheOptions(zx_duration_t lead, zx_duration_t backtrack) override;

  void WhenInitialized(fit::function<void(zx_status_t)> callback) override;

  const std::vector<std::unique_ptr<DemuxStream>>& streams() const override;

  void Seek(int64_t position, SeekCallback callback) override;

  // Node implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

  void ConfigureConnectors() override;

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

  // Runs in the ffmpeg thread doing the real work.
  void Worker();

  // Notifies that initialization is complete. This method is called from the
  // worker, and posts the notification to the main thread.
  void NotifyInitComplete();

  // Waits on behalf of |Worker| for work to do.
  bool Wait(bool* packet_requested, int64_t* seek_position, SeekCallback* seek_callback);

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
  // Bits per second if known by Ffmpeg.
  std::optional<size_t> bit_rate_ FXL_GUARDED_BY(mutex_);

  // These should be stable after init until the desctructor terminates.
  std::shared_ptr<ReaderCache> reader_cache_;
  std::vector<std::unique_ptr<DemuxStream>> streams_;
  Incident init_complete_;
  zx_status_t status_ = ZX_OK;
  async_dispatcher_t* dispatcher_;

  // After Init, only the ffmpeg thread accesses these.
  AvFormatContextPtr format_context_;
  AvIoContextPtr io_context_;
  int64_t next_pts_;
  int32_t next_stream_to_end_ = -1;  // -1: don't end, streams_.size(): stop.

  StatusCallback status_callback_;
};

// static
std::shared_ptr<Demux> FfmpegDemux::Create(std::shared_ptr<ReaderCache> reader_cache) {
  return std::make_shared<FfmpegDemuxImpl>(reader_cache);
}

FfmpegDemuxImpl::FfmpegDemuxImpl(std::shared_ptr<ReaderCache> reader_cache)
    : reader_cache_(reader_cache), dispatcher_(async_get_default_dispatcher()) {
  FX_DCHECK(dispatcher_);
  ffmpeg_thread_ = std::thread([this]() {
    ThreadPriority::SetToHigh();
    Worker();
  });
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

void FfmpegDemuxImpl::SetCacheOptions(zx_duration_t lead, zx_duration_t backtrack) {
  FX_DCHECK(lead > 0);

  WhenInitialized([this, lead, backtrack](zx_status_t init_status) {
    if (init_status != ZX_OK) {
      return;
    }

    size_t capacity_bytes;
    size_t backtrack_bytes;
    {
      std::lock_guard<std::mutex> locker(mutex_);

      if (!bit_rate_.has_value()) {
        // When ffmpeg doesn't know the media bitrate (which may be the case if
        // file size is not known), we cannot translate from time to bits, so
        // we'll let ReaderCache keep its defaults.
        return;
      }

      size_t byte_rate = bit_rate_.value() / kBitsPerByte;
      size_t lead_bytes = byte_rate * (lead / ZX_SEC(1));
      backtrack_bytes = byte_rate * (backtrack / ZX_SEC(1));
      capacity_bytes = lead_bytes + backtrack_bytes;
    }

    reader_cache_->SetCacheOptions(capacity_bytes, backtrack_bytes);
  });
}

void FfmpegDemuxImpl::WhenInitialized(fit::function<void(zx_status_t)> callback) {
  init_complete_.When([this, callback = std::move(callback)]() { callback(status_); });
}

const std::vector<std::unique_ptr<Demux::DemuxStream>>& FfmpegDemuxImpl::streams() const {
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
  Node::Dump(os);
  os << fostr::NewLine << "stream types per output:";

  {
    std::lock_guard<std::mutex> locker(mutex_);

    for (auto& stream : streams_) {
      os << fostr::NewLine << "[" << stream->index() << "] " << stream->stream_type();
    }
  }

  os << fostr::Outdent;
}

void FfmpegDemuxImpl::ConfigureConnectors() {
  for (size_t output_index = 0; output_index < streams_.size(); ++output_index) {
    ConfigureOutputToProvideLocalMemory(0,  // max_aggregate_payload_size
                                        kMaxPayloadCount,
                                        MaxPayloadSize(streams_[output_index]->stream_type().get()),
                                        nullptr,  // video_constraints
                                        output_index);
  }
}

void FfmpegDemuxImpl::FlushOutput(size_t output_index, fit::closure callback) { callback(); }

void FfmpegDemuxImpl::RequestOutputPacket() {
  std::lock_guard<std::mutex> locker(mutex_);
  packet_requested_ = true;
  condition_variable_.notify_all();
}

void FfmpegDemuxImpl::Worker() {
  static constexpr uint64_t kNanosecondsPerMicrosecond = 1000;

  status_ = AvIoContext::Create(reader_cache_, &io_context_, dispatcher_);
  if (status_ != ZX_OK) {
    FX_LOGS(ERROR) << "AvIoContext::Create failed, status " << status_;
    ReportProblem(status_ == ZX_ERR_NOT_FOUND ? fuchsia::media::playback::PROBLEM_ASSET_NOT_FOUND
                                              : fuchsia::media::playback::PROBLEM_INTERNAL,
                  "");

    NotifyInitComplete();
    return;
  }

  FX_DCHECK(io_context_);

  format_context_ = AvFormatContext::OpenInput(io_context_);
  if (!format_context_) {
    FX_LOGS(ERROR) << "AvFormatContext::OpenInput failed";
    status_ = ZX_ERR_NOT_SUPPORTED;
    ReportProblem(fuchsia::media::playback::PROBLEM_CONTAINER_NOT_SUPPORTED, "");
    NotifyInitComplete();
    return;
  }

  int r = avformat_find_stream_info(format_context_.get(), nullptr);
  if (r < 0) {
    FX_LOGS(ERROR) << "avformat_find_stream_info failed, result " << r;
    status_ = ZX_ERR_INTERNAL;
    ReportProblem(fuchsia::media::playback::PROBLEM_INTERNAL, "avformat_find_stream_info failed");
    NotifyInitComplete();
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
    if (format_context_->bit_rate != 0) {
      bit_rate_ = format_context_->bit_rate;
    }
    metadata_ = std::move(metadata);
  }

  status_ = ZX_OK;
  NotifyInitComplete();

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
      int r = av_seek_frame(format_context_.get(), -1, seek_position / 1000, AVSEEK_FLAG_BACKWARD);
      if (r < 0) {
        FX_LOGS(WARNING) << "av_seek_frame failed, result " << r;
      }

      next_stream_to_end_ = -1;
      async::PostTask(dispatcher_, std::move(seek_callback));
    }

    if (packet_requested) {
      size_t stream_index{};
      PacketPtr packet = PullPacket(&stream_index);
      // TODO(fxbug.dev/13528): Replace check with DCHECK.
      // We should always get a packet from |PullPacket|. See the comment in
      // |PullEndOfStreamPacket|.
      if (packet) {
        PutOutputPacket(std::move(packet), stream_index);
      }
    }
  }
}

void FfmpegDemuxImpl::NotifyInitComplete() {
  async::PostTask(dispatcher_, [this]() { init_complete_.Occur(); });
}

bool FfmpegDemuxImpl::Wait(bool* packet_requested, int64_t* seek_position,
                           SeekCallback* seek_callback)
    // TODO(fxbug.dev/27120): Re-enable thread safety analysis once unique_lock
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
  FX_DCHECK(stream_index_out);

  if (next_stream_to_end_ != -1) {
    // We're producing end-of-stream packets for all the streams.
    return PullEndOfStreamPacket(stream_index_out);
  }

  ffmpeg::AvPacketPtr av_packet = ffmpeg::AvPacket::Create();
  if (av_packet->side_data) {
    FX_LOGS(WARNING) << "ON CREATE, av_packet->side_data 0x" << std::hex
                     << reinterpret_cast<uintptr_t>(av_packet->side_data);
  }

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

  if (av_packet->side_data) {
    FX_DCHECK(av_packet->side_data_elems > 0);
    auto side_data = av_packet->side_data;
    for (int i = 0; i < av_packet->side_data_elems; ++i, ++side_data) {
      switch (side_data->type) {
        case AV_PKT_DATA_SKIP_SAMPLES:
          // TODO(dalesat): Implement sample skipping.
          break;
        default:
          // TODO(dalesat): Handle more side-data types.
          FX_DCHECK(false) << "Unhandled side data type " << side_data->type;
          break;
      }
    }
  }

  int64_t pts = (av_packet->pts == AV_NOPTS_VALUE) ? Packet::kNoPts : av_packet->pts;
  bool keyframe = av_packet->flags & AV_PKT_FLAG_KEY;

  fbl::RefPtr<PayloadBuffer> payload_buffer;
  uint64_t size = av_packet->size;
  if (size != 0) {
    // The recycler used here just holds a captured reference to the |AVPacket|
    // so the memory underlying the |AVPacket| and the |PayloadBuffer| is not
    // deleted/recycled. This doesn't prevent the demux from generating more
    // |AVPackets|.
    payload_buffer = PayloadBuffer::Create(
        size, av_packet->data, [av_packet = std::move(av_packet)](PayloadBuffer* payload_buffer) {
          // The deallocation happens when |av_packet|
          // goes out of scope. The |PayloadBuffer|
          // object deletes itself.
        });
  }

  return Packet::Create(pts, streams_[*stream_index_out]->pts_rate(), keyframe, false, size,
                        std::move(payload_buffer));
}

PacketPtr FfmpegDemuxImpl::PullEndOfStreamPacket(size_t* stream_index_out) {
  FX_DCHECK(next_stream_to_end_ >= 0);

  if (static_cast<std::size_t>(next_stream_to_end_) >= streams_.size()) {
    // This shouldn't happen if downstream nodes are behaving properly, but
    // it's not fatal. We DLOG at ERROR level to avoid test failures until
    // this is resolved.
    // TODO(fxbug.dev/13528): Restore DCHECK.
    FX_LOGS(ERROR) << "PullPacket called after all streams have ended";
    return nullptr;
  }

  *stream_index_out = next_stream_to_end_++;
  return Packet::CreateEndOfStream(next_pts_, streams_[*stream_index_out]->pts_rate());
}

void FfmpegDemuxImpl::CopyMetadata(AVDictionary* source, Metadata& metadata) {
  if (source == nullptr) {
    return;
  }

  for (AVDictionaryEntry* entry = av_dict_get(source, "", nullptr, AV_DICT_IGNORE_SUFFIX);
       entry != nullptr; entry = av_dict_get(source, "", entry, AV_DICT_IGNORE_SUFFIX)) {
    std::string label = entry->key;
    auto iter = kMetadataLabelMap.find(label);
    if (iter != kMetadataLabelMap.end()) {
      // Store the property under its fuchsia.media.playback label.
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

  status_callback_(duration_ns, io_context_ && (io_context_->seekable & AVIO_SEEKABLE_NORMAL) != 0,
                   std::move(metadata), problem_type, problem_details);
}

void FfmpegDemuxImpl::ReportProblem(const std::string& type, const std::string& details) {
  {
    std::lock_guard<std::mutex> locker(mutex_);
    problem_type_ = type;
    problem_details_ = details;
  }

  async::PostTask(dispatcher_, [this]() { SendStatus(); });
}

FfmpegDemuxImpl::FfmpegDemuxStream::FfmpegDemuxStream(const AVFormatContext& format_context,
                                                      size_t index)
    : stream_(format_context.streams[index]), index_(index) {
  stream_type_ = AvCodecContext::GetStreamType(*stream_);
  pts_rate_ = media::TimelineRate(stream_->time_base.den, stream_->time_base.num);
}

FfmpegDemuxImpl::FfmpegDemuxStream::~FfmpegDemuxStream() {}

size_t FfmpegDemuxImpl::FfmpegDemuxStream::index() const { return index_; }

std::unique_ptr<StreamType> FfmpegDemuxImpl::FfmpegDemuxStream::stream_type() const {
  return SafeClone(stream_type_);
}

media::TimelineRate FfmpegDemuxImpl::FfmpegDemuxStream::pts_rate() const { return pts_rate_; }

}  // namespace media_player
