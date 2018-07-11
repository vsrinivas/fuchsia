// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_COMPONENT_CONTROLLER_IMPL_H_
#define GARNET_BIN_APPMGR_COMPONENT_CONTROLLER_IMPL_H_

#include <fs/pseudo-dir.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/process.h>

#include "garnet/bin/appmgr/component_container.h"
#include "garnet/bin/appmgr/hub/component_hub.h"
#include "garnet/bin/appmgr/hub/hub_info.h"
#include "garnet/bin/appmgr/namespace.h"

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace component {

enum class ExportedDirType {
  // Legacy exported directory layout where each file / service is exposed at
  // the top level. Appmgr forwards a client's
  // |fuchsia::sys::LaunchInfo.directory_request| to the top level directory.
  kLegacyFlatLayout,

  // A nested directory structure where appmgr expects 3 sub-directories-
  // (1) public - A client's |fuchsia::sys::LaunchInfo.directory_request| is
  // forwarded to this directory.
  // (2) debug - This directory is used to expose debug files.
  // (3) ctrl - This deirectory is used to expose files to the system.
  kPublicDebugCtrlLayout,
};

typedef fit::function<void(int64_t, fuchsia::sys::TerminationReason,
                           fuchsia::sys::ComponentController_EventSender*)>
    TerminationCallback;

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
      TerminationCallback callback, int64_t default_return = -1,
      fuchsia::sys::TerminationReason default_reason =
          fuchsia::sys::TerminationReason::UNKNOWN);
  ~ComponentRequestWrapper();
  ComponentRequestWrapper(ComponentRequestWrapper&& other);
  void operator=(ComponentRequestWrapper&& other);

  void SetReturnValues(int64_t return_code,
                       fuchsia::sys::TerminationReason reason);

  bool Extract(
      fidl::InterfaceRequest<fuchsia::sys::ComponentController>* out_request,
      TerminationCallback* out_callback) {
    if (!active_) {
      return false;
    }
    *out_request = std::move(request_);
    *out_callback = std::move(callback_);
    active_ = false;
    return true;
  }

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ComponentRequestWrapper);

 private:
  fidl::InterfaceRequest<fuchsia::sys::ComponentController> request_;
  TerminationCallback callback_;
  int64_t return_code_;
  fuchsia::sys::TerminationReason reason_;
  bool active_ = true;
};

// Construct a callback that forwards termination information back over an
// incoming ComponentController_EventSender , if it exists.
TerminationCallback MakeForwardingTerminationCallback();

// FailedComponentController implements the component controller interface for
// components that failed to start. This class serves the purpose of actually
// binding to a ComponentController channel and passing back a termination
// event.
class FailedComponentController : public fuchsia::sys::ComponentController {
 public:
  FailedComponentController(
      int64_t return_code, fuchsia::sys::TerminationReason termination_reason,
      TerminationCallback termination_callback,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller);
  virtual ~FailedComponentController();
  void Wait(WaitCallback callback) override;
  void Kill() override;
  void Detach() override;

 private:
  fidl::Binding<fuchsia::sys::ComponentController> binding_;
  int64_t return_code_;
  fuchsia::sys::TerminationReason termination_reason_;
  TerminationCallback termination_callback_;
};

class ComponentControllerBase : public fuchsia::sys::ComponentController {
 public:
  ComponentControllerBase(
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
      std::string url, std::string args, std::string label,
      std::string hub_instance_id, fxl::RefPtr<Namespace> ns,
      ExportedDirType export_dir_type, zx::channel exported_dir,
      zx::channel client_request);
  virtual ~ComponentControllerBase() override;

 public:
  HubInfo HubInfo();
  const std::string& label() const { return label_; }
  const fbl::RefPtr<fs::PseudoDir>& hub_dir() const { return hub_.dir(); }

  // |fuchsia::sys::ComponentController| implementation:
  void Detach() override;

 protected:
  ComponentHub* hub() { return &hub_; }
  fidl::Binding<fuchsia::sys::ComponentController> binding_;

 private:
  std::string label_;
  std::string hub_instance_id_;

  ComponentHub hub_;

  zx::channel exported_dir_;

  fxl::RefPtr<Namespace> ns_;
};

class ComponentControllerImpl : public ComponentControllerBase {
 public:
  ComponentControllerImpl(
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
      ComponentContainer<ComponentControllerImpl>* container, zx::job job,
      zx::process process, std::string url, std::string args, std::string label,
      fxl::RefPtr<Namespace> ns, ExportedDirType export_dir_type,
      zx::channel exported_dir, zx::channel client_request,
      TerminationCallback termination_callback);
  ~ComponentControllerImpl() override;

  const std::string& koid() const { return koid_; }

  zx_status_t AddSubComponentHub(const component::HubInfo& hub_info);
  zx_status_t RemoveSubComponentHub(const component::HubInfo& hub_info);

  // |fuchsia::sys::ComponentController| implementation:
  void Kill() override;
  void Wait(WaitCallback callback) override;

 private:
  void Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
               zx_status_t status, const zx_packet_signal* signal);

  bool SendReturnCodeIfTerminated();

  ComponentContainer<ComponentControllerImpl>* container_;
  zx::job job_;
  zx::process process_;
  const std::string koid_;
  std::vector<WaitCallback> wait_callbacks_;

  async::WaitMethod<ComponentControllerImpl, &ComponentControllerImpl::Handler>
      wait_;

  TerminationCallback termination_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentControllerImpl);
};

// This class acts as a bridge between the components created by ComponentRunner
// and |request|.
class ComponentBridge : public ComponentControllerBase {
 public:
  ComponentBridge(
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
      fuchsia::sys::ComponentControllerPtr remote_controller,
      ComponentContainer<ComponentBridge>* container, std::string url,
      std::string args, std::string label, std::string hub_instance_id,
      fxl::RefPtr<Namespace> ns, ExportedDirType export_dir_type,
      zx::channel exported_dir, zx::channel client_request,
      TerminationCallback termination_callback);

  ~ComponentBridge() override;

  void SetParentJobId(const std::string& id);

  // Set the termination reason for this bridge.
  // This should be used when a runner itself terminates and needs to report
  // back a failure over the bridge when it is closed.
  void SetTerminationReason(fuchsia::sys::TerminationReason termination_reason);

  // |fuchsia::sys::ComponentController| implementation:
  void Kill() override;
  void Wait(WaitCallback callback) override;

 private:
  fuchsia::sys::ComponentControllerPtr remote_controller_;
  ComponentContainer<ComponentBridge>* container_;
  TerminationCallback termination_callback_;
  fuchsia::sys::TerminationReason termination_reason_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentBridge);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_COMPONENT_CONTROLLER_IMPL_H_
