// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/examples/store.h"
#include "apps/modular/lib/document_editor/document_editor.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/module.fidl.h"
#include "apps/modular/services/story/story.fidl.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/map.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"

using fidl::InterfaceHandle;
using fidl::InterfacePtr;
using fidl::InterfaceRequest;

using modular::Link;
using modular::Module;
using modular::Store;
using modular::Story;
using modular::StrongBinding;
using modular::operator<<;

namespace {

constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr int kValueHandoffDuration = 1;

constexpr char kModuleName[] = "Module2Impl";

// Module implementation that acts as a leaf module. It implements Module.
// TODO(jimbe) Factor out the BaseView code into its own class.
class Module2Impl : public Module, public mozart::BaseView {
 public:
  explicit Module2Impl(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<Module> module_request,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "Module2Impl"),
        module_binding_(this, std::move(module_request)),
        store_(kModuleName),
        weak_ptr_factory_(this) {
    FTL_LOG(INFO) << kModuleName;

    store_.AddCallback([this] { IncrementCounterAction(); });
  }

  ~Module2Impl() override { FTL_LOG(INFO) << "~Module2Impl"; }

  void Initialize(InterfaceHandle<Story> story,
                  InterfaceHandle<Link> link) override {
    story_.Bind(std::move(story));
    store_.Initialize(std::move(link));
  }

  void Stop(const StopCallback& done) override {
    store_.Stop();
    story_.reset();
    done();
  }

 private:
  void IncrementCounterAction() {
    if (store_.counter.sender == kModuleName || store_.counter.counter > 11)
      return;

    // TODO(jimbe) Enabling animation should be done in its own function, but
    // it needs a trigger to know when to start.
    enable_animation_ = true;
    Invalidate();
    ftl::WeakPtr<Module2Impl> module_ptr = weak_ptr_factory_.GetWeakPtr();
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this, module_ptr]() {
          FTL_LOG(INFO) << "ControlAnimation() DONE";
          if (!module_ptr.get())
            return;

          enable_animation_ = false;
          store_.counter.sender = kModuleName;
          store_.counter.counter += 1;
          store_.MarkDirty();
          store_.ModelChanged();
        },
        ftl::TimeDelta::FromSeconds(kValueHandoffDuration));
  }

  // Copied from
  // https://fuchsia.googlesource.com/mozart/+/master/examples/spinning_square/spinning_square.cc
  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());
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
      DrawContent(surface->getCanvas(), size);
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
      update->nodes.insert(kRootNodeId, std::move(root_node));
    } else {
      auto root_node = mozart::Node::New();
      update->nodes.insert(kRootNodeId, std::move(root_node));
    }
    scene()->Update(std::move(update));
    scene()->Publish(CreateSceneMetadata());

    if (enable_animation_)
      Invalidate();
  }

  void DrawContent(SkCanvas* const canvas, const mozart::Size& size) {
    canvas->clear(SK_ColorBLUE);
    canvas->translate(size.width / 2, size.height / 2);
    canvas->rotate(SkIntToScalar(45 * store_.counter.counter));
    SkPaint paint;
    paint.setColor(0xFFFF00FF);
    paint.setAntiAlias(true);
    float d = std::min(size.width, size.height) / 4;
    canvas->drawRect(SkRect::MakeLTRB(-d, -d, d, d), paint);
    canvas->flush();
  }

  mozart::BufferProducer buffer_producer_;

  StrongBinding<Module> module_binding_;

  InterfacePtr<Story> story_;

  Store store_;

  bool enable_animation_ = false;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  ftl::WeakPtrFactory<Module2Impl> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Module2Impl);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::SingleServiceViewApp<Module, Module2Impl> app;
  loop.Run();
  return 0;
}
