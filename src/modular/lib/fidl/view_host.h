// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_FIDL_VIEW_HOST_H_
#define SRC_MODULAR_LIB_FIDL_VIEW_HOST_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>

#include <map>
#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/ui/base_view/base_view.h"

namespace modular {

// A class that allows modules to display the UI of their child
// modules, without displaying any UI on their own. Used for modules
// that play the role of a view controller (aka quarterback, recipe).
// It supports to embed views of *multiple* children, which are laid
// out horizontally.
class ViewHost : public scenic::BaseView {
 public:
  explicit ViewHost(scenic::ViewContext view_context);
  ~ViewHost() override = default;

  // Connects one more view. Calling this method multiple times adds
  // multiple views and lays them out horizontally next to each other.
  // This is experimental to establish data flow patterns in toy
  // applications and can be changed or extended as needed.
  void ConnectView(fuchsia::ui::views::ViewHolderToken view_holder_token);

 private:
  struct ViewData {
    explicit ViewData(scenic::Session* session,
                      fuchsia::ui::views::ViewHolderToken view_holder_token)
        : host_node(session),
          host_view_holder(session, std::move(view_holder_token), "modular::ViewHost") {
      host_node.Attach(host_view_holder);
    }

    scenic::EntityNode host_node;
    scenic::ViewHolder host_view_holder;
  };

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FX_LOGS(ERROR) << "Scenic Error " << error; }

  // |scenic::BaseView|
  void OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) override;
  void OnScenicEvent(fuchsia::ui::scenic::Event event) override;

  void UpdateScene();

  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewHost);
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_FIDL_VIEW_HOST_H_
