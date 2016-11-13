// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_FIDL_BOUND_INTERFACE_H_
#define APPS_LEDGER_SRC_APP_FIDL_BOUND_INTERFACE_H_

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace ledger {
template <class Interface, class Impl>
class BoundInterface {
 public:
  template <class... Args>
  BoundInterface(fidl::InterfaceRequest<Interface> request, Args&&... args)
      : impl_(std::forward<Args>(args)...),
        binding_(&impl_, std::move(request)) {}

  void set_on_empty(const ftl::Closure& on_empty_callback) {
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

  FTL_DISALLOW_COPY_AND_ASSIGN(BoundInterface);
};
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_FIDL_BOUND_INTERFACE_H_
