// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_FIDL_HELPERS_BOUND_INTERFACE_H_
#define SRC_LEDGER_BIN_FIDL_HELPERS_BOUND_INTERFACE_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

namespace ledger {
namespace fidl_helpers {
template <class Interface, class Impl, class Binding = fidl::Binding<Interface>>
class BoundInterface {
 public:
  template <class... Args>
  explicit BoundInterface(fidl::InterfaceRequest<Interface> request, Args&&... args)
      : impl_(std::forward<Args>(args)...), binding_(&impl_, std::move(request)) {}

  template <class... Args>
  explicit BoundInterface(Args&&... args) : impl_(std::forward<Args>(args)...), binding_(&impl_) {}

  BoundInterface(const BoundInterface&) = delete;
  BoundInterface& operator=(const BoundInterface&) = delete;

  void Bind(fidl::InterfaceRequest<Interface> request) { binding_.Bind(std::move(request)); }

  void SetOnDiscardable(fit::closure on_discardable) {
    binding_.set_error_handler(
        [this, on_discardable = std::move(on_discardable)](zx_status_t status) {
          binding_.Unbind();
          if (on_discardable)
            on_discardable();
        });
  }

  bool IsDiscardable() const { return !binding_.is_bound(); }

  Impl* impl() { return &impl_; }

 private:
  Impl impl_;
  Binding binding_;
};
}  // namespace fidl_helpers
}  // namespace ledger

#endif  // SRC_LEDGER_BIN_FIDL_HELPERS_BOUND_INTERFACE_H_
