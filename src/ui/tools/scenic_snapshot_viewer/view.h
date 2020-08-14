// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_SNAPSHOT_VIEW_H_
#define SRC_UI_TOOLS_SNAPSHOT_VIEW_H_

#include <fuchsia/scenic/snapshot/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/ui/base_view/base_view.h"
#include "src/ui/scenic/lib/gfx/snapshot/snapshot_generated.h"

namespace scenic_snapshot_viewer {

// A view that displays saved snapshot of views.
class View final : public scenic::BaseView, public fuchsia::scenic::snapshot::Loader {
 public:
  explicit View(scenic::ViewContext view_context);
  ~View() override = default;

  // |fuchsia::scenic::snapshot::Loader|.
  virtual void Load(::fuchsia::mem::Buffer payload) override;

 private:
  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FX_LOGS(ERROR) << "Scenic Error " << error; }

  fidl::BindingSet<fuchsia::scenic::snapshot::Loader> loader_bindings_;

  void LoadNode(scenic::ContainerNode& parent_node, const snapshot::Node* flat_node);
  void LoadShape(scenic::EntityNode& parent_node, const snapshot::Node* flat_node);
  void LoadMaterial(scenic::ShapeNode& parent_node, const snapshot::Node* flat_node);

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace scenic_snapshot_viewer

#endif  // SRC_UI_TOOLS_SNAPSHOT_VIEW_H_
