// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/introspector.h"

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/sys/appmgr/realm.h"

namespace component {

using fuchsia::sys::internal::Introspect_FindComponentByProcessKoid_Response;
using fuchsia::sys::internal::Introspect_FindComponentByProcessKoid_Result;

IntrospectImpl::IntrospectImpl(Realm* realm) : realm_(realm) { ZX_ASSERT(realm_ != nullptr); }

IntrospectImpl::~IntrospectImpl() = default;

void IntrospectImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::internal::Introspect> request) {
  bindings_.AddBinding(this, std::move(request));
}

void IntrospectImpl::FindComponentByProcessKoid(zx_koid_t process,
                                                FindComponentByProcessKoidCallback callback) {
  Introspect_FindComponentByProcessKoid_Result result;
  auto status = realm_->FindComponent(process);
  if (status.is_ok()) {
    Introspect_FindComponentByProcessKoid_Response response;
    response.component_info = std::move(status.value());
    result.set_response(std::move(response));
  } else if (status.error_value() == ZX_ERR_NOT_FOUND) {
    result.set_err(status.error_value());
  } else {
    FX_LOGS(ERROR) << "Error running Realm::FindComponent: "
                   << zx_status_get_string(status.error_value());
    result.set_err(ZX_ERR_INTERNAL);
  }
  callback(std::move(result));
}

}  // namespace component
