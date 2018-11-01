#include "avcodec_context.h"

#include <map>
#include <string>

#include <lib/media/codec_impl/codec_buffer.h>

namespace {

// TODO(turnage): Add VP9, and more.
static const std::map<std::string, AVCodecID> codec_ids = {
    {"video/h264", AV_CODEC_ID_H264}};

}  // namespace

std::optional<std::unique_ptr<AvCodecContext>> AvCodecContext::CreateDecoder(
    const fuchsia::mediacodec::CodecFormatDetails& format_details,
    GetBufferCallback get_buffer_callback) {
  avcodec_register_all();
  auto codec_id = codec_ids.find(format_details.mime_type);
  if (codec_id == codec_ids.end()) {
    return std::nullopt;
  }

  AVCodec* codec = avcodec_find_decoder(codec_id->second);
  ZX_DEBUG_ASSERT(codec);
  ZX_DEBUG_ASSERT(av_codec_is_decoder(codec));
  auto avcodec_context =
      std::unique_ptr<AVCodecContext, fit::function<void(AVCodecContext*)>>(
          avcodec_alloc_context3(codec), [](AVCodecContext* avcodec_context) {
            avcodec_free_context(&avcodec_context);
          });
  ZX_ASSERT(avcodec_context);

  // This flag must be set in case our packets come on NAL boundaries
  // and not just frame boundaries.
  avcodec_context->flags2 |= AV_CODEC_FLAG2_CHUNKS;

  // This flag is required to override get_buffer2.
  ZX_ASSERT(avcodec_context->codec->capabilities & AV_CODEC_CAP_DR1);

  avcodec_context->get_buffer2 = AvCodecContext::GetBufferCallbackRouter;

  std::unique_ptr<AvCodecContext> decoder(new AvCodecContext(
      std::move(avcodec_context), std::move(get_buffer_callback)));

  const std::vector<uint8_t>& oob = format_details.codec_oob_bytes.get();
  ZX_DEBUG_ASSERT(oob.empty() || format_details.codec_oob_bytes);
  if (!oob.empty()) {
    // Freed in AVCodecContext deleter in avcodec_free.
    uint8_t* extradata = reinterpret_cast<uint8_t*>(av_malloc(oob.size()));
    ZX_ASSERT(extradata);
    std::memcpy(extradata, oob.data(), oob.size());
    decoder->avcodec_context_->extradata = extradata;
    decoder->avcodec_context_->extradata_size = oob.size();
  }

  int open_error =
      avcodec_open2(decoder->avcodec_context_.get(), codec, nullptr);
  ZX_ASSERT(!open_error);
  ZX_DEBUG_ASSERT(avcodec_is_open(decoder->avcodec_context_.get()));

  return decoder;
}

int AvCodecContext::SendPacket(const CodecPacket* codec_packet) {
  ZX_DEBUG_ASSERT(codec_packet);
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(avcodec_is_open(avcodec_context_.get()));
  ZX_DEBUG_ASSERT(av_codec_is_decoder(avcodec_context_->codec));
  ZX_DEBUG_ASSERT(codec_packet->has_start_offset());
  ZX_DEBUG_ASSERT(codec_packet->has_valid_length_bytes());
  ZX_DEBUG_ASSERT(codec_packet->buffer());

  AVPacket packet;
  av_init_packet(&packet);
  packet.data =
      codec_packet->buffer()->buffer_base() + codec_packet->start_offset();
  packet.size = codec_packet->valid_length_bytes();

  if (codec_packet->has_timestamp_ish()) {
    packet.pts = codec_packet->timestamp_ish();
  }

  return avcodec_send_packet(avcodec_context_.get(), &packet);
}

std::pair<int, AvCodecContext::AVFramePtr> AvCodecContext::ReceiveFrame() {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(avcodec_is_open(avcodec_context_.get()));
  ZX_DEBUG_ASSERT(av_codec_is_decoder(avcodec_context_->codec));

  AVFramePtr frame(av_frame_alloc(),
                   [](AVFrame* frame) { av_frame_free(&frame); });
  // If we can't allocate a frame, abort this isolate process.
  ZX_ASSERT(frame);

  int result_code = avcodec_receive_frame(avcodec_context_.get(), frame.get());
  if (result_code < 0) {
    return {result_code, nullptr};
  }

  return {result_code, std::move(frame)};
}

int AvCodecContext::EndStream() {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(avcodec_is_open(avcodec_context_.get()));
  ZX_DEBUG_ASSERT(av_codec_is_decoder(avcodec_context_->codec));
  return avcodec_send_packet(avcodec_context_.get(), nullptr);
}

AvCodecContext::DecodedOutputInfo AvCodecContext::decoded_output_info(
    AVFrame* frame) const {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(avcodec_is_open(avcodec_context_.get()));
  ZX_DEBUG_ASSERT(av_codec_is_decoder(avcodec_context_->codec));

  AvCodecContext::DecodedOutputInfo decoded_output_info;
  decoded_output_info.coded_width = frame->width;
  decoded_output_info.coded_height = frame->height;
  decoded_output_info.width =
      frame->width - frame->crop_top - frame->crop_bottom;
  decoded_output_info.height =
      frame->height - frame->crop_left - frame->crop_right;
  av_image_fill_linesizes(decoded_output_info.linesizes,
                          static_cast<AVPixelFormat>(frame->format),
                          frame->width);
  decoded_output_info.buffer_bytes_needed = av_image_get_buffer_size(
      static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
      /*linesizes_alignment=*/1);
  if (frame->sample_aspect_ratio.num) {
    decoded_output_info.sample_aspect_ratio = std::make_pair(
        frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den);
  }

  return decoded_output_info;
}

// static
int AvCodecContext::GetBufferCallbackRouter(AVCodecContext* avcodec_context,
                                            AVFrame* frame, int flags) {
  auto instance = reinterpret_cast<AvCodecContext*>(avcodec_context->opaque);
  ZX_DEBUG_ASSERT(instance);
  return instance->GetBufferHandler(avcodec_context, frame, flags);
}

int AvCodecContext::GetBufferHandler(AVCodecContext* avcodec_context,
                                     AVFrame* frame, int flags) {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(get_buffer_callback_);
  ZX_DEBUG_ASSERT(frame->width);
  // TODO(turnage): Accept 10 bit YUV formats.
  ZX_DEBUG_ASSERT(frame->format == AV_PIX_FMT_YUV420P);

  return get_buffer_callback_(decoded_output_info(frame), avcodec_context,
                              frame, flags);
}

AvCodecContext::AvCodecContext(
    std::unique_ptr<AVCodecContext, fit::function<void(AVCodecContext*)>>
        avcodec_context,
    GetBufferCallback get_buffer_callback)
    : avcodec_context_(std::move(avcodec_context)),
      get_buffer_callback_(std::move(get_buffer_callback)) {
  ZX_DEBUG_ASSERT(avcodec_context_);
  ZX_DEBUG_ASSERT(get_buffer_callback_);

  avcodec_context_->opaque = this;
}