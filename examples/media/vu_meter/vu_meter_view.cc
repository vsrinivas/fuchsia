// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/vu_meter/vu_meter_view.h"

#include <hid/usages.h>

#include <iomanip>

#include "lib/app/cpp/connect.h"
#include "garnet/examples/media/vu_meter/vu_meter_params.h"
#include "lib/media/fidl/media_service.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"

namespace examples {

VuMeterView::VuMeterView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    app::ApplicationContext* application_context,
    const VuMeterParams& params)
    : mozart::SkiaView(std::move(view_manager),
                       std::move(view_owner_request),
                       "VU Meter"),
      packet_consumer_(this),
      fast_left_(kFastDecay),
      fast_right_(kFastDecay),
      slow_left_(kSlowDecay),
      slow_right_(kSlowDecay) {
  FTL_DCHECK(params.is_valid());

  auto media_service =
      application_context->ConnectToEnvironmentService<media::MediaService>();
  media_service->CreateAudioCapturer(media_capturer_.NewRequest());

  media_capturer_.set_connection_error_handler([this]() {
    FTL_LOG(ERROR) << "Connection error occurred. Quitting.";
    media_capturer_.reset();
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  media_capturer_->GetPacketProducer(packet_producer_.NewRequest());

  fidl::InterfaceHandle<media::MediaPacketConsumer> packet_consumer_handle;
  packet_consumer_.Bind(&packet_consumer_handle);

  packet_producer_->Connect(std::move(packet_consumer_handle), []() {});

  // Set demand on the consumer to 2 packets. This obligates the producer to
  // try to keep two packets in flight at any given time. We could set this to
  // 1, but that would effectively serialize the production of packets with
  // OnPacketSupplied. Using a value of 2, when we're done with one packet
  // (OnPacketSupplied returns), there's already another packet queued at the
  // consumer, so we don't have to wait. While we're consuming that new packet,
  // the producer is preparing another one.
  packet_consumer_.SetDemand(2);

  // Fetch the list of supported media types
  media_capturer_->GetSupportedMediaTypes(
      [this](fidl::Array<media::MediaTypeSetPtr> supported_media_types) {
        OnGotSupportedMediaTypes(std::move(supported_media_types));
      });
}

VuMeterView::~VuMeterView() {}

bool VuMeterView::OnInputEvent(mozart::InputEventPtr event) {
  FTL_DCHECK(event);
  bool handled = false;
  if (event->is_pointer()) {
    auto& pointer = event->get_pointer();
    if (pointer->phase == mozart::PointerEvent::Phase::DOWN) {
      ToggleStartStop();
      handled = true;
    }
  } else if (event->is_keyboard()) {
    auto& keyboard = event->get_keyboard();
    if (keyboard->phase == mozart::KeyboardEvent::Phase::PRESSED) {
      switch (keyboard->hid_usage) {
        case HID_USAGE_KEY_SPACE:
          ToggleStartStop();
          handled = true;
          break;
        case HID_USAGE_KEY_Q:
          mtl::MessageLoop::GetCurrent()->PostQuitTask();
          handled = true;
          break;
        default:
          break;
      }
    }
  }
  return handled;
}

void VuMeterView::OnSceneInvalidated(
    scenic::PresentationInfoPtr presentation_info) {
  SkCanvas* canvas = AcquireCanvas();
  if (canvas) {
    DrawContent(canvas);
    ReleaseAndSwapCanvas();
  }
}

void VuMeterView::DrawContent(SkCanvas* canvas) {
  canvas->clear(SK_ColorBLACK);

  SkPaint paint;
  paint.setFlags(SkPaint::kAntiAlias_Flag);

  paint.setColor(SK_ColorCYAN);
  canvas->drawCircle(
      logical_size().width / 3.0f, logical_size().height / 2,
      (fast_left_.current() * logical_size().width / 2) / kVuFullWidth, paint);
  canvas->drawCircle(
      2.0f * logical_size().width / 3.0f, logical_size().height / 2,
      (fast_right_.current() * logical_size().width / 2) / kVuFullWidth, paint);

  paint.setColor(SK_ColorWHITE);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(SkIntToScalar(3));
  canvas->drawCircle(
      logical_size().width / 3.0f, logical_size().height / 2,
      (slow_left_.current() * logical_size().width / 2) / kVuFullWidth, paint);
  canvas->drawCircle(
      2.0f * logical_size().width / 3.0f, logical_size().height / 2,
      (slow_right_.current() * logical_size().width / 2) / kVuFullWidth, paint);
}

void VuMeterView::ToggleStartStop() {
  if ((started_) || !channels_) {
    media_capturer_->Stop();
    started_ = false;
  } else {
    media_capturer_->Start();
    started_ = true;
  }

  InvalidateScene();
}

void VuMeterView::OnGotSupportedMediaTypes(
    fidl::Array<media::MediaTypeSetPtr> media_types) {
  // Look for a media type we like.
  for (const auto& type : media_types) {
    if (type->medium != media::MediaTypeMedium::AUDIO) {
      continue;
    }

    FTL_DCHECK(!type->details.is_null());
    FTL_DCHECK(type->details->is_audio());
    const auto& audio_details = *(type->details->get_audio());
    if (audio_details.sample_format != media::AudioSampleFormat::SIGNED_16)
      continue;

    channels_ = std::max(std::min(2u, audio_details.max_channels),
                         audio_details.min_channels);
    frames_per_second_ =
      std::max(std::min(2u, audio_details.max_frames_per_second),
               audio_details.min_frames_per_second);

    auto tmp = media::AudioMediaTypeDetails::New();
    tmp->sample_format = media::AudioSampleFormat::SIGNED_16;
    tmp->channels = channels_;
    tmp->frames_per_second = frames_per_second_;

    auto cfg = media::MediaType::New();
    cfg->medium = media::MediaTypeMedium::AUDIO;
    cfg->encoding = media::MediaType::kAudioEncodingLpcm;
    cfg->details = media::MediaTypeDetails::New();
    cfg->details->set_audio(std::move(tmp));

    FTL_LOG(INFO) << "Configured capture for "
                  << channels_
                  << " channel" << ((channels_ == 1) ? " " : "s ")
                  << frames_per_second_
                  << " Hz 16-bit LPCM";

    media_capturer_->SetMediaType(std::move(cfg));
    ToggleStartStop();
    return;
  }

  FTL_LOG(WARNING) << "No compatible media types detect among the "
                   << media_types.size()
                   << " supplied.";
}

void VuMeterView::OnPacketSupplied(
    std::unique_ptr<media::MediaPacketConsumerBase::SuppliedPacket>
        supplied_packet) {
  // TODO(dalesat): Synchronize display and captured audio.
  FTL_DCHECK(supplied_packet->payload_size() % (kBytesPerSample * channels_) ==
             0);
  int16_t* sample = static_cast<int16_t*>(supplied_packet->payload());
  uint32_t frame_count =
      supplied_packet->payload_size() / (kBytesPerSample * channels_);

  uint32_t right_channel_ndx = (channels_ == 1) ? 0 : 1;
  for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    int16_t abs_sample = std::abs(sample[0]);
    fast_left_.Process(abs_sample);
    slow_left_.Process(abs_sample);

    abs_sample = std::abs(sample[right_channel_ndx]);
    fast_right_.Process(abs_sample);
    slow_right_.Process(abs_sample);

    sample += channels_;
  }

  InvalidateScene();
}

VuMeterView::PacketConsumer::PacketConsumer(VuMeterView* owner)
    : owner_(owner) {
  FTL_DCHECK(owner_);
}

void VuMeterView::PacketConsumer::OnPacketSupplied(
    std::unique_ptr<SuppliedPacket> supplied_packet) {
  owner_->OnPacketSupplied(std::move(supplied_packet));
}

}  // namespace examples
