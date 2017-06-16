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
#ifdef MOZART_EXAMPLES_USE_SCENE_MANAGER
#include "apps/mozart/examples/paint/session_manager_includes.h"
#endif  // MOZART_EXAMPLES_USE_SCENE_MANAGER

namespace examples {

namespace {
constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
}  // namespace

class PaintView : public mozart::BaseView, public mozart::InputListener {
 public:
  PaintView(app::ApplicationContext* application_context,
            mozart::ViewManagerPtr view_manager,
            fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "Paint"),
        input_handler_(GetViewServiceProvider(), this) {
#ifdef MOZART_EXAMPLES_USE_SCENE_MANAGER
    ConnectToSceneManager(application_context);
    InitializeSceneManagerSession();
#endif
  }

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

#ifdef MOZART_EXAMPLES_USE_SCENE_MANAGER
      UpdateSceneManagerSession(image, size);
#endif

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

#ifdef MOZART_EXAMPLES_USE_SCENE_MANAGER
  void ConnectToSceneManager(app::ApplicationContext* application_context) {
    // Launch SceneManager.
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "file://system/apps/hello_scene_manager_service";
    launch_info->services = services_.NewRequest();
    application_context->launcher()->CreateApplication(
        std::move(launch_info), controller_.NewRequest());
    controller_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Hello SceneManager service terminated.";
      mtl::MessageLoop::GetCurrent()->QuitNow();
    });

    // Connect to the SceneManager service.
    app::ConnectToService(services_.get(), scene_manager_.NewRequest());
  }

  void InitializeSceneManagerSession() {
    // TODO: set up SessionListener.
    scene_manager_->CreateSession(session_.NewRequest(), nullptr);

    session_->Enqueue(PopulateSceneManagerSession());

    session_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Session terminated.";
      mtl::MessageLoop::GetCurrent()->QuitNow();
    });
  }

  fidl::Array<mozart2::OpPtr> PopulateSceneManagerSession() {
    auto ops = fidl::Array<mozart2::OpPtr>::New(0);

    // Create a Link to attach ourselves to.
    mx::eventpair link_handle1;
    mx::eventpair link_handle2;
    mx_status_t status = mx::eventpair::create(0, &link_handle1, &link_handle2);
    if (status != MX_OK) {
      FTL_LOG(ERROR)
          << "PopulateSceneManagerSession: Creating eventpair failed.";
      mtl::MessageLoop::GetCurrent()->QuitNow();
      return nullptr;
    }
    mozart::ResourceId link_id = NewResourceId();
    ops.push_back(mozart::NewCreateLinkOp(link_id, std::move(link_handle1)));

    // Create a shape node.
    mozart::ResourceId node_id = NewResourceId();
    node_id_ = node_id;
    ops.push_back(mozart::NewCreateShapeNodeOp(node_id));

    // Make the shape a circle.
    mozart::ResourceId shape_id = NewResourceId();
    ops.push_back(mozart::NewCreateCircleOp(shape_id, 500.f));

    ops.push_back(mozart::NewSetShapeOp(node_id, shape_id));

    // Translate the circle.
    const float kScreenWidth = 2160.f;
    const float kScreenHeight = 1440.f;
    float translation[3] = {kScreenWidth / 2, kScreenHeight / 2, 10.f};
    ops.push_back(mozart::NewSetTransformOp(
        node_id, translation,
        mozart::kOnesFloat3,        // scale
        mozart::kZeroesFloat3,      // anchor point
        mozart::kQuaternionDefault  // rotation
    ));
    // Attach the circle to the Link.
    ops.push_back(mozart::NewAddChildOp(link_id, node_id));

    return ops;
  }

  void UpdateSceneManagerSession(const mozart::ImagePtr& image,
                                 const mozart::Size& size) {
    auto ops = fidl::Array<mozart2::OpPtr>::New(0);

    // Destroy the resources from the previous frame. Generally this should
    // not be done and resources should be recycled whenever possible. However,
    // in our case we wanted to preserve the use of BufferProducer for legacy
    // purposes and this made recycling Resource objects impractical.
    if (previous_memory_id_ != 0) {
      ops.push_back(mozart::NewReleaseResourceOp(previous_memory_id_));
    }
    if (previous_image_id_ != 0) {
      ops.push_back(mozart::NewReleaseResourceOp(previous_image_id_));
    }
    if (previous_material_id_ != 0) {
      ops.push_back(mozart::NewReleaseResourceOp(previous_material_id_));
    }
    mozart::ResourceId memory_id = NewResourceId();

    mx::vmo vmo_copy;
    auto status = image->buffer->vmo.duplicate(MX_RIGHT_SAME_RIGHTS, &vmo_copy);
    if (status) {
      FTL_LOG(ERROR) << "Failed to duplicate vmo handle.";
      return;
    }

    // Update Scene Manager with the image. In our case this requires creating
    // a new Memory object, Image object, and Material object. In general,
    // however, it would be best to create these upfront and then cycle through
    // them.
    ops.push_back(
        mozart::NewCreateMemoryOp(memory_id, std::move(vmo_copy),
                                  mozart2::MemoryType::HOST_MEMORY));

    mozart::ResourceId image_id = NewResourceId();
    ops.push_back(mozart::NewCreateImageOp(
        image_id, memory_id, 0, mozart2::ImageInfo::PixelFormat::BGRA_8,
        mozart2::ImageInfo::Tiling::LINEAR, size.width, size.height,
        size.width));

    mozart::ResourceId material_id = NewResourceId();
    ops.push_back(
        mozart::NewCreateMaterialOp(material_id, image_id, 255, 100, 100, 255));
    previous_memory_id_ = memory_id;
    previous_image_id_ = image_id;
    previous_material_id_ = material_id;

    ops.push_back(mozart::NewSetMaterialOp(node_id_, material_id));

    session_->Enqueue(std::move(ops));
    session_->Present(fidl::Array<mx::event>::New(0),
                      fidl::Array<mx::event>::New(0));
  }

  mozart::ResourceId NewResourceId() { return ++resource_id_counter_; }

