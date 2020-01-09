// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/component_event_provider_impl.h"

#include <lib/async/default.h>

#include "src/lib/fxl/logging.h"

namespace component {

ComponentEventProviderImpl::ComponentEventProviderImpl(Realm* realm)
    : executor_(async_get_default_dispatcher()),
      binding_(this),
      realm_(realm),
      weak_ptr_factory_(this) {}

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
  async::PostTask(async_get_default_dispatcher(), [this] { NotifyOfExistingComponents(); });
}

zx_status_t ComponentEventProviderImpl::Connect(
    fidl::InterfaceRequest<fuchsia::sys::internal::ComponentEventProvider> request) {
  if (binding_.is_bound()) {
    return ZX_ERR_BAD_STATE;
  }
  binding_.Bind(std::move(request));
  return ZX_OK;
}

void ComponentEventProviderImpl::NotifyComponentStarted(
    fuchsia::sys::internal::SourceIdentity component) {
  if (listener_.is_bound()) {
    listener_->OnStart(std::move(component));
  }
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

std::vector<std::string> ComponentEventProviderImpl::RelativeRealmPath(Realm* leaf_realm) {
  std::vector<std::string> relative_realm_path;
  // We don't want to include this realms label in the realm path. As we don't want to expose this
  // realm name to the subscribing component running under this realm.
  if (leaf_realm != realm_) {
    relative_realm_path.push_back(leaf_realm->label());
  }
  Realm* realm = leaf_realm;

  // We stop traversing the realm tree bottom up until we arrive to this realm_ or the root.
  while (realm != realm_ && realm_->parent()) {
    realm = realm->parent();
    relative_realm_path.push_back(realm->label());
  }

  // We arrived to root and we couldn't find |realm_| therefore this realm is not in the path.
  // Just a sanity check, this shouldn't occur given that this provider only calls this method with
  // realms under it.
  if (realm != realm_) {
    FXL_LOG(ERROR) << "Unreachable: ComponentEventProvider attempted to get a relative realm path "
                   << "from a realm not in its tree";
    return {};
  }

  std::reverse(relative_realm_path.begin(), relative_realm_path.end());
  return relative_realm_path;
}

void ComponentEventProviderImpl::NotifyOfExistingComponents() {
  std::queue<Realm*> pending_realms;
  pending_realms.push(realm_);
  while (!pending_realms.empty()) {
    auto realm = pending_realms.front();
    pending_realms.pop();

    // Make sure we notify about all components in sub-realms of this realm which don't have an
    // event listener attached.
    for (auto& pair : realm->children()) {
      if (realm != realm_ && !realm->HasComponentEventListenerBound()) {
        pending_realms.push(pair.first);
      }
    }
    auto relative_realm_path = RelativeRealmPath(realm);

    // Notify about all components in this realm.
    for (auto& pair : realm->applications()) {
      const auto& application = pair.second;
      fuchsia::sys::internal::SourceIdentity identity;
      identity.set_component_url(application->url());
      identity.set_component_name(application->label());
      identity.set_instance_id(application->hub_instance_id());
      identity.set_realm_path(relative_realm_path);
      NotifyComponentStarted(fidl::Clone(identity));

      // If the component doesn't have an out/diagnostics directory or its out/ directory ready
      // doesn't exist, the and_then combinator won't be executed. Once the component exposes a
      // diagnostics directory (if ever), the listener will be notified through the regular flow.
      executor_.schedule_task(application->GetDiagnosticsDir().and_then(
          [self = weak_ptr_factory_.GetWeakPtr(), identity = std::move(identity)](
              fidl::InterfaceHandle<fuchsia::io::Directory>& dir) mutable {
            if (self) {
              self->NotifyComponentDirReady(std::move(identity), std::move(dir));
            }
          }));
    }
  }
}

}  // namespace component
