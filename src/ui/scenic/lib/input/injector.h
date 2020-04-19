// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
#define SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_

#include <fuchsia/ui/pointerflow/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>

namespace scenic_impl {
namespace input {

using InjectorId = uint64_t;

// Non-FIDL-type struct for keeping client defined settings.
struct InjectorSettings {
  fuchsia::ui::pointerflow::DispatchPolicy dispatch_policy;
  uint32_t device_id;
  fuchsia::ui::input3::PointerDeviceType device_type;
  zx_koid_t context_koid;
  zx_koid_t target_koid;
};

// Implementation of the |fuchsia::ui::pointerflow::Injector| interface. One instance per channel.
class Injector : public fuchsia::ui::pointerflow::Injector {
 public:
  Injector(InjectorId id, InjectorSettings settings,
           fidl::InterfaceRequest<fuchsia::ui::pointerflow::Injector> injector);

  // |fuchsia::ui::pointerflow::Injector|
  void Inject(std::vector<fuchsia::ui::pointerflow::Event> events,
              InjectCallback callback) override;

  void set_error_handler(fit::function<void(zx_status_t)> error_handler) {
    binding_.set_error_handler(std::move(error_handler));
  }

 private:
  fidl::Binding<fuchsia::ui::pointerflow::Injector> binding_;

  // Scenic-internal identifier.
  const InjectorId id_;

  // Client defined data.
  const InjectorSettings settings_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
