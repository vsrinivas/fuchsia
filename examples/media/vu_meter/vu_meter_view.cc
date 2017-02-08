// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/vu_meter/vu_meter_view.h"

#include <hid/usages.h>

#include <iomanip>

#include "application/lib/app/connect.h"
#include "apps/media/examples/vu_meter/vu_meter_params.h"
#include "apps/media/services/media_service.fidl.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace examples {

namespace {
constexpr uint32_t kContentImageResourceId = 1u;

constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

}  // namespace

VuMeterView::VuMeterView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    modular::ApplicationContext* application_context,
    const VuMeterParams& params)
    : mozart::BaseView(std::move(view_manager),
                       std::move(view_owner_request),
                       "VU Meter"),
      input_handler_(GetViewServiceProvider(), this),
      packet_consumer_(this),
      fast_left_(kFastDecay),
      fast_right_(kFastDecay),
      slow_left_(kSlowDecay),
      slow_right_(kSlowDecay) {
  FTL_DCHECK(params.is_valid());

  media::MediaServicePtr media_service =
      application_context->ConnectToEnvironmentService<media::MediaService>();

  // Get an audio capturer.
  media_service->CreateAudioCapturer(media_capturer_.NewRequest());

  media_capturer_.set_connection_error_handler([this]() {
    FTL_LOG(ERROR) << "Connection error occurred. Quitting.";
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

  ToggleStartStop();
}

VuMeterView::~VuMeterView() {}

void VuMeterView::OnEvent(mozart::EventPtr event,
                          const OnEventCallback& callback) {
  FTL_DCHECK(event);
  bool handled = false;
  switch (event->action) {
    case mozart::EventType::POINTER_DOWN:
      FTL_DCHECK(event->pointer_data);
      ToggleStartStop();
      handled = true;
      break;

    case mozart::EventType::KEY_PRESSED:
      FTL_DCHECK(event->key_data);
      if (!event->key_data) {
        break;
      }
      switch (event->key_data->hid_usage) {
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
      break;
    default:
      break;
  }

  callback(handled);
}

void VuMeterView::OnDraw() {
  FTL_DCHECK(properties());

  auto update = mozart::SceneUpdate::New();

  const mozart::Size& view_size = *properties()->view_layout->size;

  if (view_size.width == 0 || view_size.height == 0) {
    // Nothing to show yet.
    update->nodes.insert(kRootNodeId, mozart::Node::New());
  } else {
    mozart::RectF bounds;
    bounds.width = view_size.width;
    bounds.height = view_size.height;

    mozart::ImagePtr image;
    sk_sp<SkSurface> surface =
        mozart::MakeSkSurface(view_size, &buffer_producer_, &image);
    FTL_CHECK(surface);
    DrawContent(view_size, surface->getCanvas());

    auto content_resource = mozart::Resource::New();
    content_resource->set_image(mozart::ImageResource::New());
    content_resource->get_image()->image = std::move(image);
    update->resources.insert(kContentImageResourceId,
                             std::move(content_resource));

    auto root_node = mozart::Node::New();
    root_node->op = mozart::NodeOp::New();
    root_node->op->set_image(mozart::ImageNodeOp::New());
    root_node->op->get_image()->content_rect = bounds.Clone();
    root_node->op->get_image()->image_resource_id = kContentImageResourceId;
    root_node->hit_test_behavior = mozart::HitTestBehavior::New();
    update->nodes.insert(kRootNodeId, std::move(root_node));
  }

  scene()->Update(std::move(update));
  scene()->Publish(CreateSceneMetadata());

  Invalidate();
}

void VuMeterView::DrawContent(const mozart::Size& size, SkCanvas* canvas) {
  canvas->clear(SK_ColorBLACK);

  SkPaint paint;
  paint.setFlags(SkPaint::kAntiAlias_Flag);

  paint.setColor(SK_ColorCYAN);
  canvas->drawCircle(size.width / 3.0f, size.height / 2,
                     (fast_left_.current() * size.width / 2) / kVuFullWidth,
                     paint);
  canvas->drawCircle(2.0f * size.width / 3.0f, size.height / 2,
                     (fast_right_.current() * size.width / 2) / kVuFullWidth,
                     paint);

  paint.setColor(SK_ColorWHITE);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(SkIntToScalar(3));
  canvas->drawCircle(size.width / 3.0f, size.height / 2,
                     (slow_left_.current() * size.width / 2) / kVuFullWidth,
                     paint);
  canvas->drawCircle(2.0f * size.width / 3.0f, size.height / 2,
                     (slow_right_.current() * size.width / 2) / kVuFullWidth,
                     paint);
}

void VuMeterView::ToggleStartStop() {
  if (started_) {
    media_capturer_->Stop();
    started_ = false;
  } else {
    media_capturer_->Start();
    started_ = true;
  }

  Invalidate();
}

void VuMeterView::OnPacketSupplied(
    std::unique_ptr<media::MediaPacketConsumerBase::SuppliedPacket>
        supplied_packet) {
  // TODO(dalesat): Synchronize display and captured audio.
  FTL_DCHECK(supplied_packet->payload_size() % (kBytesPerSample * kChannels) ==
             0);
  int16_t* sample = static_cast<int16_t*>(supplied_packet->payload());
  uint32_t frame_count =
      supplied_packet->payload_size() / kBytesPerSample / kChannels;

  for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    int16_t abs_sample = std::abs(*sample);
    fast_left_.Process(abs_sample);
    slow_left_.Process(abs_sample);
    ++sample;
    abs_sample = std::abs(*sample);
    fast_right_.Process(abs_sample);
    slow_right_.Process(abs_sample);
    ++sample;
  }
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
