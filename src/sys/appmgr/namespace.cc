// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/namespace.h"

#include <fuchsia/process/cpp/fidl.h>
#include <fuchsia/sys/internal/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/trace/event.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <utility>

#include "fbl/ref_ptr.h"
#include "fuchsia/sys/cpp/fidl.h"
#include "lib/async/cpp/task.h"
#include "lib/syslog/cpp/macros.h"
#include "src/sys/appmgr/job_provider_impl.h"
#include "src/sys/appmgr/realm.h"
#include "src/sys/appmgr/util.h"

namespace component {

Namespace::Namespace(fxl::WeakPtr<Realm> realm, fuchsia::sys::ServiceListPtr additional_services,
                     const std::vector<std::string>* service_allowlist)
    : Namespace(PrivateConstructor{}, nullptr, std::move(realm), std::move(additional_services),
                service_allowlist) {}

Namespace::Namespace(PrivateConstructor p, fxl::RefPtr<Namespace> parent, fxl::WeakPtr<Realm> realm,
                     fuchsia::sys::ServiceListPtr additional_services,
                     const std::vector<std::string>* service_allowlist)
    : vfs_(async_get_default_dispatcher()), weak_ptr_factory_(this), status_(Status::RUNNING) {
  fbl::RefPtr<LogConnectorImpl> connector;
  if (realm) {
    connector = realm->log_connector();
  }
  services_ = fbl::MakeRefCounted<ServiceProviderDirImpl>(connector, service_allowlist);
  job_provider_ = fbl::MakeRefCounted<JobProviderImpl>(realm.get());
  realm_ = std::move(realm);
  // WARNING! Do not add new services here! This makes services available in all
  // component namespaces ambiently without requiring proper routing between
  // realms, and this list should not be expanded.
  services_->AddService(
      fuchsia::sys::Environment::Name_,
      fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
        if (status_ == Status::RUNNING) {
          environment_bindings_.AddBinding(
              this, fidl::InterfaceRequest<fuchsia::sys::Environment>(std::move(channel)));
        }
        return ZX_OK;
      }));
  services_->AddService(
      Launcher::Name_, fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
        if (status_ == Status::RUNNING) {
          launcher_bindings_.AddBinding(this, fidl::InterfaceRequest<Launcher>(std::move(channel)));
        }
        return ZX_OK;
      }));
  services_->AddService(
      fuchsia::process::Launcher::Name_,
      fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
        if (realm_) {
          realm_->environment_services()->Connect(
              fidl::InterfaceRequest<fuchsia::process::Launcher>(std::move(channel)));
          return ZX_OK;
        }
        return ZX_ERR_BAD_STATE;
      }));
  services_->AddService(
      fuchsia::process::Resolver::Name_,
      fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
        if (realm_) {
          realm_->environment_services()->Connect(
              fidl::InterfaceRequest<fuchsia::process::Resolver>(std::move(channel)));
          return ZX_OK;
        }
        return ZX_ERR_BAD_STATE;
      }));

  // WARNING! Do not add new services here! This makes services available in all
  // component namespaces ambiently without requiring proper routing between
  // realms, and this list should not be expanded.

  if (additional_services) {
    auto& names = additional_services->names;
    service_provider_ = additional_services->provider.Bind();
    service_host_directory_ = std::move(additional_services->host_directory);
    for (auto& name : names) {
      if (service_host_directory_) {
        services_->AddService(name,
                              fbl::MakeRefCounted<fs::Service>([this, name](zx::channel channel) {
                                fdio_service_connect_at(service_host_directory_.channel().get(),
                                                        name.c_str(), channel.release());
                                return ZX_OK;
                              }));
      } else {
        services_->AddService(name,
                              fbl::MakeRefCounted<fs::Service>([this, name](zx::channel channel) {
                                service_provider_->ConnectToService(name, std::move(channel));
                                return ZX_OK;
                              }));
      }
    }
  }

  // If any services in |parent| share a name with |additional_services|,
  // |additional_services| takes priority.
  if (parent) {
    services_->set_parent(parent->services());
    parent_ = parent->weak_ptr_factory_.GetWeakPtr();
  }

  services_->InitLogging();
}

Namespace::~Namespace() {}

fxl::RefPtr<Namespace> Namespace::CreateChildNamespace(
    fxl::RefPtr<Namespace>& parent, fxl::WeakPtr<Realm> realm,
    fuchsia::sys::ServiceListPtr additional_services,
    const std::vector<std::string>* service_allowlist) {
  ZX_ASSERT(parent);
  if (parent->status_ != Status::RUNNING) {
    return nullptr;
  }
  fxl::RefPtr<Namespace> ns =
      fxl::MakeRefCounted<Namespace>(PrivateConstructor{}, parent, std::move(realm),
                                     std::move(additional_services), service_allowlist);
  parent->AddChild(ns);
  return ns;
}

void Namespace::AddChild(fxl::RefPtr<Namespace> child) {
  children_.emplace(child.get(), std::move(child));
}

