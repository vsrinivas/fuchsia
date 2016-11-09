// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include "apps/modular/services/document/document.fidl.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
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
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"

namespace {

constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr int kTickRotationDegrees = 45;
constexpr int kValueHandoffDuration = 3;

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
using fidl::StructPtr;

using modular::DocumentEditor;
using modular::Link;
using modular::LinkChanged;
using modular::Module;
using modular::MojoDocMap;
using modular::Story;
using modular::StrongBinding;
using modular::operator<<;

// Module implementation that acts as a leaf module. It implements
// both Module and the LinkChanged observer of its own Link.
class Module1Impl : public mozart::BaseView, public Module, public LinkChanged {
 public:
  explicit Module1Impl(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<Module> module_request,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "Module1Impl"),
        module_binding_(this, std::move(module_request)),
        watcher_binding_(this),
        tick_(0) {
    FTL_LOG(INFO) << "Module1Impl";
  }

  ~Module1Impl() override { FTL_LOG(INFO) << "~Module1Impl"; }

  void Initialize(InterfaceHandle<Story> story,
                  InterfaceHandle<Link> link) override {
    story_.Bind(std::move(story));
    link_.Bind(std::move(link));

    InterfaceHandle<LinkChanged> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));
  }

  // See comments on Module2Impl in example-module2.cc.
  void Notify(MojoDocMap docs) override {
    FTL_LOG(INFO) << "Module1Impl::Notify() " << (int64_t)this << docs;
    docs_ = std::move(docs);

    tick_++;
    if (UpdateCounter()) {
      handoff_time_ = ftl::TimePoint::Now() +
                      ftl::TimeDelta::FromSeconds(kValueHandoffDuration);
      Invalidate();
    }
  }

 private:
  bool UpdateCounter() {
    DocumentEditor editor;
    if (!editor.Edit(kDocId, &docs_))
      return false;

    Value* sender = editor.GetValue(kSenderLabel);
    Value* value = editor.GetValue(kCounterLabel);
    FTL_DCHECK(value != nullptr);

    int counter = value->get_int_value();
    value->set_int_value(counter + 1);

    bool updated = counter <= 10;
    if (updated) {
      sender->set_string_value("Module1Impl");
      link_->SetAllDocuments(docs_.Clone());
    } else {
      // For the last iteration, test that Module2 removes the sender.
      FTL_DCHECK(sender == nullptr);
      story_->Done();
    }

    return updated;
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

    if (ftl::TimePoint::Now() >= handoff_time_) {
      UpdateCounter();
    } else {
      Invalidate();
    }
  }

  void DrawContent(SkCanvas* const canvas, const mozart::Size& size) {
    canvas->clear(SK_ColorBLUE);
    canvas->translate(size.width / 2, size.height / 2);
    canvas->rotate(SkIntToScalar(kTickRotationDegrees * tick_));
    SkPaint paint;
    paint.setColor(SK_ColorGREEN);
    paint.setAntiAlias(true);
    float d = std::min(size.width, size.height) / 4;
    canvas->drawRect(SkRect::MakeLTRB(-d, -d, d, d), paint);
    canvas->flush();
  }

  StrongBinding<Module> module_binding_;
  StrongBinding<LinkChanged> watcher_binding_;

  InterfacePtr<Story> story_;
  InterfacePtr<Link> link_;

  // Used by |OnDraw()| to decide whether enough time has passed, so that the
  // value can be sent back and a new frame drawn.
  ftl::TimePoint handoff_time_;
  MojoDocMap docs_;

  // This is a counter that is incremented when a new value is received and used
  // to rotate a square.
  int tick_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Module1Impl);
};
}
// namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::SingleServiceViewApp<modular::Module, Module1Impl> app;
  loop.Run();
  return 0;
}
