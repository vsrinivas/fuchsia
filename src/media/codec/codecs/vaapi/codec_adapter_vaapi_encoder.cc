// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_vaapi_encoder.h"

#include <zircon/status.h>

#include <va/va_drmcommon.h>

#include "h264_accelerator.h"
#include "media/base/video_codecs.h"
#include "media/gpu/h264_decoder.h"
#include "src/media/codec/codecs/vaapi/third_party/chromium/h264_vaapi_video_encoder_delegate.h"
#include "src/media/codec/codecs/vaapi/third_party/chromium/vaapi_picture.h"
#include "src/media/codec/codecs/vaapi/third_party/chromium/vaapi_wrapper.h"
#include "src/media/third_party/chromium_media/media/gpu/gpu_video_encode_accelerator_helpers.h"

CodecAdapterVaApiEncoder::CodecAdapterVaApiEncoder(std::mutex& lock,
                                                   CodecAdapterEvents* codec_adapter_events)
    : CodecAdapter(lock, codec_adapter_events) {
  ZX_DEBUG_ASSERT(events_);
}
CodecAdapterVaApiEncoder::~CodecAdapterVaApiEncoder() {
  input_processing_loop_.Shutdown();
  encoder_.reset();
}

void CodecAdapterVaApiEncoder::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  if (!initial_input_format_details.has_format_details_version_ordinal()) {
    events_->onCoreCodecFailCodec(
        "CoreCodecInit(): Initial input format details missing version "
        "ordinal.");
    return;
  }
  // Will always be 0 for now.
  input_format_details_version_ordinal_ =
      initial_input_format_details.format_details_version_ordinal();
  vaapi_wrapper_ = std::make_shared<media::VaapiWrapper>();

  if (!HandleInputFormatChange(initial_input_format_details, true)) {
    return;
  }
  zx_status_t result =
      input_processing_loop_.StartThread("input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "CodecCodecInit(): Failed to start input processing thread with "
        "zx_status_t: %d",
        result);
    return;
  }
}

