// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/document_editor/document_editor.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "apps/modular/services/story/story_runner.fidl.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/map.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"

namespace {

constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

// Subjects
constexpr char kDocId[] =
    "http://google.com/id/dc7cade7-7be0-4e23-924d-df67e15adae5";

// Property labels
constexpr char kCounterLabel[] = "http://schema.domokit.org/counter";
constexpr char kSenderLabel[] = "http://schema.org/sender";

using document_store::Document;
using document_store::Value;

using fidl::Array;
using fidl::InterfaceHandle;
using fidl::InterfacePtr;
using fidl::InterfaceRequest;
using fidl::Map;
using fidl::String;

using modular::DocumentEditor;
using modular::FidlDocMap;
using modular::Link;
using modular::LinkWatcher;
using modular::Module;
using modular::Story;
using modular::StrongBinding;
using modular::operator<<;

// Module implementation that acts as a leaf module. It implements
// both Module and the LinkWatcher observer of its own Link.
class Module2Impl : public Module, public LinkWatcher, public mozart::BaseView {
 public:
  explicit Module2Impl(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<Module> module_request,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "Module2Impl"),
        module_binding_(this, std::move(module_request)),
        watcher_binding_(this),
        tick_(0) {
    FTL_LOG(INFO) << "Module2Impl";
  }

  ~Module2Impl() override { FTL_LOG(INFO) << "~Module2Impl"; }

  void Initialize(InterfaceHandle<Story> story,
                  InterfaceHandle<Link> link) override {
    story_.Bind(std::move(story));
    link_.Bind(std::move(link));

    InterfaceHandle<LinkWatcher> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));
  }

  // Whenever the module sees a changed value, it increments it by 1
  // and writes it back. This works because the module is not notified
  // of changes from itself. More precisely, a watcher registered
  // through one link handle is not notified of changes requested
  // through the same handle. It's really the handle identity that
  // decides.
  void Notify(FidlDocMap docs) override {
    FTL_LOG(INFO) << "Module2Impl::Notify() " << (int64_t)this << docs;

    DocumentEditor editor;
    if (!editor.Edit(kDocId, &docs))
      return;

    Value* sender = editor.GetValue(kSenderLabel);
    Value* counter = editor.GetValue(kCounterLabel);

    FTL_DCHECK(sender != nullptr);
    FTL_DCHECK(counter != nullptr);

    sender->set_string_value("Module2Impl");

    int n = counter->get_int_value() + 1;
    counter->set_int_value(n);

    // For the last value, remove the sender property to prove that property
    // removal works.
    if (n == 11) {
      editor.RemoveProperty(kSenderLabel);
    }

    Invalidate();
    link_->SetAllDocuments(std::move(docs));
  }

 private:
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
      sk_sp<SkSurface> surface = mozart::MakeSkSurface(size, &image);
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
  }

  void DrawContent(SkCanvas* const canvas, const mozart::Size& size) {
    canvas->clear(SK_ColorBLUE);
    canvas->translate(size.width / 2, size.height / 2);
    canvas->rotate(SkIntToScalar(45 * (tick_++)));
    SkPaint paint;
    paint.setColor(0xFFFF00FF);
    paint.setAntiAlias(true);
    float d = std::min(size.width, size.height) / 4;
    canvas->drawRect(SkRect::MakeLTRB(-d, -d, d, d), paint);
    canvas->flush();
  }

  StrongBinding<Module> module_binding_;
  StrongBinding<LinkWatcher> watcher_binding_;

  InterfacePtr<Story> story_;
  InterfacePtr<Link> link_;

  int tick_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Module2Impl);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::SingleServiceViewApp<modular::Module, Module2Impl> app;
  loop.Run();
  return 0;
}
