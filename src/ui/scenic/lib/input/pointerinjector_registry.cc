// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/pointerinjector_registry.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/math.h"

namespace scenic_impl::input {

namespace {

bool IsValidConfig(const fuchsia::ui::pointerinjector::Config& config) {
  if (!config.has_device_id() || !config.has_device_type() || !config.has_context() ||
      !config.has_target() || !config.has_viewport() || !config.has_dispatch_policy()) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config| is incomplete.";
    return false;
  }

  if (config.dispatch_policy() != fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET &&
      config.dispatch_policy() !=
          fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Only EXCLUSIVE_TARGET and "
                      "TOP_HIT_AND_ANCESTORS_IN_TARGET DispatchPolicy is supported.";
    return false;
  }

  if (config.device_type() != fuchsia::ui::pointerinjector::DeviceType::TOUCH) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Only DeviceType TOUCH is supported.";
    return false;
  }

  if (!config.context().is_view() || !config.target().is_view()) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config.context| or |config.target| "
                      "is not a view. Only views are supported.";
    return false;
  }

  if (Injector::IsValidViewport(config.viewport()) != ZX_OK) {
    // Errors printed in IsValidViewport. Just return result here.
    return false;
  }

  return true;
}

}  // namespace

PointerinjectorRegistry::PointerinjectorRegistry(sys::ComponentContext* context,
                                                 InjectFunc inject_touch_exclusive,
                                                 InjectFunc inject_touch_hit_tested,
                                                 inspect::Node inspect_node)
    : inject_touch_exclusive_(std::move(inject_touch_exclusive)),
      inject_touch_hit_tested_(std::move(inject_touch_hit_tested)),
      inspect_node_(std::move(inspect_node)) {
  if (context) {
    // Adding the service here is safe since the PointerinjectorRegistry instance in InputSystem is
    // created at construction time..
    context->outgoing()->AddPublicService(injector_registry_.GetHandler(this));
  }
}

void PointerinjectorRegistry::Register(
    fuchsia::ui::pointerinjector::Config config,
    fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> injector,
    RegisterCallback callback) {
  if (!IsValidConfig(config)) {
    // Errors printed inside IsValidConfig. Just return here.
    return;
  }

  // Check connectivity here, since injector doesn't have access to it.
  const zx_koid_t context_koid = utils::ExtractKoid(config.context().view());
  const zx_koid_t target_koid = utils::ExtractKoid(config.target().view());
  if (context_koid == ZX_KOID_INVALID || target_koid == ZX_KOID_INVALID) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config.context| or |config.target| "
                      "was invalid.";
    return;
  }
  if (!view_tree_snapshot_->IsDescendant(target_koid, context_koid)) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config.context| must be connected to "
                      "the Scene, and |config.target| must be a descendant of |config.context|";
    return;
  }

  // TODO(fxbug.dev/50348): Add a callback to kill the channel immediately if connectivity breaks.

  const InjectorId id = ++last_injector_id_;
  InjectorSettings settings{.dispatch_policy = config.dispatch_policy(),
                            .device_id = config.device_id(),
                            .device_type = config.device_type(),
                            .context_koid = context_koid,
                            .target_koid = target_koid};
  Viewport viewport{
      .extents = {config.viewport().extents()},
      .context_from_viewport_transform =
          utils::ColumnMajorMat3VectorToMat4(config.viewport().viewport_to_context_transform()),
  };

  fit::function<void(const InternalPointerEvent&, StreamId)> inject_func;
  switch (settings.dispatch_policy) {
    case fuchsia::ui::pointerinjector::DispatchPolicy::EXCLUSIVE_TARGET:
      inject_func = [this](const InternalPointerEvent& event, StreamId stream_id) {
        inject_touch_exclusive_(event, stream_id);
      };
      break;
    case fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET:
      inject_func = [this](const InternalPointerEvent& event, StreamId stream_id) {
        inject_touch_hit_tested_(event, stream_id);
      };
      break;
    default:
      FX_CHECK(false) << "Should never be reached.";
      break;
  }

  const auto [it, success] = injectors_.try_emplace(
      id, inspect_node_.CreateChild(inspect_node_.UniqueName("injector-")), std::move(settings),
      std::move(viewport), std::move(injector),
      /*is_descendant_and_connected*/
      [this](zx_koid_t descendant, zx_koid_t ancestor) {
        return view_tree_snapshot_->IsDescendant(descendant, ancestor);
      },
      std::move(inject_func),
      /*on_channel_closed*/
      [this, id] { injectors_.erase(id); });
  FX_CHECK(success) << "Injector already exists.";

  callback();
}

}  // namespace scenic_impl::input
