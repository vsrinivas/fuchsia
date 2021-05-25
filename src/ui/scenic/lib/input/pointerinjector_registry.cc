// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/pointerinjector_registry.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/math.h"

namespace scenic_impl::input {

using fuchsia::ui::pointerinjector::DeviceType;
using fuchsia::ui::pointerinjector::DispatchPolicy;

namespace {

bool IsValidConfig(const fuchsia::ui::pointerinjector::Config& config) {
  if (!config.has_device_id() || !config.has_device_type() || !config.has_context() ||
      !config.has_target() || !config.has_viewport() || !config.has_dispatch_policy()) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Argument |config| is incomplete.";
    return false;
  }

  const auto device_type = config.device_type();
  if (device_type != DeviceType::TOUCH && device_type != DeviceType::MOUSE) {
    FX_LOGS(ERROR) << "InjectorRegistry::Register : Unknown DeviceType.";
    return false;
  }

  const auto dispatch_policy = config.dispatch_policy();
  if (device_type == DeviceType::MOUSE) {
    if (dispatch_policy != DispatchPolicy::EXCLUSIVE_TARGET &&
        dispatch_policy != DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET) {
      FX_LOGS(ERROR)
          << "InjectorRegistry::Register : DeviceType::MOUSE with mismatched dispatch policy.";
      return false;
    }
  } else if (device_type == DeviceType::TOUCH) {
    if (dispatch_policy != DispatchPolicy::EXCLUSIVE_TARGET &&
        dispatch_policy != DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET) {
      FX_LOGS(ERROR)
          << "InjectorRegistry::Register : DeviceType::TOUCH with mismatched dispatch policy.";
      return false;
    }
  } else {
    FX_NOTREACHED();
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
                                                 InjectFunc inject_mouse_exclusive,
                                                 InjectFunc inject_mouse_hit_tested,
                                                 inspect::Node inspect_node)
    : inspect_node_(std::move(inspect_node)) {
  // Can't use an initializer list for this, since InjectFunc is move-only. Otherwise
  // |inject_funcs_| could be made const.
  inject_funcs_.try_emplace({DeviceType::TOUCH, DispatchPolicy::EXCLUSIVE_TARGET},
                            std::move(inject_touch_exclusive));
  inject_funcs_.try_emplace({DeviceType::TOUCH, DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET},
                            std::move(inject_touch_hit_tested));
  inject_funcs_.try_emplace({DeviceType::MOUSE, DispatchPolicy::EXCLUSIVE_TARGET},
                            std::move(inject_mouse_exclusive));
  inject_funcs_.try_emplace({DeviceType::MOUSE, DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET},
                            std::move(inject_mouse_hit_tested));

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

  const auto [it, success] = injectors_.try_emplace(
      id, inspect_node_.CreateChild(inspect_node_.UniqueName("injector-")), std::move(settings),
      std::move(viewport), std::move(injector),
      /*is_descendant_and_connected*/
      [this](zx_koid_t descendant, zx_koid_t ancestor) {
        return view_tree_snapshot_->IsDescendant(descendant, ancestor);
      },
      /*inject*/
      [&inject_func = inject_funcs_.at({settings.device_type, settings.dispatch_policy})](
          const InternalPointerEvent& event, StreamId stream_id) { inject_func(event, stream_id); },
      /*on_channel_closed*/
      [this, id] { injectors_.erase(id); });
  FX_CHECK(success) << "Injector already exists.";

  callback();
}

}  // namespace scenic_impl::input
