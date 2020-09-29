// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/tile/tile_view.h"

#include <fuchsia/math/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <fs/service.h>

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/ui/base_view/embedded_view_utils.h"

namespace examples {

TileView::TileView(scenic::ViewContext context, TileParams params)
    : BaseView(std::move(context), "Tile"),
      vfs_(async_get_default_dispatcher()),
      services_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      params_(std::move(params)),
      container_node_(session()) {
  root_node().AddChild(container_node_);
  CreateNestedEnvironment();
  ConnectViews();
}

void TileView::PresentView(fuchsia::ui::views::ViewHolderToken view_holder_token,
                           fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation) {
  AddChildView("tile_view child(Presented view)", std::move(view_holder_token), nullptr);
}

// |fuchsia::ui::policy::Presenter|
void TileView::PresentOrReplaceView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) {
  FX_LOGS(WARNING)
      << "PresentOrReplaceView not fully supported by TileView. Using PresentView instead.";
  PresentView(std::move(view_holder_token), std::move(presentation_request));
}

void TileView::ConnectViews() {
  for (const auto& url : params_.view_urls) {
    fuchsia::sys::ComponentControllerPtr controller;

    fuchsia::sys::LaunchInfo launch_info;

    std::shared_ptr<sys::ServiceDirectory> services =
        sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

    // Pass arguments to children, if there are any.
    std::vector<std::string> split_url =
        fxl::SplitStringCopy(url, " ", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
    FX_CHECK(split_url.size() >= 1);
    launch_info.url = split_url[0];

    if (split_url.size() > 1) {
      launch_info.arguments.emplace();
      for (auto it = split_url.begin() + 1; it != split_url.end(); it++) {
        launch_info.arguments->push_back(*it);
      }
    }

    // |env_launcher_| launches the component with our nested environment.
    env_launcher_->CreateComponent(std::move(launch_info), controller.NewRequest());

    // Create a View from the launched component.
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    auto view_provider = services->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);

    // Add the view.
    AddChildView("tile_view child(" + split_url[0] + ")", std::move(view_holder_token),
                 std::move(controller));
  }
}

zx::channel TileView::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_.ServeDirectory(services_dir_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

void TileView::CreateNestedEnvironment() {
  // Add a binding for the presenter service
  auto service = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
    presenter_bindings_.AddBinding(
        this, fidl::InterfaceRequest<fuchsia::ui::policy::Presenter>(std::move(channel)));
    return ZX_OK;
  }));
  services_dir_->AddEntry(fuchsia::ui::policy::Presenter::Name_, service);

  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names.push_back(fuchsia::ui::policy::Presenter::Name_);
  service_list->host_directory = OpenAsDirectory();

  fuchsia::sys::EnvironmentPtr environment;
  component_context()->svc()->Connect(environment.NewRequest());
  environment->CreateNestedEnvironment(env_.NewRequest(), env_controller_.NewRequest(), "tile",
                                       std::move(service_list), {.inherit_parent_services = true});
  env_->GetLauncher(env_launcher_.NewRequest());
}

void TileView::OnChildAttached(uint32_t view_holder_id) {
  auto it = views_.find(view_holder_id);
  FX_DCHECK(it != views_.end());
}

void TileView::OnChildUnavailable(uint32_t view_holder_id) {
  FX_LOGS(ERROR) << "View died unexpectedly: view_holder_id=" << view_holder_id;
  RemoveChildView(view_holder_id);
}

void TileView::OnScenicEvent(fuchsia::ui::scenic::Event event) {
  switch (event.Which()) {
    case ::fuchsia::ui::scenic::Event::Tag::kGfx:
      switch (event.gfx().Which()) {
        case ::fuchsia::ui::gfx::Event::Tag::kViewConnected: {
          auto& evt = event.gfx().view_connected();
          OnChildAttached(evt.view_holder_id);
          break;
        }
        case ::fuchsia::ui::gfx::Event::Tag::kViewDisconnected: {
          auto& evt = event.gfx().view_disconnected();
          OnChildUnavailable(evt.view_holder_id);
          break;
        }
        default:
          break;
      }
    default:
      break;
  }
}

void TileView::AddChildView(std::string label,
                            fuchsia::ui::views::ViewHolderToken view_holder_token,
                            fuchsia::sys::ComponentControllerPtr controller) {
  auto view_data = std::make_unique<ViewData>(label, std::move(view_holder_token),
                                              std::move(controller), session());

  container_node_.AddChild(view_data->host_node);

  view_data->host_node.AddChild(view_data->clip_shape_node);
  view_data->host_node.SetClip(0, true);
  view_data->host_node.Attach(view_data->view_holder);

  views_.emplace(view_data->view_holder.id(), std::move(view_data));

  InvalidateScene();
}

void TileView::RemoveChildView(uint32_t view_holder_id) {
  auto it = views_.find(view_holder_id);
  FX_DCHECK(it != views_.end());

  it->second->host_node.Detach();
  views_.erase(it);

  InvalidateScene();
}

void TileView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size() || views_.empty())
    return;

  // Layout all children in a row.
  const bool vertical = (params_.orientation_mode == TileParams::OrientationMode::kVertical);

  uint32_t index = 0;
  const uint32_t space =
      vertical ? static_cast<uint32_t>(logical_size().y) : static_cast<uint32_t>(logical_size().x);
  const uint32_t base = space / static_cast<uint32_t>(views_.size());
  uint32_t excess = space % static_cast<uint32_t>(views_.size());
  float offset = 0;
  for (auto it = views_.begin(); it != views_.end(); ++it, ++index) {
    ViewData* view_data = it->second.get();

    // Distribute any excess width among the leading children.
    float extent = static_cast<float>(base);
    if (excess) {
      extent++;
      excess--;
    }

    fuchsia::math::RectF layout_bounds;
    if (vertical) {
      layout_bounds.x = 0;
      layout_bounds.y = offset;
      layout_bounds.width = logical_size().x;
      layout_bounds.height = extent;
    } else {
      layout_bounds.x = offset;
      layout_bounds.y = 0;
      layout_bounds.width = extent;
      layout_bounds.height = logical_size().y;
    }
    offset += extent;

    if (view_data->width != layout_bounds.width || view_data->height != layout_bounds.height) {
      view_data->width = layout_bounds.width;
      view_data->height = layout_bounds.height;
      view_data->view_holder.SetViewProperties(0, 0, 0, view_data->width, view_data->height, 1000.f,
                                               0, 0, 0, 0, 0, 0);
    }

    view_data->host_node.SetTranslation(layout_bounds.x, layout_bounds.y, 0u);

    // Clip
    scenic::Rectangle shape(session(),            // session
                            layout_bounds.width,  // width
                            layout_bounds.height  // height
    );
    view_data->clip_shape_node.SetShape(shape);
    view_data->clip_shape_node.SetTranslation(layout_bounds.width * 0.5f,
                                              layout_bounds.height * 0.5f, 0.f);
  }
}

TileView::ViewData::ViewData(std::string label,
                             fuchsia::ui::views::ViewHolderToken view_holder_token,
                             fuchsia::sys::ComponentControllerPtr controller,
                             scenic::Session* session)
    : controller(std::move(controller)),
      host_node(session),
      clip_shape_node(session),
      view_holder(session, std::move(view_holder_token), label) {}

}  // namespace examples
