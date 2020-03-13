// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/fidl/view_host.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "src/lib/syslog/cpp/logger.h"

namespace modular {

ViewHost::ViewHost(scenic::ViewContext view_context)
    : BaseView(std::move(view_context), "ViewHost") {}

void ViewHost::ConnectView(fuchsia::ui::views::ViewHolderToken view_holder_token) {
  auto view_data = std::make_unique<ViewData>(session(), std::move(view_holder_token));

  root_node().AddChild(view_data->host_node);
  views_.emplace(view_data->host_view_holder.id(), std::move(view_data));

  UpdateScene();
  InvalidateScene();
}

void ViewHost::OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties /*old_properties*/) {
  UpdateScene();
}

void ViewHost::OnScenicEvent(fuchsia::ui::scenic::Event event) {
  switch (event.Which()) {
    case fuchsia::ui::scenic::Event::Tag::kGfx:
      switch (event.gfx().Which()) {
        case fuchsia::ui::gfx::Event::Tag::kViewDisconnected: {
          uint32_t view_holder_id = event.gfx().view_disconnected().view_holder_id;
          FX_LOGS(ERROR) << "View died unexpectedly, id=" << view_holder_id;

          auto it = views_.find(view_holder_id);
          FX_DCHECK(it != views_.end());
          it->second->host_node.Detach();
          views_.erase(it);

          UpdateScene();
          InvalidateScene();
          break;
        }
        default:
          break;
      }
      break;
    default:
      FX_DCHECK(false) << "ViewHost::OnScenicEvent: Got an unhandled Scenic "
                          "event.";
      break;
  }
}

void ViewHost::UpdateScene() {
  if (views_.empty() || !has_logical_size()) {
    return;
  }

  // Layout all children in a row.
  const float width = logical_size().x / views_.size();
  float offset = 0.f;
  for (auto& [view_key, view_data] : views_) {
    fuchsia::ui::gfx::ViewProperties view_properties = {
        .bounding_box =
            fuchsia::ui::gfx::BoundingBox{
                .min = fuchsia::ui::gfx::vec3{.x = 0.f, .y = 0.f, .z = -logical_size().z},
                .max = fuchsia::ui::gfx::vec3{.x = width, .y = logical_size().y, .z = 0.f},
            },
        .inset_from_min = fuchsia::ui::gfx::vec3{.x = 0.f, .y = 0.f, .z = 0.f},
        .inset_from_max = fuchsia::ui::gfx::vec3{.x = 0.f, .y = 0.f, .z = 0.f},
    };

    view_data->host_node.SetTranslation(offset, 0.f, 0.f);
    view_data->host_view_holder.SetViewProperties(view_properties);
    offset += width;
  }
}

}  // namespace modular
