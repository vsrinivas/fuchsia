// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_CONTAINER_STATE_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_CONTAINER_STATE_H_

#include <array>
#include <iosfwd>
#include <memory>
#include <unordered_map>
#include <vector>

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "garnet/bin/ui/view_manager/view_stub.h"
#include "src/lib/fxl/macros.h"

namespace view_manager {

class ViewState;
class ViewStub;
class ViewTreeState;

// Base class for views and view trees.
// This object is owned by the ViewRegistry that created it.
class ViewContainerState {
 public:
  // Whether the View below us is connected.
  enum ViewConnectionState { UNKNOWN, CONNECTED, DISCONNECTED };
  struct ChildView {
    // The ViewContainer we are attached to.
    ViewContainerState* container = nullptr;
    // Whether the View below us is connected.
    ViewConnectionState view_connected = UNKNOWN;
    ViewRegistry* view_registry = nullptr;
    // If zero, then it's not attached.
    uint32_t child_key = 0;
    std::unique_ptr<scenic::Session> session;
    std::unique_ptr<scenic::ImportNode> host_node;
    scenic::ViewHolder view_holder;
    std::array<float, 3> min_dimensions = {0, 0, 0};
    std::array<float, 3> max_dimensions = {0, 0, 0};
    std::array<float, 3> inset_min = {0, 0, 0};
    std::array<float, 3> inset_max = {0, 0, 0};

    static void OnScenicEvent(ChildView* session,
                              std::vector<fuchsia::ui::scenic::Event> events);
  };

  ViewContainerState(ViewRegistry* registry,
                     fuchsia::ui::scenic::Scenic* scenic);

  // Gets or sets the view container listener.
  ::fuchsia::ui::viewsv1::ViewContainerListener* view_container_listener()
      const {
    return view_container_listener_.get();
  }
  void set_view_container_listener(
      ::fuchsia::ui::viewsv1::ViewContainerListenerPtr
          view_container_listener) {
    view_container_listener_ = std::move(view_container_listener);
  }

  // The map of children, indexed by child key.
  const std::unordered_map<uint32_t, std::unique_ptr<ChildView>>& children()
      const {
    return children_;
  }

  // Removes all children as a single operation.
  void RemoveAllChildren();

  // Transform the properties into a SendViewPropertiesCmd,
  // and forward it to Scenic.
  void SetChildProperties(
      uint32_t child_key,
      ::fuchsia::ui::viewsv1::ViewPropertiesPtr child_properties);

  // Dynamic type tests (ugh).
  virtual ViewState* AsViewState();
  virtual ViewTreeState* AsViewTreeState();

  virtual const std::string& FormattedLabel() const = 0;

  void AddChild(uint32_t child_key, zx::eventpair view_holder_token,
                zx::eventpair host_import_token);

  void RemoveChild(uint32_t child_key,
                   zx::eventpair transferred_view_holder_token);

 protected:
  virtual ~ViewContainerState();

 private:
  ViewRegistry* view_registry_ = nullptr;
  fuchsia::ui::scenic::Scenic* scenic_ = nullptr;

  fuchsia::ui::viewsv1::ViewContainerListenerPtr view_container_listener_;

  std::unordered_map<uint32_t, std::unique_ptr<ChildView>> children_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewContainerState);
};

std::ostream& operator<<(std::ostream& os, ViewContainerState* view_state);

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_CONTAINER_STATE_H_
