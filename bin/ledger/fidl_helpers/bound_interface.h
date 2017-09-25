// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_FIDL_HELPERS_BOUND_INTERFACE_H_
#define PERIDOT_BIN_LEDGER_FIDL_HELPERS_BOUND_INTERFACE_H_

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/fidl_helpers/boundable.h"

namespace ledger {
namespace fidl_helpers {
template <class Interface, class Impl>
class BoundInterface : public Boundable<Interface> {
 public:
  template <class... Args>
  explicit BoundInterface(fidl::InterfaceRequest<Interface> request,
                          Args&&... args)
      : impl_(std::forward<Args>(args)...),
        binding_(&impl_, std::move(request)) {}

  template <class... Args>
  explicit BoundInterface(Args&&... args)
      : impl_(std::forward<Args>(args)...), binding_(&impl_) {}

  void Bind(fidl::InterfaceRequest<Interface> request) final {
    binding_.Bind(std::move(request));
  }

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
  fidl::Binding<Interface> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BoundInterface);
};
}  // namespace fidl_helpers
}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_FIDL_HELPERS_BOUND_INTERFACE_H_
