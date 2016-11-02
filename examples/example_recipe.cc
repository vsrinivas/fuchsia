// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Module that serves as the recipe in the example story, i.e. that
// creates other Modules in the session.

#include <mojo/system/main.h>

#include "apps/document_store/interfaces/document.mojom.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/mojo/single_service_view_app.h"
#include "apps/modular/services/story/story_runner.mojom.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/interfaces/view_token.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/map.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/services/geometry/cpp/geometry_util.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"

namespace {

constexpr uint32_t kViewResourceIdBase = 100;
constexpr uint32_t kViewResourceIdSpacing = 100;

constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr uint32_t kViewNodeIdBase = 100;
constexpr uint32_t kViewNodeIdSpacing = 100;
constexpr uint32_t kViewSceneNodeIdOffset = 1;

// Subjects
constexpr char kDocId[] =
    "http://google.com/id/dc7cade7-7be0-4e23-924d-df67e15adae5";

// Property labels
constexpr char kCounterLabel[] = "http://schema.domokit.org/counter";
constexpr char kSenderLabel[] = "http://schema.org/sender";
constexpr char kIsALabel[] = "isA";

// Predefined Values
constexpr char kIsAValue[] = "http://schema.domokit.org/PingPongPacket";

using document_store::Document;
using document_store::DocumentPtr;

using mojo::ApplicationImplBase;
using mojo::Array;
using mojo::Binding;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::Shell;
using mojo::StrongBinding;
using mojo::StructPtr;

using modular::DocumentEditor;
using modular::Link;
using modular::LinkChanged;
using modular::Module;
using modular::ModuleController;
using modular::ModuleWatcher;
using modular::MojoDocMap;
using modular::Session;
using modular::operator<<;

// Implementation of the LinkChanged service that forwards each document
// changed in one Link instance to a second Link instance.
class LinkConnection : public LinkChanged {
 public:
  LinkConnection(InterfacePtr<Link>& src, InterfacePtr<Link>& dst)
      : src_binding_(this), src_(src), dst_(dst) {
    InterfaceHandle<LinkChanged> watcher;
    src_binding_.Bind(GetProxy(&watcher));
    src_->Watch(std::move(watcher));
  }

  void Notify(MojoDocMap docs) override {
    FTL_LOG(INFO) << "LinkConnection::Notify()" << docs;
    if (docs.size() > 0) {
      dst_->SetAllDocuments(std::move(docs));
    }
  }

 private:
  Binding<LinkChanged> src_binding_;
  InterfacePtr<Link>& src_;
  InterfacePtr<Link>& dst_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkConnection);
};

// Implementation of the LinkChanged service that just reports every
// document changed in the given Link.
class LinkMonitor : public LinkChanged {
 public:
  LinkMonitor(const std::string tag, InterfacePtr<Link>& link)
      : binding_(this), tag_(tag) {
    InterfaceHandle<LinkChanged> watcher;
    binding_.Bind(GetProxy(&watcher));
    link->WatchAll(std::move(watcher));
  }

  void Notify(MojoDocMap docs) override {
    FTL_LOG(INFO) << "LinkMonitor::Notify()";
  }

 private:
  Binding<LinkChanged> binding_;
  const std::string tag_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkMonitor);
};

class ModuleMonitor : public ModuleWatcher {
 public:
  ModuleMonitor(InterfacePtr<ModuleController>& module_client,
                InterfacePtr<Session>& session)
      : binding_(this), session_(session) {
    InterfaceHandle<ModuleWatcher> watcher;
    binding_.Bind(GetProxy(&watcher));
    module_client->Watch(std::move(watcher));
  }

  void Done() override { session_->Done(); }

 private:
  Binding<ModuleWatcher> binding_;
  InterfacePtr<Session>& session_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(ModuleMonitor);
};

struct ViewData {
  explicit ViewData(uint32_t key) : key(key) {}
  const uint32_t key;
  StructPtr<mozart::ViewInfo> view_info;
  StructPtr<mozart::ViewProperties> view_properties;
  mojo::RectF layout_bounds;
  uint32_t scene_version = 1u;
};

// Module implementation that acts as a recipe.
class RecipeImpl : public Module, public mozart::BaseView {
 public:
  RecipeImpl(mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
             InterfaceRequest<Module> module_request,
             mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(app_connector),
                 std::move(view_owner_request),
                 "RecipeImpl"),
        module_binding_(this, std::move(module_request)) {
    FTL_LOG(INFO) << "RecipeImpl";
  }
  ~RecipeImpl() override { FTL_LOG(INFO) << "~RecipeImpl"; }

