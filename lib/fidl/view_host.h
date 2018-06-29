// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_VIEW_HOST_H_
#define PERIDOT_LIB_FIDL_VIEW_HOST_H_

#include <map>
#include <memory>

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>

#include "lib/fxl/macros.h"
#include "lib/ui/view_framework/base_view.h"

namespace modular {

// A class that allows modules to display the UI of their child
// modules, without displaying any UI on their own. Used for modules
// that play the role of a view controller (aka quarterback, recipe).
// It supports to embed views of *multiple* children, which are laid
// out horizontally.
class ViewHost : public mozart::BaseView {
 public:
  explicit ViewHost(
      fuchsia::ui::views_v1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request);
  ~ViewHost() override;

  // Connects one more view. Calling this method multiple times adds
  // multiple views and lays them out horizontally next to each other.
  // This is experimental to establish data flow patterns in toy
  // applications and can be changed or extended as needed.
  void ConnectView(
      fidl::InterfaceHandle<fuchsia::ui::views_v1_token::ViewOwner> view_owner);

 private:
  struct ViewData;

  // |BaseView|:
  void OnPropertiesChanged(
      fuchsia::ui::views_v1::ViewProperties old_properties) override;
  void OnChildUnavailable(uint32_t child_key) override;

  void UpdateScene();

  scenic::EntityNode container_node_;

  std::map<uint32_t, std::unique_ptr<ViewData>> views_;
  uint32_t next_child_key_{1u};

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewHost);
};

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_VIEW_HOST_H_
