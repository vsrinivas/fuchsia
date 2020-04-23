// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/injector.h"

#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace input {

Injector::Injector(InjectorId id, InjectorSettings settings,
                   fidl::InterfaceRequest<fuchsia::ui::pointerflow::Injector> injector,
                   fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
                       is_descendant_and_connected)
    : binding_(this, std::move(injector)),
      id_(id),
      settings_(std::move(settings)),
      is_descendant_and_connected_(std::move(is_descendant_and_connected)) {
  FXL_DCHECK(is_descendant_and_connected_);
  FXL_LOG(INFO) << "Injector : Registered new injector with internal id: " << id_
                << " Device Id: " << settings_.device_id
                << " Device Type: " << static_cast<uint32_t>(settings_.device_type)
                << " Dispatch Policy: " << static_cast<uint32_t>(settings_.dispatch_policy)
                << " Context koid: " << settings_.context_koid
                << " and Target koid: " << settings_.target_koid;
}

void Injector::Inject(::std::vector<fuchsia::ui::pointerflow::Event> events,
                      InjectCallback callback) {
  // TODO(50348): Find a way to make to listen for scene graph events instead of checking
  // connectivity per injected event.
  if (!is_descendant_and_connected_(settings_.target_koid, settings_.context_koid)) {
    FXL_LOG(ERROR) << "Inject() called with Context and Target making an invalid hierarchy.";
    // TODO(50347): Inject CANCEL event for ongoing stream before closing channel.
    binding_.Close(ZX_ERR_BAD_STATE);
    return;
  }

  // TODO(48972): Actually inject.
  callback();
}

}  // namespace input
}  // namespace scenic_impl