bool CodecAdapterVaApiEncoder::HandleInputFormatChange(
    const fuchsia::media::FormatDetails& input_format_details, bool initial) {
  VADisplay va_dpy = VADisplayWrapper::GetSingleton()->display();
  const std::string& mime_type = input_format_details.mime_type();
  if (mime_type != "video/h264") {
    events_->onCoreCodecFailCodec("HandleInputFormatChange(): Unknown mime_type %s\n",
                                  mime_type.c_str());
    return false;
  }
  if (!input_format_details.has_domain()) {
    events_->onCoreCodecFailCodec("HandleInputFormatChange(): No domain");
    return false;
  }
  if (!input_format_details.domain().is_video()) {
    events_->onCoreCodecFailCodec("HandleInputFormatChange(): Input not video");
    return false;
  }
  if (!input_format_details.domain().video().is_uncompressed()) {
    events_->onCoreCodecFailCodec("HandleInputFormatChange(): Input not uncompressed");
    return false;
  }
  uint32_t width = input_format_details.domain().video().uncompressed().image_format.display_width;
  uint32_t height =
      input_format_details.domain().video().uncompressed().image_format.display_height;
  auto checked_width = safemath::MakeCheckedNum(width).Cast<int>();
  auto checked_height = safemath::MakeCheckedNum(height).Cast<int>();
  if (!checked_width.IsValid() || checked_width.ValueOrDie() == 0) {
    events_->onCoreCodecFailCodec("HandleInputFormatChange(): Initial width %d invalid", width);
    return false;
  }
  if (!checked_height.IsValid() || checked_height.ValueOrDie() == 0) {
    events_->onCoreCodecFailCodec("HandleInputFormatChange(): Initial height %d invalid", height);
    return false;
  }
  uint32_t coded_width =
      input_format_details.domain().video().uncompressed().image_format.coded_width;
  uint32_t coded_height =
      input_format_details.domain().video().uncompressed().image_format.coded_height;
  auto checked_coded_width = safemath::MakeCheckedNum(coded_width).Cast<int>();
  auto checked_coded_height = safemath::MakeCheckedNum(coded_height).Cast<int>();
  if (!checked_coded_width.IsValid() || checked_coded_width.ValueOrDie() == 0) {
    events_->onCoreCodecFailCodec("HandleInputFormatChange(): Initial coded width %d invalid",
                                  coded_width);
    return false;
  }
  if (!checked_coded_height.IsValid() || checked_coded_height.ValueOrDie() == 0) {
    events_->onCoreCodecFailCodec("HandleInputFormatChange(): Initial height %d invalid",
                                  coded_height);
    return false;
  }

  bool reset_encoder = initial;

  gfx::Size display_size(checked_width.ValueOrDie(), checked_height.ValueOrDie());

  if (display_size_ != display_size) {
    reset_encoder = true;
    input_surface_.reset();

    std::lock_guard lock(surfaces_lock_);
    // Increment surface generation so all existing surfaces will be freed
    // when they're released instead of being returned to the pool.
    surface_size_ = display_size_;
    surface_generation_++;
    surfaces_.clear();
  }

  display_size_ = display_size;
  coded_size_ = gfx::Size(checked_coded_width.ValueOrDie(), checked_coded_height.ValueOrDie());
  if (display_size_.width() > coded_size_.width() ||
      display_size_.height() > coded_size_.height()) {
    events_->onCoreCodecFailCodec(
        "HandleInputFormatChange(): Display dimensions %s larger than coded dimensions %s",
        display_size_.ToString().c_str(), coded_size_.ToString().c_str());
    return false;
  }

  auto accelerator_config = media::VideoEncodeAccelerator::Config();
  accelerator_config.input_visible_size = display_size_;
  accelerator_config.output_profile = media::H264PROFILE_HIGH;
  media::VaapiVideoEncoderDelegate::Config ave_config;

  VAConfigAttrib attrib;
  attrib.type = VAConfigAttribEncMaxRefFrames;
  // TODO: Cache this value instead of querying every time.
  VAStatus va_res = vaGetConfigAttributes(va_dpy, va_profile_, va_entrypoint_, &attrib, 1);
  if (va_res != VA_STATUS_SUCCESS) {
    events_->onCoreCodecFailCodec("vaGetConfigAttributes failed: %d", va_res);
    return false;
  }

  ave_config.max_num_ref_frames = attrib.value;

  // Defaults taken from fuchsia::media fidl.
  accelerator_config.initial_framerate = 30;
  accelerator_config.bitrate = media::Bitrate::ConstantBitrate(200000u);
  accelerator_config.gop_length = 8 + 1;
  if (input_format_details.has_encoder_settings()) {
    auto& encoder_settings = input_format_details.encoder_settings();
    if (encoder_settings.is_h264()) {
      auto& h264 = encoder_settings.h264();
      if (h264.has_frame_rate()) {
        accelerator_config.initial_framerate = h264.frame_rate();
      }
      if (h264.has_bit_rate()) {
        accelerator_config.bitrate = media::Bitrate::ConstantBitrate(h264.bit_rate());
      }
      if (h264.has_gop_size()) {
        // gop_length includes the initial IDR frame, so add 1.
        auto new_gop_size = safemath::CheckAdd(h264.gop_size(), 1);
        if (!new_gop_size.IsValid()) {
          events_->onCoreCodecFailCodec("HandleInputFormatChange(): Invalid gop_size %u",
                                        h264.gop_size());
          return false;
        }
        accelerator_config.gop_length = new_gop_size.ValueOrDie();
      }
      if (h264.has_force_key_frame()) {
        next_frame_keyframe_ = next_frame_keyframe_ || h264.force_key_frame();
      }
      if (h264.has_quantization_params()) {
        events_->onCoreCodecFailCodec(
            "HandleInputFormatChange(): Setting quantization params not supported");
        return false;
      }
    } else {
      events_->onCoreCodecFailCodec("HandleInputFormatChange(): Incorrect encoder setting type");
      return false;
    }
  }

  if (accelerator_config.gop_length != accelerator_config_.gop_length) {
    reset_encoder = true;
  }

  accelerator_config_ = accelerator_config;

  if (reset_encoder) {
    context_id_ = {};
    config_.reset();
    encoder_ = std::make_unique<media::H264VaapiVideoEncoderDelegate>(vaapi_wrapper_,
                                                                      fit::function<void()>());
    if (!encoder_->Initialize(accelerator_config, ave_config)) {
      events_->onCoreCodecFailCodec("Failed to initialize encoder");
      return false;
    }
    VAConfigAttrib attribs[1];
    attribs[0].type = VAConfigAttribRTFormat;
    attribs[0].value = VA_RT_FORMAT_YUV420;
    VAConfigID config_id;
    VAStatus va_status = vaCreateConfig(va_dpy, va_profile_, va_entrypoint_, attribs,
                                        std::size(attribs), &config_id);
    if (va_status != VA_STATUS_SUCCESS) {
      events_->onCoreCodecFailCodec("Failed to create config.");
      return false;
    }
    config_.emplace(config_id);
  } else {
    if (!encoder_->UpdateRates(media::AllocateBitrateForDefaultEncoding(accelerator_config_),
                               *accelerator_config.initial_framerate)) {
      events_->onCoreCodecFailCodec("Failed to update bitrate");
      return false;
    }
  }
  if (!input_surface_) {
    VASurfaceID input_surface;
    VAStatus va_res = vaCreateSurfaces(va_dpy, VA_RT_FORMAT_YUV420, display_size_.width(),
                                       display_size_.height(), &input_surface, 1, nullptr, 0);
    if (va_res != VA_STATUS_SUCCESS) {
      events_->onCoreCodecFailCodec("vaCreateSurfaces failed: %d", va_res);
      return false;
    }

    input_surface_ = ScopedSurfaceID(input_surface);
  }
  return true;
}