void Namespace::AddBinding(fidl::InterfaceRequest<fuchsia::sys::Environment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void Namespace::CreateNestedEnvironment(
    fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
    fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> controller, std::string label,
    fuchsia::sys::ServiceListPtr additional_services, fuchsia::sys::EnvironmentOptions options) {
  if (realm_ && status_ == Status::RUNNING) {
    realm_->CreateNestedEnvironment(std::move(environment), std::move(controller), std::move(label),
                                    std::move(additional_services), options);
  }
}

void Namespace::GetLauncher(fidl::InterfaceRequest<Launcher> launcher) {
  launcher_bindings_.AddBinding(this, std::move(launcher));
}

void Namespace::GetServices(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services) {
  services_->AddBinding(std::move(services));
}

zx_status_t Namespace::ServeServiceDirectory(
    fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) {
  return vfs_.ServeDirectory(services_, directory_request.TakeChannel());
}

void Namespace::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  if (status_ != Status::RUNNING) {
    ComponentRequestWrapper component_request(std::move(controller));
    component_request.SetReturnValues(-1, fuchsia::sys::TerminationReason::REALM_SHUTTING_DOWN);
    return;
  }
  auto cc_trace_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("appmgr", "Namespace::CreateComponent", cc_trace_id, "launch_info.url",
                    launch_info.url);
  if (realm_) {
    realm_->CreateComponent(std::move(launch_info), std::move(controller),
                            [cc_trace_id](std::weak_ptr<ComponentControllerImpl> component) {
                              TRACE_ASYNC_END("appmgr", "Namespace::CreateComponent", cc_trace_id);
                            });
  } else {
    ComponentRequestWrapper component_request(std::move(controller));
    component_request.SetReturnValues(-1, fuchsia::sys::TerminationReason::REALM_SHUTTING_DOWN);
  }
}

fidl::InterfaceHandle<fuchsia::io::Directory> Namespace::OpenServicesAsDirectory() {
  return Util::OpenAsDirectory(&vfs_, services_);
}

void Namespace::NotifyComponentDiagnosticsDirReady(
    const std::string& component_url, const std::string& component_name,
    const std::string& component_id, fidl::InterfaceHandle<fuchsia::io::Directory> directory) {
  if (realm_) {
    realm_->NotifyComponentDiagnosticsDirReady(component_url, component_name, component_id,
                                               std::move(directory));
  }
}

void Namespace::NotifyComponentStopped(const std::string& component_url,
                                       const std::string& component_name,
                                       const std::string& component_id) {
  if (realm_) {
    realm_->NotifyComponentStopped(component_url, component_name, component_id);
  }
}

void Namespace::MaybeAddComponentEventProvider() {
  if (services_->IsServiceAllowlisted(fuchsia::sys::internal::ComponentEventProvider::Name_)) {
    services_->AddService(
        fuchsia::sys::internal::ComponentEventProvider::Name_,
        fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
          if (realm_) {
            return realm_->BindComponentEventProvider(
                fidl::InterfaceRequest<fuchsia::sys::internal::ComponentEventProvider>(
                    std::move(channel)));
          }
          return ZX_ERR_BAD_STATE;
        }));
  }
}

void Namespace::RunShutdownIfNoChildren(fxl::RefPtr<Namespace> ns) {
  if (ns->status_ == Status::SHUTTING_DOWN && ns->children_.empty()) {
    ns->status_ = Status::STOPPING;
    ns->vfs_.CloseAllConnectionsForVnode(*ns->services_, [ns]() {
      ns->vfs_.Shutdown([ns](zx_status_t /*unused*/) mutable {
        ns->status_ = Status::STOPPED;
        if (ns->parent_) {
          bool ret = ns->parent_->RemoveChild(ns.get());
          FX_DCHECK(ret);
        }
        for (auto& callback : ns->shutdown_callbacks_) {
          async::PostTask(async_get_default_dispatcher(),
                          [callback = std::move(callback)]() mutable { callback(); });
        }
        ns->shutdown_callbacks_.clear();
        ns.reset();
      });
    });
  }
}

bool Namespace::RemoveChild(Namespace* child) { return children_.erase(child) == 1; }

void Namespace::FlushAndShutdown(fxl::RefPtr<Namespace> self,
                                 fs::ManagedVfs::CloseAllConnectionsForVnodeCallback callback) {
  ZX_ASSERT(self.get() == this);
  switch (status_) {
    case Status::SHUTTING_DOWN:
    case Status::STOPPING:
      // We are already stopping/shutting down. Store callback and return.
      if (callback) {
        shutdown_callbacks_.push_back(std::move(callback));
      }
      return;
    case Status::STOPPED:
      if (callback) {
        async::PostTask(async_get_default_dispatcher(),
                        [self, callback = std::move(callback)]() mutable { callback(); });
      }
      return;
    case Status::RUNNING:
      if (callback) {
        shutdown_callbacks_.push_back(std::move(callback));
      }
      break;
  }
  status_ = Status::SHUTTING_DOWN;
  environment_bindings_.CloseAll();
  launcher_bindings_.CloseAll();
  if (children_.empty()) {
    RunShutdownIfNoChildren(std::move(self));
    return;
  }

  for (auto& child : children_) {
    async::PostTask(async_get_default_dispatcher(), [ns = child.second, self]() {
      ns->FlushAndShutdown(ns, [ptr = ns.get(), self]() { RunShutdownIfNoChildren(self); });
    });
  }
}

}  // namespace component
