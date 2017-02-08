// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/usages.h>
#include <math.h>

#include <algorithm>
#include <map>
#include <string>

#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/input_handler.h"
#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
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
  PaintView(mozart::ViewManagerPtr view_manager,
            fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "Paint"),
        input_handler_(GetViewServiceProvider(), this) {}

  ~PaintView() override {}

 private:
  SkPath CurrentPath(uint32_t pointer_id) {
    SkPath path;
    if (points_.count(pointer_id)) {
      uint32_t count = 0;
      for (auto point : points_.at(pointer_id)) {
        if (count++ == 0) {
          path.moveTo(point);
        } else {
          path.lineTo(point);
        }
      }
    }
    return path;
  }

  // |InputListener|:
  void OnEvent(mozart::InputEventPtr event,
               const OnEventCallback& callback) override {
    bool handled = false;
    if (event->is_pointer()) {
      const mozart::PointerEventPtr& pointer = event->get_pointer();
      uint32_t pointer_id = pointer->device_id * 32 + pointer->pointer_id;
      switch (pointer->phase) {
        case mozart::PointerEvent::Phase::DOWN:
        case mozart::PointerEvent::Phase::MOVE:
          // On down + move, keep appending points to the path being built
          // For mouse only draw if left button is pressed
          if (pointer->type == mozart::PointerEvent::Type::TOUCH ||
              pointer->type == mozart::PointerEvent::Type::STYLUS ||
              (pointer->type == mozart::PointerEvent::Type::MOUSE &&
               pointer->buttons & mozart::kMousePrimaryButton)) {
            if (!points_.count(pointer_id)) {
              points_[pointer_id] = std::vector<SkPoint>();
            }
            points_.at(pointer_id)
                .push_back(SkPoint::Make(pointer->x, pointer->y));
          }
          handled = true;
          break;
        case mozart::PointerEvent::Phase::UP:
          // Path is done, add it to the list of paths and reset the list of
          // points
          paths_.push_back(CurrentPath(pointer_id));
          points_.erase(pointer_id);
          handled = true;
          break;
        default:
          break;
      }
    } else if (event->is_keyboard()) {
      const mozart::KeyboardEventPtr& keyboard = event->get_keyboard();
      if (keyboard->hid_usage == HID_USAGE_KEY_ESC) {
        // clear
        paths_.clear();
        handled = true;
      }
    }

    callback(handled);
    Invalidate();
  }

  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());

    auto update = mozart::SceneUpdate::New();

    const mozart::Size& size = *properties()->view_layout->size;
    if (size.width > 0 && size.height > 0) {
      mozart::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;

      // Draw the contents of the scene to a surface.
      mozart::ImagePtr image;
      sk_sp<SkSurface> surface =
          mozart::MakeSkSurface(size, &buffer_producer_, &image);
      FTL_CHECK(surface);
      DrawContent(surface->getCanvas(), size);

      // Update the scene contents.
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

    // Publish the updated scene contents.
    scene()->Update(std::move(update));
    scene()->Publish(CreateSceneMetadata());
    buffer_producer_.Tick();
  }

  void DrawContent(SkCanvas* canvas, const mozart::Size& size) {
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
  mozart::BufferProducer buffer_producer_;
  std::map<uint32_t, std::vector<SkPoint>> points_;
  std::vector<SkPath> paths_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PaintView);
};

}  // namespace examples

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  mozart::ViewProviderApp app([](mozart::ViewContext view_context) {
    return std::make_unique<examples::PaintView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request));
  });

  loop.Run();
  return 0;
}
