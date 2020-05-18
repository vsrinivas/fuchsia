// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_INTROSPECTOR_H_
#define SRC_SYS_APPMGR_INTROSPECTOR_H_

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <zircon/types.h>

#include "src/lib/fxl/macros.h"

namespace component {

class Realm;

class IntrospectImpl : public fuchsia::sys::internal::Introspect {
 public:
  explicit IntrospectImpl(Realm* realm);

  ~IntrospectImpl() override;

  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::internal::Introspect> request);

  void FindComponentByProcessKoid(zx_koid_t process,
                                  FindComponentByProcessKoidCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::sys::internal::Introspect> bindings_;
  Realm* const realm_;  // Not owned.

  FXL_DISALLOW_COPY_AND_ASSIGN(IntrospectImpl);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_INTROSPECTOR_H_
