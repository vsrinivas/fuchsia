// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <string>

#include "application/lib/app/connect.h"
#include "apps/mozart/lib/skia/skia_font_loader.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/input_handler.h"
#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace examples {

namespace {
constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

enum class Action {
  kHang10,
  kStutter30,
  kCrash,
};

struct Button {
  const char* label;
  Action action;
};

const Button kButtons[] = {
    {"Hang for 10 seconds", Action::kHang10},
    {"Stutter for 30 seconds", Action::kStutter30},
    {"Crash!", Action::kCrash},
};

constexpr SkScalar kButtonWidth = 300;
constexpr SkScalar kButtonHeight = 30;
constexpr SkScalar kMargin = 15;
}  // namespace

class JankView : public mozart::BaseView, public mozart::InputListener {
 public:
  JankView(mozart::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
           fonts::FontProviderPtr font_provider)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "Jank"),
        input_handler_(GetViewServiceProvider(), this),
        font_loader_(std::move(font_provider)) {
    font_loader_.LoadDefaultFont([this](sk_sp<SkTypeface> typeface) {
      FTL_CHECK(typeface);  // TODO(jeffbrown): Fail gracefully.

      typeface_ = std::move(typeface);
      Invalidate();
    });
  }

  ~JankView() override {}

 private:
  // |InputListener|:
  void OnEvent(mozart::InputEventPtr event,
               const OnEventCallback& callback) override {
    if (event->is_pointer()) {
      const mozart::PointerEventPtr& pointer = event->get_pointer();
      if (pointer->phase == mozart::PointerEvent::Phase::DOWN) {
        SkScalar x = pointer->x;
        SkScalar y = pointer->y;
        if (x >= kMargin && x <= kButtonWidth + kMargin) {
          int index = (y - kMargin) / (kButtonHeight + kMargin);
          if (index >= 0 &&
              size_t(index) < sizeof(kButtons) / sizeof(kButtons[0]) &&
              y < (kButtonHeight + kMargin) * (index + 1))
            OnClick(kButtons[index]);
        }
      }
    }
    callback(false);
  }

  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());

    if (!typeface_)
      return;  // wait for typeface to be loaded

    auto update = mozart::SceneUpdate::New();

    const mozart::Size& size = *properties()->view_layout->size;
    if (size.width > 0 && size.height > 0) {
      mozart::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;

      mozart::ImagePtr image;
      sk_sp<SkSurface> surface =
          mozart::MakeSkSurface(size, &buffer_producer_, &image);
      FTL_CHECK(surface);
      DrawContent(surface->getCanvas());

      auto content_resource = mozart::Resource::New();
      content_resource->set_image(mozart::ImageResource::New());
      content_resource->get_image()->image = std::move(image);
      update->resources.insert(kContentImageResourceId,
                               std::move(content_resource));

      auto root_node = mozart::Node::New();
      root_node->hit_test_behavior = mozart::HitTestBehavior::New();
      root_node->op = mozart::NodeOp::New();
      root_node->op->set_image(mozart::ImageNodeOp::New());
      root_node->op->get_image()->content_rect = bounds.Clone();
      root_node->op->get_image()->image_resource_id = kContentImageResourceId;
      update->nodes.insert(kRootNodeId, std::move(root_node));
    } else {
      auto root_node = mozart::Node::New();
      update->nodes.insert(kRootNodeId, std::move(root_node));
    }

    scene()->Update(std::move(update));
    scene()->Publish(CreateSceneMetadata());
    buffer_producer_.Tick();

    Invalidate();

    if (stutter_end_time_ > ftl::TimePoint::Now())
      sleep(2);
  }

  void DrawContent(SkCanvas* canvas) {
    SkScalar hsv[3] = {
        static_cast<SkScalar>(
            fmod(ftl::TimePoint::Now().ToEpochDelta().ToSecondsF() * 60, 360.)),
        1, 1};
    canvas->clear(SkHSVToColor(hsv));

    SkScalar x = kMargin;
    SkScalar y = kMargin;
    for (const auto& button : kButtons) {
      DrawButton(canvas, button.label,
                 SkRect::MakeXYWH(x, y, kButtonWidth, kButtonHeight));
      y += kButtonHeight + kMargin;
    }
  }

  void DrawButton(SkCanvas* canvas, const char* label, const SkRect& bounds) {
    SkPaint boxPaint;
    boxPaint.setColor(SkColorSetRGB(200, 200, 200));
    canvas->drawRect(bounds, boxPaint);
    boxPaint.setColor(SkColorSetRGB(40, 40, 40));
    boxPaint.setStyle(SkPaint::kStroke_Style);
    canvas->drawRect(bounds, boxPaint);

    SkPaint textPaint;
    textPaint.setColor(SK_ColorBLACK);
    textPaint.setTextSize(16);
    textPaint.setTextEncoding(SkPaint::kUTF8_TextEncoding);
    textPaint.setTypeface(typeface_);
    textPaint.setAntiAlias(true);
    SkRect textBounds;
    textPaint.measureText(label, strlen(label), &textBounds);
    canvas->drawText(label, strlen(label),
                     bounds.centerX() - textBounds.centerX(),
                     bounds.centerY() - textBounds.centerY(), textPaint);
  }

  void OnClick(const Button& button) {
    switch (button.action) {
      case Action::kHang10: {
        sleep(10);
        break;
      }

      case Action::kStutter30: {
        stutter_end_time_ =
            ftl::TimePoint::Now() + ftl::TimeDelta::FromSeconds(30);
        break;
      }

      case Action::kCrash: {
        abort();
        break;
      }
    }
  }

  mozart::InputHandler input_handler_;
  mozart::BufferProducer buffer_producer_;
  mozart::SkiaFontLoader font_loader_;
  sk_sp<SkTypeface> typeface_;
  ftl::TimePoint stutter_end_time_;

  FTL_DISALLOW_COPY_AND_ASSIGN(JankView);
};

}  // namespace examples

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  mozart::ViewProviderApp app([](mozart::ViewContext view_context) {
    return std::make_unique<examples::JankView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request),
        view_context.application_context
            ->ConnectToEnvironmentService<fonts::FontProvider>());
  });

  loop.Run();
  return 0;
}
