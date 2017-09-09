// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_FIDL_HELPERS_BOUNDABLE_H_
#define APPS_LEDGER_SRC_FIDL_HELPERS_BOUNDABLE_H_

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace ledger {
namespace fidl_helpers {
// SetBoundable represents an object that can be bound to once.
template <class Interface>
class Boundable {
 public:
  virtual ~Boundable() = default;

  // Binds a single interface request to the object.
  virtual void Bind(fidl::InterfaceRequest<Interface> request) = 0;
};

// SetBoundable represents an object that can be bound to multiple times.
template <class Interface>
class SetBoundable {
 public:
  // Adds a binding to the object.
  virtual void AddBinding(fidl::InterfaceRequest<Interface> request) = 0;
};
}  // namespace fidl_helpers
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_FIDL_HELPERS_BOUNDABLE_H_
