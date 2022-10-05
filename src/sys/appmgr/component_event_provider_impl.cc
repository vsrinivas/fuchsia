// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/component_event_provider_impl.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/sys/appmgr/realm.h"

namespace component {

ComponentEventProviderImpl::ComponentEventProviderImpl(fxl::WeakPtr<Realm> realm,
                                                       async_dispatcher_t* dispatcher)
    : executor_(dispatcher), binding_(this), realm_(realm) {}

ComponentEventProviderImpl::~ComponentEventProviderImpl() = default;

void ComponentEventProviderImpl::SetListener(
    fidl::InterfaceHandle<fuchsia::sys::internal::ComponentEventListener> listener) {
  if (listener_.is_bound()) {
    return;
  }
  listener_ = listener.Bind();
  listener_.set_error_handler([this](zx_status_t status) {
    listener_.Unbind();
    listener_.set_error_handler(nullptr);
  });
}

zx_status_t ComponentEventProviderImpl::Connect(
    fidl::InterfaceRequest<fuchsia::sys::internal::ComponentEventProvider> request) {
  if (binding_.is_bound()) {
    return ZX_ERR_BAD_STATE;
  }
  binding_.Bind(std::move(request));
  return ZX_OK;
}

void ComponentEventProviderImpl::NotifyComponentStopped(
    fuchsia::sys::internal::SourceIdentity component) {
  if (listener_.is_bound()) {
    listener_->OnStop(std::move(component));
  }
}

void ComponentEventProviderImpl::NotifyComponentDirReady(
    fuchsia::sys::internal::SourceIdentity component,
    fidl::InterfaceHandle<fuchsia::io::Directory> directory) {
  if (listener_.is_bound()) {
    listener_->OnDiagnosticsDirReady(std::move(component), std::move(directory));
  }
}

std::vector<std::string> ComponentEventProviderImpl::RelativeRealmPath(
    const fxl::WeakPtr<Realm>& leaf_realm) {
  std::vector<std::string> relative_realm_path;
  auto realm = leaf_realm;

  // We stop traversing the realm tree bottom up until we arrive to this realm_ or the root.
  while (realm && realm.get() != realm_.get()) {
    relative_realm_path.push_back(realm->label());
    realm = realm->parent();
  }

  // We arrived to root and we couldn't find |realm_| therefore this realm is not in the path.
  // Just a sanity check, this shouldn't occur given that this provider only calls this method with
  // realms under it.
  if (realm.get() != realm_.get()) {
    FX_LOGS(ERROR) << "Unreachable: ComponentEventProvider attempted to get a relative realm path "
                   << "from a realm not in its tree";
    return {};
  }

  std::reverse(relative_realm_path.begin(), relative_realm_path.end());
  return relative_realm_path;
}

}  // namespace component
