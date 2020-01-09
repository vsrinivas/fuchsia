// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_COMPONENT_CONTROLLER_IMPL_H_
#define SRC_SYS_APPMGR_COMPONENT_CONTROLLER_IMPL_H_

#include <fuchsia/inspect/deprecated/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/promise.h>
#include <lib/zx/process.h>
#include <zircon/assert.h>

#include <vector>

#include <fs/pseudo_dir.h>

#include "lib/fidl/cpp/binding.h"
#include "src/sys/appmgr/component_container.h"
#include "src/sys/appmgr/debug_info_retriever.h"
#include "src/sys/appmgr/hub/component_hub.h"
#include "src/sys/appmgr/hub/hub_info.h"
#include "src/sys/appmgr/namespace.h"
#include "src/sys/appmgr/system_objects_directory.h"

namespace component {

// ComponentRequestWrapper wraps failure behavior in the event a Component fails
// to start. It wraps the behavior of binding to an incoming interface request
// and sending error events to clients before closing the channel.
// If there is no error, the wrapped request and callback may be Extract()ed
// and bound to a concrete interface.
// TODO(CP-84): Solve the general problem this solves.
class ComponentRequestWrapper {
 public:
  explicit ComponentRequestWrapper(
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
      int64_t default_return = -1,
      fuchsia::sys::TerminationReason default_reason = fuchsia::sys::TerminationReason::UNKNOWN);
  ~ComponentRequestWrapper();
  ComponentRequestWrapper(ComponentRequestWrapper&& other);
  void operator=(ComponentRequestWrapper&& other);

  void SetReturnValues(int64_t return_code, fuchsia::sys::TerminationReason reason);

  bool Extract(fidl::InterfaceRequest<fuchsia::sys::ComponentController>* out_request) {
    if (!request_.is_valid()) {
      return false;
    }
    *out_request = std::move(request_);
    return true;
  }

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ComponentRequestWrapper);

 private:
  fidl::InterfaceRequest<fuchsia::sys::ComponentController> request_;
  int64_t return_code_;
  fuchsia::sys::TerminationReason reason_;
};

// FailedComponentController implements the component controller interface for
// components that failed to start. This class serves the purpose of actually
// binding to a ComponentController channel and passing back a termination
// event.
class FailedComponentController final : public fuchsia::sys::ComponentController {
 public:
  FailedComponentController(int64_t return_code, fuchsia::sys::TerminationReason termination_reason,
                            fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller);
  ~FailedComponentController() override;
  void Kill() override;
  void Detach() override;

 private:
  fidl::Binding<fuchsia::sys::ComponentController> binding_;
  int64_t return_code_;
  fuchsia::sys::TerminationReason termination_reason_;
};

class ComponentControllerBase : public fuchsia::sys::ComponentController {
 public:
  ComponentControllerBase(fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
                          std::string url, std::string args, std::string label,
                          std::string hub_instance_id, fxl::RefPtr<Namespace> ns,
                          zx::channel exported_dir, zx::channel client_request);
  virtual ~ComponentControllerBase() override;

 public:
  HubInfo HubInfo();
  const std::string& label() const { return label_; }
  const fbl::RefPtr<fs::PseudoDir>& hub_dir() const { return hub_.dir(); }

  // The instance ID (process koid) of the component in the hub.
  const std::string& hub_instance_id() const { return hub_instance_id_; }

  // The url of this component.
  const std::string& url() const { return url_; }

  // |fuchsia::sys::ComponentController| implementation:
  void Detach() override;

  // Provides a handle to the component out/diagnostics directory if one exists.
  fit::promise<fidl::InterfaceHandle<fuchsia::io::Directory>, zx_status_t> GetDiagnosticsDir();

 protected:
  ComponentHub* hub() { return &hub_; }

  // Returns the incoming services from the namespace.
  const fbl::RefPtr<ServiceProviderDirImpl>& incoming_services() const {
    ZX_DEBUG_ASSERT(ns_);
    return ns_->services();
  };

  void SendOnDirectoryReadyEvent();

  void SendOnTerminationEvent(int64_t, fuchsia::sys::TerminationReason);

 private:
  // Notifies a realm's ComponentEventListener with the out/diagnostics directory for a component.
  void NotifyDiagnosticsDirReady();

  async::Executor executor_;

  fidl::Binding<fuchsia::sys::ComponentController> binding_;

  // The name of this component: e.g., my_component.cmx
  std::string label_;

  // The instance id of this component in the hub (process koid)
  std::string hub_instance_id_;

  // The url of this component: e.g., fuchsia-pkg://fuchsia.com/my_package#meta/my_component.cmx
  std::string url_;

  ComponentHub hub_;

  fxl::RefPtr<Namespace> ns_;

  fxl::WeakPtrFactory<ComponentControllerBase> weak_ptr_factory_;

  fuchsia::io::NodePtr cloned_exported_dir_;

  fuchsia::io::DirectoryPtr exported_dir_;

  fuchsia::inspect::deprecated::InspectPtr inspect_checker_;

  // guards against sending this event two times
  bool on_terminated_event_sent_ = false;

  // whether the out directory is ready or not.
  bool out_ready_ = false;
};

class ComponentControllerImpl : public ComponentControllerBase {
 public:
  ComponentControllerImpl(fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
                          ComponentContainer<ComponentControllerImpl>* container, zx::job job,
                          zx::process process, std::string url, std::string args, std::string label,
                          fxl::RefPtr<Namespace> ns, zx::channel exported_dir,
                          zx::channel client_request);
  ~ComponentControllerImpl() override;

  const std::string& koid() const { return koid_; }

  zx_status_t AddSubComponentHub(const component::HubInfo& hub_info);
  zx_status_t RemoveSubComponentHub(const component::HubInfo& hub_info);

  // |fuchsia::sys::ComponentController| implementation:
  void Kill() override;

 private:
  void Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
               const zx_packet_signal* signal);

  bool SendReturnCodeIfTerminated();

  ComponentContainer<ComponentControllerImpl>* container_;
  zx::job job_;
  zx::process process_;
  const std::string koid_;

  async::WaitMethod<ComponentControllerImpl, &ComponentControllerImpl::Handler> wait_;

  SystemObjectsDirectory system_objects_directory_;

  fidl::BindingSet<fuchsia::inspect::deprecated::Inspect, std::shared_ptr<component::Object>>
      system_directory_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentControllerImpl);
};

// This class acts as a bridge between the components created by ComponentRunner
// and |request|.
class ComponentBridge : public ComponentControllerBase {
 public:
  ComponentBridge(fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
                  fuchsia::sys::ComponentControllerPtr remote_controller,
                  ComponentContainer<ComponentBridge>* container, std::string url, std::string args,
                  std::string label, std::string hub_instance_id, fxl::RefPtr<Namespace> ns,
                  zx::channel exported_dir, zx::channel client_request);

  ~ComponentBridge() override;

  void SetParentJobId(const std::string& id);

  // Set the termination reason for this bridge.
  // This should be used when a runner itself terminates and needs to report
  // back a failure over the bridge when it is closed.
  void SetTerminationReason(fuchsia::sys::TerminationReason termination_reason);

  // |fuchsia::sys::ComponentController| implementation:
  void Kill() override;

  void OnTerminated(OnTerminatedCallback callback) { on_terminated_event_ = std::move(callback); }

 private:
  fuchsia::sys::ComponentControllerPtr remote_controller_;
  ComponentContainer<ComponentBridge>* container_;
  fuchsia::sys::TerminationReason termination_reason_;
  OnTerminatedCallback on_terminated_event_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentBridge);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_COMPONENT_CONTROLLER_IMPL_H_