#endif  // MOZART_EXAMPLES_USE_SCENE_MANAGER

  mozart::InputHandler input_handler_;
  mozart::BufferProducer buffer_producer_;
  std::map<uint32_t, std::vector<SkPoint>> points_;
  std::vector<SkPath> paths_;

#ifdef MOZART_EXAMPLES_USE_SCENE_MANAGER
  // State related to the SceneManager session.
  app::ApplicationControllerPtr controller_;
  mozart2::SessionPtr session_;
  app::ServiceProviderPtr services_;
  mozart2::SceneManagerPtr scene_manager_;

  // The ID of the circle we are texturing
  mozart::ResourceId node_id_;

  mozart::ResourceId previous_memory_id_ = 0;
  mozart::ResourceId previous_image_id_ = 0;
  mozart::ResourceId previous_material_id_ = 0;
  mozart::ResourceId resource_id_counter_ = 0;
#endif  // MOZART_EXAMPLES_USE_SCENE_MANAGER

  FTL_DISALLOW_COPY_AND_ASSIGN(PaintView);
};

}  // namespace examples

class PaintViewProviderApp {
 public:
  explicit PaintViewProviderApp(app::ApplicationContext* application_context,
                                mozart::ViewFactory factory)
      : service_(application_context, std::move(factory)) {}
  ~PaintViewProviderApp() {}

 private:
  mozart::ViewProviderService service_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PaintViewProviderApp);
};

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  auto application_context = app::ApplicationContext::CreateFromStartupInfo();

  PaintViewProviderApp app(
      application_context.get(), [app_context = application_context.get()](
                                     mozart::ViewContext view_context) {
        return std::make_unique<examples::PaintView>(
            app_context, std::move(view_context.view_manager),
            std::move(view_context.view_owner_request));
      });

  loop.Run();
  return 0;
}