void CodecAdapterVaApiEncoder::ProcessInputLoop() {
  std::optional<CodecInputItem> maybe_input_item;

  while ((maybe_input_item = input_queue_.WaitForElement())) {
    CodecInputItem input_item = std::move(maybe_input_item.value());
    if (input_item.is_format_details()) {
      if (!HandleInputFormatChange(input_item.format_details(), false)) {
        // If there's an error, HandleInputFormatChange will signal an async error itself.
        return;
      }

    } else if (input_item.is_end_of_stream()) {
      // Chromium's video encoder code doesn't support frame reordering, so all frames will already
      // have been output and no additional flushing is necessary.
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
    } else if (input_item.is_packet()) {
      if (!ProcessPacket(input_item.packet())) {
        // If there's an error, ProcessPacket will signal an async error itself.
        return;
      }
    }
  }
}

bool CodecAdapterVaApiEncoder::ProcessPacket(CodecPacket* packet) {
  VADisplay va_dpy = VADisplayWrapper::GetSingleton()->display();
  if (!context_id_) {
    // We intentionally delay triggering the output buffer allocation until some input has arrived,
    // to avoid clients potentially taking a generally-incorrect dependency on output config
    // happening prior to any delivered input.
    events_->onCoreCodecMidStreamOutputConstraintsChange(true);
    VAContextID context_id;
    VAStatus va_res =
        vaCreateContext(va_dpy, config_->id(), display_size_.width(), display_size_.height(),
                        VA_PROGRESSIVE, nullptr, 0, &context_id);
    if (va_res != VA_STATUS_SUCCESS) {
      events_->onCoreCodecFailCodec("vaCreateContext failed: %d", va_res);
      return false;
    }
    context_id_.emplace(context_id);
    vaapi_wrapper_->set_context_id(context_id);
  }
  {
    std::lock_guard lock(surfaces_lock_);
    if (surfaces_.empty()) {
      std::vector<VASurfaceID> va_surfaces;
      va_surfaces.resize(1);
      VAStatus va_res = vaCreateSurfaces(va_dpy, VA_RT_FORMAT_YUV420, display_size_.width(),
                                         display_size_.height(), va_surfaces.data(),
                                         static_cast<uint32_t>(va_surfaces.size()), nullptr, 0);
      if (va_res != VA_STATUS_SUCCESS) {
        events_->onCoreCodecFailCodec("vaCreateSurfaces failed: %d", va_res);
        return false;
      }

      for (VASurfaceID id : va_surfaces) {
        surfaces_.emplace_back(id);
      }
    }
  }

  auto video_frame = std::make_shared<media::VideoFrame>();
  video_frame->display_size = display_size_;
  video_frame->coded_size = coded_size_;
  video_frame->base = packet->buffer()->base();
  video_frame->size_bytes = packet->buffer()->size();
  video_frame->stride =
      fbl::round_up(static_cast<uint32_t>(display_size_.width()),
                    buffer_settings_[kInputPort]->image_format_constraints.bytes_per_row_divisor);

  scoped_refptr<VASurface> va_surface = GetVASurface();
  VABufferID coded_buffer;
  // The VA-API driver can efficiently reuse deleted buffers, so we create a enw buffer every frame.
  VAStatus va_res =
      vaCreateBuffer(va_dpy, context_id_->id(), VAEncCodedBufferType,
                     static_cast<uint32_t>(media::GetEncodeBitstreamBufferSize(coded_size_)), 1,
                     nullptr, &coded_buffer);
  if (va_res != VA_STATUS_SUCCESS) {
    events_->onCoreCodecFailCodec("vaCreateBuffer failed: %d", va_res);
    return false;
  }

  auto buffer_id = std::make_unique<ScopedBufferID>(coded_buffer);
  auto picture = std::make_shared<VaapiPicture>();
  picture->va_surface = std::move(va_surface);

  auto encode_job = std::make_unique<media::VaapiVideoEncoderDelegate::EncodeJob>(
      video_frame, false, input_surface_->id(), display_size_, picture, std::move(buffer_id));

  if (next_frame_keyframe_) {
    encode_job->ProduceKeyframe();
    next_frame_keyframe_ = false;
  }

  if (!encoder_->Encode(*encode_job)) {
    events_->onCoreCodecFailCodec("Encoding video failed");
    return false;
  }
  auto encode_result = encoder_->GetEncodeResult(std::move(encode_job));
  std::optional<uint64_t> input_timestampish = std::nullopt;
  if (packet->has_timestamp_ish()) {
    input_timestampish = packet->timestamp_ish();
  }
  events_->onCoreCodecInputPacketDone(packet);
  const CodecBuffer* buffer =
      output_buffer_pool_.AllocateBuffer(encode_result->metadata().payload_size_bytes);
  if (!buffer) {
    // May fail if codec is shutting down.
    return false;
  }
  {
    uint8_t* target_ptr = buffer->base();
    void* buffer_data;
    VAStatus va_res = vaMapBuffer(va_dpy, encode_result->coded_buffer_id(), &buffer_data);
    if (va_res != VA_STATUS_SUCCESS) {
      events_->onCoreCodecFailCodec("Failed to map buffer: %d\n", va_res);
      return false;
    }
    auto* buffer_segment = reinterpret_cast<VACodedBufferSegment*>(buffer_data);

    while (buffer_segment) {
      DCHECK(buffer_segment->buf);

      memcpy(target_ptr, buffer_segment->buf, buffer_segment->size);

      target_ptr += buffer_segment->size;
      buffer_segment = reinterpret_cast<VACodedBufferSegment*>(buffer_segment->next);
    }
    vaUnmapBuffer(va_dpy, encode_result->coded_buffer_id());
  }
  std::optional<CodecPacket*> maybe_output_packet = free_output_packets_.WaitForElement();
  if (!maybe_output_packet) {
    // May fail if codec is shutting down.
    return false;
  }
  auto output_packet = *maybe_output_packet;
  output_packet->SetBuffer(buffer);
  output_packet->SetStartOffset(0);
  output_packet->SetValidLengthBytes(
      static_cast<uint32_t>(encode_result->metadata().payload_size_bytes));
  if (input_timestampish) {
    output_packet->SetTimstampIsh(*input_timestampish);
  } else {
    output_packet->ClearTimestampIsh();
  }

  {
    std::lock_guard<std::mutex> lock(lock_);
    ZX_DEBUG_ASSERT(in_use_by_client_.find(output_packet) == in_use_by_client_.end());

    in_use_by_client_.emplace(output_packet, VaApiEncoderOutput(buffer->base(), this));
  }
  events_->onCoreCodecOutputPacket(output_packet,
                                   /*error_detected_before=*/false,
                                   /*error_detected_during=*/false);
  return true;
}

