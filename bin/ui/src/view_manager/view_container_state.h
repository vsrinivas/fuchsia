// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_CONTAINER_STATE_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_CONTAINER_STATE_H_

#include <iosfwd>
#include <memory>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "base/macros.h"
#include "mojo/services/ui/views/interfaces/view_containers.mojom.h"
#include "services/ui/view_manager/view_stub.h"

namespace view_manager {

class ViewState;
class ViewStub;
class ViewTreeState;

// Base class for views and view trees.
// This object is owned by the ViewRegistry that created it.
class ViewContainerState {
 public:
  using ChildrenMap = std::unordered_map<uint32_t, std::unique_ptr<ViewStub>>;

  ViewContainerState();

  // Gets or sets the view container listener.
  mojo::ui::ViewContainerListener* view_container_listener() const {
    return view_container_listener_.get();
  }
  void set_view_container_listener(
      mojo::ui::ViewContainerListenerPtr view_container_listener) {
    view_container_listener_ = view_container_listener.Pass();
  }

  // The map of children, indexed by child key.
  // The view stub pointers are never null but some view stubs may
  // have been marked unavailable.
  const ChildrenMap& children() const { return children_; }

  // Links a child into the view tree.
  void LinkChild(uint32_t key, std::unique_ptr<ViewStub> child);

  // Unlinks a child of the view tree.
  std::unique_ptr<ViewStub> UnlinkChild(uint32_t key);

  // Unlinks all children as a single operation.
  std::vector<std::unique_ptr<ViewStub>> UnlinkAllChildren();

  // Dynamic type tests (ugh).
  virtual ViewState* AsViewState();
  virtual ViewTreeState* AsViewTreeState();

  virtual const std::string& FormattedLabel() const = 0;

 protected:
  virtual ~ViewContainerState();

 private:
  mojo::ui::ViewContainerListenerPtr view_container_listener_;
  ChildrenMap children_;

  DISALLOW_COPY_AND_ASSIGN(ViewContainerState);
};

std::ostream& operator<<(std::ostream& os, ViewContainerState* view_state);

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_CONTAINER_STATE_H_
