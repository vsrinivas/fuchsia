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
    : executor_(dispatcher), binding_(this), realm_(realm), weak_ptr_factory_(this) {}

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
  const zx_status_t status =
      async::PostTask(executor_.dispatcher(), [self = weak_ptr_factory_.GetWeakPtr()] {
        if (!self) {
          FX_DLOGS(WARNING) << "called posted task after exit, skipping callback";
          return;
        }
        self->NotifyOfExistingComponents();
      });
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not synthesize events for existing components: "
                   << zx_status_get_string(status);
  }
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

void ComponentEventProviderImpl::NotifyOfExistingComponents() {
  std::queue<fxl::WeakPtr<Realm>> pending_realms;
  pending_realms.push(realm_);
  while (!pending_realms.empty()) {
    auto realm = pending_realms.front();
    pending_realms.pop();

    // Make sure we notify about all components in sub-realms of this realm which don't have an
    // event listener attached.
    for (auto& pair : realm->children()) {
      if (!pair.second->realm()->HasComponentEventListenerBound()) {
        pending_realms.push(pair.second->realm()->weak_ptr());
      }
    }
    auto relative_realm_path = RelativeRealmPath(realm);

    // Notify about all components in this realm.
    for (auto& pair : realm->applications()) {
      NotifyAboutExistingComponent(relative_realm_path, pair.second);
    }

    // Notify about all components in runners in this realm.
    for (auto& pair : realm->runners()) {
      const auto& runner = pair.second;
      for (auto& comp_pair : runner->components()) {
        const auto& component_bridge = comp_pair.second;
        // Given that an environment might have been created with use_parent_runners, we need to get
        // its actual realm which might not be the realm where the runner is.
        fxl::WeakPtr<Realm> realm = component_bridge->realm();
        if (realm) {
          NotifyAboutExistingComponent(RelativeRealmPath(realm), component_bridge);
        }
      }
    }
  }
}

void ComponentEventProviderImpl::NotifyAboutExistingComponent(
    std::vector<std::string> relative_realm_path,
    std::shared_ptr<ComponentControllerBase> application) {
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
      [self = weak_ptr_factory_.GetWeakPtr(),
       identity = std::move(identity)](fidl::InterfaceHandle<fuchsia::io::Directory>& dir) mutable {
        if (self) {
          self->NotifyComponentDirReady(std::move(identity), std::move(dir));
        }
      }));
}

}  // namespace component