void CodecAdapterVaApiEncoder::CleanUpAfterStream() {}

scoped_refptr<VASurface> CodecAdapterVaApiEncoder::GetVASurface() {
  uint64_t surface_generation;
  VASurfaceID surface_id;
  gfx::Size pic_size;
  {
    std::lock_guard lock(surfaces_lock_);
    if (surfaces_.empty())
      return {};
    surface_id = surfaces_.back().release();
    surfaces_.pop_back();
    surface_generation = surface_generation_;
    pic_size = surface_size_;
  }
  return std::make_shared<VASurface>(
      surface_id, pic_size, VA_RT_FORMAT_YUV420,
      fit::function<void(VASurfaceID)>([this, surface_generation](VASurfaceID surface_id) {
        std::lock_guard lock(surfaces_lock_);
        if (surface_generation_ == surface_generation) {
          surfaces_.emplace_back(surface_id);
        } else {
          vaDestroySurfaces(VADisplayWrapper::GetSingleton()->display(), &surface_id, 1);
        }
      }));
}

VaApiEncoderOutput::~VaApiEncoderOutput() {
  if (adapter_)
    adapter_->output_buffer_pool_.FreeBuffer(base_address_);
}

VaApiEncoderOutput::VaApiEncoderOutput(VaApiEncoderOutput&& other) noexcept {
  adapter_ = other.adapter_;
  base_address_ = other.base_address_;
  other.adapter_ = nullptr;
}

VaApiEncoderOutput& VaApiEncoderOutput::operator=(VaApiEncoderOutput&& other) noexcept {
  adapter_ = other.adapter_;
  base_address_ = other.base_address_;
  other.adapter_ = nullptr;
  return *this;
}
