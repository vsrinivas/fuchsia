// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/injector.h"

#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace input {

Injector::Injector(InjectorId id, InjectorSettings settings,
                   fidl::InterfaceRequest<fuchsia::ui::pointerflow::Injector> injector)
    : binding_(this, std::move(injector)), id_(id), settings_(std::move(settings)) {
  FXL_LOG(INFO) << "Injector : Registered new injector with internal id: " << id_
                << " Device Id: " << settings_.device_id
                << " Device Type: " << static_cast<uint32_t>(settings_.device_type)
                << " Dispatch Policy: " << static_cast<uint32_t>(settings_.dispatch_policy)
                << " Context koid: " << settings_.context_koid
                << " and Target koid: " << settings_.target_koid;
}

void Injector::Inject(::std::vector<fuchsia::ui::pointerflow::Event> events,
                      InjectCallback callback) {
  FXL_LOG(ERROR) << "Injector::Inject : New injection API not yet implemented.";
  callback();
}

}  // namespace input
}  // namespace scenic_impl
