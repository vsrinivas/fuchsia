// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_POINTERINJECTOR_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_INPUT_POINTERINJECTOR_REGISTRY_H_

#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

#include "lib/inspect/cpp/inspect.h"
#include "src/ui/scenic/lib/input/injector.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

using TouchInjectFunc = fit::function<void(const InternalTouchEvent& event, StreamId stream_id)>;
using MouseInjectFunc = fit::function<void(const InternalMouseEvent& event, StreamId stream_id)>;

// Handles the registration and config validation of fuchsia::ui::pointerinjector clients.
class PointerinjectorRegistry : public fuchsia::ui::pointerinjector::Registry {
 public:
  PointerinjectorRegistry(sys::ComponentContext* context, TouchInjectFunc inject_touch_exclusive,
                          TouchInjectFunc inject_touch_hit_tested,
                          MouseInjectFunc inject_mouse_exclusive,
                          MouseInjectFunc inject_mouse_hit_tested,
                          fit::function<void(StreamId stream_id)> cancel_mouse_stream,
                          inspect::Node inspect_node = inspect::Node());

  // |fuchsia.ui.pointerinjector.Registry|
  void Register(fuchsia::ui::pointerinjector::Config config,
                fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> injector,
                RegisterCallback callback) override;

  void OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot) {
    view_tree_snapshot_ = std::move(snapshot);
  }

 private:
  using InjectorId = uint64_t;
  InjectorId last_injector_id_ = 0;
  std::unordered_map<InjectorId, std::unique_ptr<Injector>> injectors_;

  fidl::BindingSet<fuchsia::ui::pointerinjector::Registry> injector_registry_;

  const TouchInjectFunc inject_touch_exclusive_;
  const TouchInjectFunc inject_touch_hit_tested_;
  const MouseInjectFunc inject_mouse_exclusive_;
  const MouseInjectFunc inject_mouse_hit_tested_;
  const fit::function<void(StreamId stream_id)> cancel_mouse_stream_;

  std::shared_ptr<const view_tree::Snapshot> view_tree_snapshot_ =
      std::make_shared<const view_tree::Snapshot>();

  inspect::Node inspect_node_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_POINTERINJECTOR_REGISTRY_H_
