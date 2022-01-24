// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVER_H_
#define LIB_FIDL_LLCPP_SERVER_H_

#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/internal/arrow.h>
#include <lib/fidl/llcpp/internal/endpoints.h>
#include <lib/fidl/llcpp/internal/server_details.h>
#include <lib/fidl/llcpp/wire_messaging_declarations.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

namespace fidl {

namespace internal {

class ServerBindingRefBase;
std::weak_ptr<AsyncServerBinding> BorrowBinding(const ServerBindingRefBase&);

class ServerBindingRefBase {
 public:
  explicit ServerBindingRefBase(std::weak_ptr<internal::AsyncServerBinding> binding)
      : binding_(std::move(binding)) {}
  ~ServerBindingRefBase() = default;

  void Unbind() {
    if (auto binding = binding_.lock())
      binding->StartTeardown(std::move(binding));
  }

 protected:
  const std::weak_ptr<internal::AsyncServerBinding>& binding() const { return binding_; }

 private:
  friend std::weak_ptr<internal::AsyncServerBinding> internal::BorrowBinding(
      const ServerBindingRefBase&);

  std::weak_ptr<internal::AsyncServerBinding> binding_;
};

inline std::weak_ptr<AsyncServerBinding> BorrowBinding(const ServerBindingRefBase& binding_ref) {
  return binding_ref.binding();
}

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SERVER_H_