  void Initialize(InterfaceHandle<Session> session,
                  InterfaceHandle<Link> link) override {
    FTL_LOG(INFO) << "RecipeImpl::Initialize()";

    // TODO(mesch): Good illustration of the remaining issue to
    // restart a session: How does this code look like when the
    // Session is not new, but already contains existing Modules and
    // Links from the previous execution that is continued here?

    session_.Bind(std::move(session));
    link_.Bind(std::move(link));

    session_->CreateLink("module1", GetProxy(&module1_link_));
    session_->CreateLink("module2", GetProxy(&module2_link_));

    InterfaceHandle<Link> module1_link_handle;  // To pass to StartModule().
    module1_link_->Dup(GetProxy(&module1_link_handle));

    InterfaceHandle<Link> module2_link_handle;  // To pass to StartModule().
    module2_link_->Dup(GetProxy(&module2_link_handle));

    FTL_LOG(INFO) << "recipe start module module1";
    InterfaceHandle<mozart::ViewOwner> module1_view;
    session_->StartModule("mojo:example_module1",
                          std::move(module1_link_handle), GetProxy(&module1_),
                          GetProxy(&module1_view));
    GetViewContainer()->AddChild(0, std::move(module1_view));
    views_.emplace(
        std::make_pair(0, std::unique_ptr<ViewData>(new ViewData(0))));

    FTL_LOG(INFO) << "recipe start module module2";
    InterfaceHandle<mozart::ViewOwner> module2_view;
    session_->StartModule("mojo:example_module2",
                          std::move(module2_link_handle), GetProxy(&module2_),
                          GetProxy(&module2_view));
    GetViewContainer()->AddChild(1, std::move(module2_view));
    views_.emplace(
        std::make_pair(1, std::unique_ptr<ViewData>(new ViewData(1))));

    monitors_.emplace_back(new LinkMonitor("module1", module1_link_));
    monitors_.emplace_back(new LinkMonitor("module2", module2_link_));

    connections_.emplace_back(new LinkConnection(module1_link_, module2_link_));
    connections_.emplace_back(new LinkConnection(module2_link_, module1_link_));

    module_monitors_.emplace_back(new ModuleMonitor(module1_, session_));
    module_monitors_.emplace_back(new ModuleMonitor(module2_, session_));

    // This must come last, otherwise we get a notification of our own
    // write because of the "send initial values" code.
    DocumentEditor editor(kDocId);
    editor.SetProperty(kIsALabel, DocumentEditor::NewIriValue(kIsAValue));
    editor.SetProperty(kCounterLabel, DocumentEditor::NewIntValue(1));
    editor.SetProperty(kSenderLabel,
                       DocumentEditor::NewStringValue("RecipeImpl"));

    MojoDocMap docs;
    editor.TakeDocument(&docs[editor.docid()]);
    module1_link_->SetAllDocuments(std::move(docs));
  }

 private:
  // |BaseView| implementation copied from
  // https://github.com/fuchsia-mirror/mozart/blob/master/examples/tile/tile_view.cc
  // |BaseView|:
  void OnChildAttached(uint32_t child_key,
                       StructPtr<mozart::ViewInfo> child_view_info) override {
    auto it = views_.find(child_key);
    FTL_DCHECK(it != views_.end()) << "Invalid child_key.";
    auto view_data = it->second.get();
    view_data->view_info = std::move(child_view_info);
    Invalidate();
  }

  // |BaseView|:
  void OnChildUnavailable(uint32_t child_key) override {
    auto it = views_.find(child_key);
    FTL_DCHECK(it != views_.end()) << "Invalid child_key.";
    FTL_LOG(ERROR) << "View died unexpectedly: child_key=" << child_key;
    std::unique_ptr<ViewData> view_data = std::move(it->second);
    views_.erase(it);
    GetViewContainer()->RemoveChild(child_key, nullptr);
    Invalidate();
  }

