// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/usages.h>
#include <math.h>
#include <mojo/system/main.h>

#include <algorithm>
#include <map>
#include <string>

#include "apps/mozart/lib/skia/skia_surface_holder.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/input_handler.h"
#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRect.h"

namespace examples {

namespace {
constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
}  // namespace

class PaintView : public mozart::BaseView, public mozart::InputListener {
 public:
  PaintView(mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
            mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(app_connector.Pass(), view_owner_request.Pass(), "Paint"),
        input_handler_(GetViewServiceProvider(), this) {}

  ~PaintView() override {}

 private:
  SkPath CurrentPath(uint32_t pointer_id) {
    SkPath path;
    uint32_t count = 0;
    for (auto point : points_.at(pointer_id)) {
      if (count++ == 0) {
        path.moveTo(point);
      } else {
        path.lineTo(point);
      }
    }
    return path;
  }

  // |InputListener|:
  void OnEvent(mozart::EventPtr event,
               const OnEventCallback& callback) override {
    bool handled = false;
    if (event->pointer_data) {
      uint32_t pointer_id = event->pointer_data->pointer_id;
      switch (event->action) {
        case mozart::EventType::POINTER_DOWN:
        case mozart::EventType::POINTER_MOVE:
          // On down + move, keep appending points to the path being built
          // For mouse only draw if left button is pressed
          if (event->pointer_data->kind == mozart::PointerKind::TOUCH ||
              (event->pointer_data->kind == mozart::PointerKind::MOUSE &&
               event->flags == mozart::EventFlags::LEFT_MOUSE_BUTTON)) {
            if (!points_.count(pointer_id)) {
              points_[pointer_id] = std::vector<SkPoint>();
            }
            points_.at(pointer_id)
                .push_back(SkPoint::Make(event->pointer_data->x,
                                         event->pointer_data->y));
          }
          handled = true;
          break;
        case mozart::EventType::POINTER_UP:
          // Path is done, add it to the list of paths and reset the list of
          // points
          paths_.push_back(CurrentPath(pointer_id));
          points_.erase(pointer_id);
          handled = true;
          break;
        default:
          break;
      }
    } else if (event->key_data) {
      if (event->key_data->hid_usage == HID_USAGE_KEY_ESC) {
        // clear
        paths_.clear();
        handled = true;
      }
    }

    callback.Run(handled);
    Invalidate();
  }

  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());

    auto update = mozart::SceneUpdate::New();

    const mojo::Size& size = *properties()->view_layout->size;
    if (size.width > 0 && size.height > 0) {
      mojo::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;

      mozart::SkiaSurfaceHolder surface_holder(size);
      DrawContent(surface_holder.surface()->getCanvas(), size);
      auto content_resource = mozart::Resource::New();
      content_resource->set_image(mozart::ImageResource::New());
      content_resource->get_image()->image = surface_holder.TakeImage();
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
  }

  void DrawContent(SkCanvas* canvas, const mojo::Size& size) {
    canvas->clear(SK_ColorWHITE);

    SkPaint paint;
    paint.setColor(0xFFFF00FF);
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(SkIntToScalar(3));

    for (auto path : paths_) {
      canvas->drawPath(path, paint);
    }

    paint.setColor(SK_ColorBLUE);
    for (auto iter = points_.begin(); iter != points_.end(); ++iter) {
      if (!iter->second.empty()) {
        canvas->drawPath(CurrentPath(iter->first), paint);
      }
    }

    canvas->flush();
  }

  mozart::InputHandler input_handler_;
  std::map<uint32_t, std::vector<SkPoint>> points_;
  std::vector<SkPath> paths_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PaintView);
};

class PaintApp : public mozart::ViewProviderApp {
 public:
  PaintApp() {}
  ~PaintApp() override {}

  void CreateView(
      const std::string& connection_url,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override {
    new PaintView(mojo::CreateApplicationConnector(shell()),
                  view_owner_request.Pass());
  }

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PaintApp);
};

}  // namespace examples

MojoResult MojoMain(MojoHandle application_request) {
  examples::PaintApp app;
  return mojo::RunApplication(application_request, &app);
}
