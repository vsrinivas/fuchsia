// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>

#include "garnet/examples/media/vu_meter/vu_meter_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include "lib/media/fidl/media_capturer.fidl.h"
#include "lib/media/fidl/media_transport.fidl.h"
#include "lib/media/transport/media_packet_consumer_base.h"
#include "lib/ui/view_framework/skia_view.h"

namespace examples {

class VuMeterView : public mozart::SkiaView {
 public:
  VuMeterView(mozart::ViewManagerPtr view_manager,
              fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
              app::ApplicationContext* application_context,
              const VuMeterParams& params);

  ~VuMeterView() override;

 private:
  static constexpr uint32_t kBytesPerSample = 2;
  static constexpr float kVuFullWidth = 35000.0f;
  static constexpr float kFastDecay = 0.0001f;
  static constexpr float kSlowDecay = 0.00003f;

  class PacketConsumer : public media::MediaPacketConsumerBase {
   public:
    PacketConsumer(VuMeterView* owner);

   protected:
    void OnPacketSupplied(
        std::unique_ptr<SuppliedPacket> supplied_packet) override;

   private:
    VuMeterView* owner_;
  };

  class PeakFilter {
   public:
    PeakFilter(float decay) : multiplier_(1.0f - decay) {}

    float Process(float in) {
      if (current_ < in) {
        current_ = in;
      } else {
        current_ *= multiplier_;
      }
      return current_;
    }

    float current() { return current_; }

   private:
    float multiplier_;
    float current_ = 0;
  };

  // |BaseView|:
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr presentation_info) override;
  bool OnInputEvent(mozart::InputEventPtr event) override;

  // Draws the UI.
  void DrawContent(SkCanvas* canvas);

  // Toggles between start and stop.
  void ToggleStartStop();

  // Selects the media type to capture.
  void OnGotSupportedMediaTypes(
      fidl::Array<media::MediaTypeSetPtr> media_types);

  void OnPacketSupplied(
      std::unique_ptr<media::MediaPacketConsumerBase::SuppliedPacket>
          supplied_packet);

  media::MediaCapturerPtr media_capturer_;
  media::MediaPacketProducerPtr packet_producer_;
  PacketConsumer packet_consumer_;
  bool started_ = false;
  uint32_t channels_ = 0;
  uint32_t frames_per_second_ = 0;
  PeakFilter fast_left_;
  PeakFilter fast_right_;
  PeakFilter slow_left_;
  PeakFilter slow_right_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VuMeterView);
};

}  // namespace examples
