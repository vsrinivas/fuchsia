// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_FIDL_HELPERS_BOUND_INTERFACE_SET_H_
#define APPS_LEDGER_SRC_FIDL_HELPERS_BOUND_INTERFACE_SET_H_

#include "peridot/bin/ledger/fidl_helpers/boundable.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace ledger {
namespace fidl_helpers {
template <class Interface, class Impl>
class BoundInterfaceSet : public SetBoundable<Interface> {
 public:
  template <class... Args>
  explicit BoundInterfaceSet(Args&&... args)
      : impl_(std::forward<Args>(args)...) {}

  void AddBinding(fidl::InterfaceRequest<Interface> request) final {
    binding_.AddBinding(&impl_, std::move(request));
  }

  void CloseAllBindings() { binding_.CloseAllBindings(); }

  void set_on_empty(const fxl::Closure& on_empty_callback) {
    binding_.set_connection_error_handler([this, on_empty_callback]() {
      binding_.Close();
      if (on_empty_callback)
        on_empty_callback();
    });
  }

  bool is_bound() { return binding_.is_bound(); }

 private:
  Impl impl_;
  fidl::BindingSet<Interface> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BoundInterfaceSet);
};
}  // namespace fidl_helpers
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_FIDL_HELPERS_BOUND_INTERFACE_SET_H_
