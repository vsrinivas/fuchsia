// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_
#define SRC_SYS_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <fs/vfs_types.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/sys/appmgr/log_connector_impl.h"
#include "src/sys/appmgr/moniker.h"

namespace component {

constexpr char kUnknownComponent[] = "<unknown v2 component>";

// A directory-like object which dynamically creates Service vnodes
// for any file lookup. It also exposes service provider interface.
//
// It supports enumeration for only first level of services.
class ServiceProviderDirImpl : public fuchsia::sys::ServiceProvider, public fs::Vnode {
 public:
  explicit ServiceProviderDirImpl(fbl::RefPtr<LogConnectorImpl> log_connector,
                                  const std::vector<std::string>* services = nullptr);
  ~ServiceProviderDirImpl() override;

  // Sets the parent of this. Parent should be fully initialized.
  void set_parent(fbl::RefPtr<ServiceProviderDirImpl> parent);

  const std::string& component_url() const { return component_url_; }

  const std::string& component_moniker() const { return component_moniker_; }

  void set_component_moniker(const Moniker& moniker) {
    component_moniker_ = moniker.ToString();
    component_url_ = moniker.url;
  }
  void set_component_id(const std::string& id) { component_id_ = id; }

  void AddService(const std::string& service_name, fbl::RefPtr<fs::Service> service);

  void AddBinding(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request);

  //
  // Overridden from |fs::Vnode|:
  //

  fs::VnodeProtocolSet GetProtocols() const final;

  zx_status_t Lookup(fbl::StringPiece name, fbl::RefPtr<fs::Vnode>* out) final;

  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;

  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                      size_t* out_actual) final;

  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* representation) final;

  //
  // Overridden from |fuchsia::sys::ServiceProvider|:
  //

  void ConnectToService(std::string service_name, zx::channel channel) override;

  // Initialize LogConnector and LogSink services if needed. Should be called *after*
  // other namespace setup steps so that parent-provided versions of those services will
  // take precedence.
  void InitLogging();

  bool IsServiceAllowlisted(const std::string& service_name) {
    return (!has_services_allowlist_ || services_allowlist_.count(service_name) > 0);
  }

 private:
  fidl::BindingSet<fuchsia::sys::ServiceProvider> bindings_;
  fs::SynchronousVfs vfs_;
  // |root_| has all services offered by this provider (including those
  // inherited from the parent, if any).
  fbl::RefPtr<fs::PseudoDir> root_;
  fbl::RefPtr<ServiceProviderDirImpl> parent_;
  fbl::RefPtr<LogConnectorImpl> log_connector_;
  fxl::WeakPtrFactory<ServiceProviderDirImpl> weak_factory_;

  bool has_builtin_logsink_ = false;

  // TODO(fxbug.dev/3924): Remove has_services_allowlist_ when empty services is
  // equivalent to no services.
  bool has_services_allowlist_ = false;
  std::unordered_set<std::string> services_allowlist_;
  // Secondary storage for services under |root_| in a format that can easily
  // be consumed by children. Stored as vector to preserve order.
  typedef std::pair<std::string, fbl::RefPtr<fs::Service>> ServiceHandle;
  std::vector<ServiceHandle> service_handles_;
  std::unordered_set<std::string> all_service_names_;

  std::string component_moniker_ = kUnknownComponent;
  std::string component_url_ = kUnknownComponent;
  std::string component_id_ = "-1";

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderDirImpl);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_
