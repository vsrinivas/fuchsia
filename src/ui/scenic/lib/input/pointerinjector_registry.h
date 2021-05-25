// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_POINTERINJECTOR_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_INPUT_POINTERINJECTOR_REGISTRY_H_

#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

#include "lib/inspect/cpp/inspect.h"
#include "src/ui/scenic/lib/input/injector.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

using InjectFunc = fit::function<void(const InternalPointerEvent& event, StreamId stream_id)>;

// Handles the registration and config validation of fuchsia::ui::pointerinjector clients.
class PointerinjectorRegistry : public fuchsia::ui::pointerinjector::Registry {
 public:
  PointerinjectorRegistry(sys::ComponentContext* context, InjectFunc inject_touch_exclusive,
                          InjectFunc inject_touch_hit_tested, InjectFunc inject_mouse_exclusive,
                          InjectFunc inject_mouse_hit_tested,
                          inspect::Node inspect_node = inspect::Node());

  // |fuchsia.ui.pointerinjector.Registry|
  void Register(fuchsia::ui::pointerinjector::Config config,
                fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> injector,
                RegisterCallback callback) override;

  void OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot) {
    view_tree_snapshot_ = std::move(snapshot);
  }

 private:
  using InjectorType = std::pair<fuchsia::ui::pointerinjector::DeviceType,
                                 fuchsia::ui::pointerinjector::DispatchPolicy>;
  struct InjectorTypeHash {
    std::size_t operator()(const InjectorType& pair) const {
      return (static_cast<uint64_t>(pair.first) << 32) | static_cast<uint64_t>(pair.second);
    }
  };

  using InjectorId = uint64_t;
  InjectorId last_injector_id_ = 0;
  std::unordered_map<InjectorId, Injector> injectors_;

  fidl::BindingSet<fuchsia::ui::pointerinjector::Registry> injector_registry_;

  std::unordered_map<InjectorType, InjectFunc, InjectorTypeHash> inject_funcs_;

  std::shared_ptr<const view_tree::Snapshot> view_tree_snapshot_ =
      std::make_shared<const view_tree::Snapshot>();

  inspect::Node inspect_node_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_POINTERINJECTOR_REGISTRY_H_