  // |BaseView|:
  void OnLayout() override {
    FTL_DCHECK(properties());
    // Layout all children in a row.
    if (!views_.empty()) {
      const mojo::Size& size = *properties()->view_layout->size;

      uint32_t index = 0;
      uint32_t space = size.width;
      uint32_t base = space / views_.size();
      uint32_t excess = space % views_.size();
      uint32_t offset = 0;
      for (auto it = views_.begin(); it != views_.end(); ++it, ++index) {
        auto view_data = it->second.get();

        // Distribute any excess width among the leading children.
        uint32_t extent = base;
        if (excess) {
          extent++;
          excess--;
        }

        view_data->layout_bounds.x = offset;
        view_data->layout_bounds.y = 0;
        view_data->layout_bounds.width = extent;
        view_data->layout_bounds.height = size.height;
        offset += extent;

        auto view_properties = mozart::ViewProperties::New();
        view_properties->view_layout = mozart::ViewLayout::New();
        view_properties->view_layout->size = mojo::Size::New();
        view_properties->view_layout->size->width =
            view_data->layout_bounds.width;
        view_properties->view_layout->size->height =
            view_data->layout_bounds.height;

        if (view_data->view_properties.Equals(view_properties))
          continue;  // no layout work to do

        view_data->view_properties = view_properties.Clone();
        view_data->scene_version++;
        GetViewContainer()->SetChildProperties(
            it->first, view_data->scene_version, view_properties.Pass());
      }
    }
  }

  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());

    // Update the scene.
    // TODO: only send the resources once, be more incremental
    auto update = mozart::SceneUpdate::New();
    update->clear_resources = true;
    update->clear_nodes = true;

    // Create the root node.
    auto root_node = mozart::Node::New();

    // Add the children.
    for (auto it = views_.cbegin(); it != views_.cend(); it++) {
      const ViewData& view_data = *(it->second.get());
      const uint32_t scene_resource_id =
          kViewResourceIdBase + view_data.key * kViewResourceIdSpacing;
      const uint32_t container_node_id =
          kViewNodeIdBase + view_data.key * kViewNodeIdSpacing;

      mojo::RectF extent;
      extent.width = view_data.layout_bounds.width;
      extent.height = view_data.layout_bounds.height;

      // Create a container to represent the place where the child view
      // will be presented.  The children of the container provide
      // fallback behavior in case the view is not available.
      auto container_node = mozart::Node::New();
      container_node->content_clip = extent.Clone();
      container_node->content_transform = mojo::Transform::New();
      SetTranslationTransform(container_node->content_transform.get(),
                              view_data.layout_bounds.x,
                              view_data.layout_bounds.y, 0.f);

      // If we have the view, add it to the scene.
      if (view_data.view_info) {
        auto scene_resource = mozart::Resource::New();
        scene_resource->set_scene(mozart::SceneResource::New());
        scene_resource->get_scene()->scene_token =
            view_data.view_info->scene_token.Clone();
        update->resources.insert(scene_resource_id, scene_resource.Pass());

        const uint32_t scene_node_id =
            container_node_id + kViewSceneNodeIdOffset;
        auto scene_node = mozart::Node::New();
        scene_node->op = mozart::NodeOp::New();
        scene_node->op->set_scene(mozart::SceneNodeOp::New());
        scene_node->op->get_scene()->scene_resource_id = scene_resource_id;
        update->nodes.insert(scene_node_id, scene_node.Pass());
        container_node->child_node_ids.push_back(scene_node_id);
      }

      // Add the container.
      update->nodes.insert(container_node_id, container_node.Pass());
      root_node->child_node_ids.push_back(container_node_id);
    }

    // Add the root node.
    update->nodes.insert(kRootNodeId, root_node.Pass());
    scene()->Update(update.Pass());

    // Publish the scene.
    scene()->Publish(CreateSceneMetadata());
  }

  StrongBinding<Module> module_binding_;

  InterfacePtr<Link> link_;
  InterfacePtr<Session> session_;

  InterfacePtr<ModuleController> module1_;
  InterfacePtr<Link> module1_link_;

  InterfacePtr<ModuleController> module2_;
  InterfacePtr<Link> module2_link_;

  std::vector<std::unique_ptr<LinkConnection>> connections_;
  std::vector<std::unique_ptr<LinkMonitor>> monitors_;
  std::vector<std::unique_ptr<ModuleMonitor>> module_monitors_;

  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(RecipeImpl);
};

}  // namespace

MojoResult MojoMain(MojoHandle application_request) {
  modular::SingleServiceViewApp<modular::Module, RecipeImpl> app;
  return mojo::RunApplication(application_request, &app);
}
