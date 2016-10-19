// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>
#include <unistd.h>

#include <string>

#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/input_handler.h"
#include "apps/mozart/lib/skia/skia_font_loader.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
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
  JankView(mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
           mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(app_connector.Pass(), view_owner_request.Pass(), "Jank"),
        input_handler_(GetViewServiceProvider(), this),
        font_loader_(BaseView::app_connector()) {
    font_loader_.LoadDefaultFont([this](sk_sp<SkTypeface> typeface) {
      FTL_CHECK(typeface);  // TODO(jeffbrown): Fail gracefully.

      typeface_ = std::move(typeface);
      Invalidate();
    });
  }

  ~JankView() override {}

 private:
  // |InputListener|:
  void OnEvent(mozart::EventPtr event,
               const OnEventCallback& callback) override {
    if (event->pointer_data &&
        event->action == mozart::EventType::POINTER_DOWN) {
      SkScalar x = event->pointer_data->x;
      SkScalar y = event->pointer_data->y;
      if (x >= kMargin && x <= kButtonWidth + kMargin) {
        int index = (y - kMargin) / (kButtonHeight + kMargin);
        if (index >= 0 &&
            size_t(index) < sizeof(kButtons) / sizeof(kButtons[0]) &&
            y < (kButtonHeight + kMargin) * (index + 1))
          OnClick(kButtons[index]);
      }
    }
    callback.Run(false);
  }

  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());

    if (!typeface_)
      return;  // wait for typeface to be loaded

    auto update = mozart::SceneUpdate::New();

    const mojo::Size& size = *properties()->view_layout->size;
    if (size.width > 0 && size.height > 0) {
      mojo::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;

      mozart::ImagePtr image;
      sk_sp<SkSurface> surface = mozart::MakeSkSurface(size, &image);
      FTL_CHECK(surface);
      DrawContent(surface->getCanvas());

      auto content_resource = mozart::Resource::New();
      content_resource->set_image(mozart::ImageResource::New());
      content_resource->get_image()->image = std::move(image);
      update->resources.insert(kContentImageResourceId,
                               content_resource.Pass());

      auto root_node = mozart::Node::New();
      root_node->hit_test_behavior = mozart::HitTestBehavior::New();
      root_node->op = mozart::NodeOp::New();
      root_node->op->set_image(mozart::ImageNodeOp::New());
      root_node->op->get_image()->content_rect = bounds.Clone();
      root_node->op->get_image()->image_resource_id = kContentImageResourceId;
      update->nodes.insert(kRootNodeId, root_node.Pass());
    } else {
      auto root_node = mozart::Node::New();
      update->nodes.insert(kRootNodeId, root_node.Pass());
    }

    scene()->Update(update.Pass());
    scene()->Publish(CreateSceneMetadata());

    Invalidate();

    if (MojoGetTimeTicksNow() < stutter_end_time_)
      sleep(2);
  }

  void DrawContent(SkCanvas* canvas) {
    SkScalar hsv[3] = {static_cast<SkScalar>(
                           fmod(MojoGetTimeTicksNow() * 0.000001 * 60, 360.)),
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
    FTL_LOG(INFO) << "Clicked: " << button.label;

    switch (button.action) {
      case Action::kHang10: {
        sleep(10);
        break;
      }

      case Action::kStutter30: {
        stutter_end_time_ = MojoGetTimeTicksNow() + 30 * 1000000;
        break;
      }

      case Action::kCrash: {
        abort();
        break;
      }
    }
  }

  mozart::InputHandler input_handler_;
  mozart::SkiaFontLoader font_loader_;
  sk_sp<SkTypeface> typeface_;
  int64_t stutter_end_time_ = 0u;

  FTL_DISALLOW_COPY_AND_ASSIGN(JankView);
};

class JankApp : public mozart::ViewProviderApp {
 public:
  JankApp() {}
  ~JankApp() override {}

  void CreateView(
      const std::string& connection_url,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override {
    new JankView(mojo::CreateApplicationConnector(shell()),
                 view_owner_request.Pass());
  }

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(JankApp);
};

}  // namespace examples

MojoResult MojoMain(MojoHandle application_request) {
  examples::JankApp jank_app;
  return mojo::RunApplication(application_request, &jank_app);
}
