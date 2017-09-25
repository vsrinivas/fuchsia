// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/view_host.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"

namespace modular {

struct ViewHost::ViewData {
  explicit ViewData(scenic_lib::Session* session) : host_node(session) {}

  scenic_lib::EntityNode host_node;
};

ViewHost::ViewHost(mozart::ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "ViewHost"),
      container_node_(session()) {
  parent_node().AddChild(container_node_);
}

ViewHost::~ViewHost() = default;

void ViewHost::ConnectView(
    fidl::InterfaceHandle<mozart::ViewOwner> view_owner) {
  const uint32_t child_key = next_child_key_++;

  auto view_data = std::make_unique<ViewData>(session());

  zx::eventpair host_import_token;
  view_data->host_node.ExportAsRequest(&host_import_token);
  container_node_.AddChild(view_data->host_node);
  views_.emplace(child_key, std::move(view_data));

  GetViewContainer()->AddChild(child_key, std::move(view_owner),
                               std::move(host_import_token));
  UpdateScene();
}

void ViewHost::OnPropertiesChanged(
    mozart::ViewPropertiesPtr /*old_properties*/) {
  UpdateScene();
}

void ViewHost::OnChildUnavailable(uint32_t child_key) {
  FXL_LOG(ERROR) << "View died unexpectedly: child_key=" << child_key;

  auto it = views_.find(child_key);
  FXL_DCHECK(it != views_.end());

  it->second->host_node.Detach();
  views_.erase(it);

  GetViewContainer()->RemoveChild(child_key, nullptr);
  UpdateScene();
}

void ViewHost::UpdateScene() {
  if (!properties() || views_.empty()) {
    return;
  }

  // Layout all children in a row.
  uint32_t index = 0;
  uint32_t space = logical_size().width;
  uint32_t base = space / views_.size();
  uint32_t excess = space % views_.size();
  uint32_t offset = 0;
  for (auto it = views_.begin(); it != views_.end(); ++it, ++index) {
    ViewData* view_data = it->second.get();

    // Distribute any excess width among the leading children.
    uint32_t extent = base;
    if (excess) {
      extent++;
      excess--;
    }

    mozart::RectF layout_bounds;
    layout_bounds.x = offset;
    layout_bounds.y = 0;
    layout_bounds.width = extent;
    layout_bounds.height = logical_size().height;
    offset += extent;

    auto view_properties = mozart::ViewProperties::New();
    view_properties->view_layout = mozart::ViewLayout::New();
    view_properties->view_layout->size = mozart::SizeF::New();
    view_properties->view_layout->size->width = layout_bounds.width;
    view_properties->view_layout->size->height = layout_bounds.height;
    view_properties->view_layout->inset = mozart::InsetF::New();
    GetViewContainer()->SetChildProperties(it->first,
                                           std::move(view_properties));

    view_data->host_node.SetTranslation(layout_bounds.x, layout_bounds.y, 0u);
  }

  session()->Present(0, [](scenic::PresentationInfoPtr info) {});
}

}  // namespace modular
