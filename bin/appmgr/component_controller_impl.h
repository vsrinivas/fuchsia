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

class ComponentControllerBase : public fuchsia::sys::ComponentController {
 public:
  ComponentControllerBase(
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
      std::string url, std::string args, std::string label,
      std::string hub_instance_id, fxl::RefPtr<Namespace> ns,
      ExportedDirType export_dir_type, zx::channel exported_dir,
      zx::channel client_request);
  ~ComponentControllerBase() override;

 public:
  HubInfo HubInfo();
  const std::string& label() const { return label_; }
  const fbl::RefPtr<fs::PseudoDir>& hub_dir() const { return hub_.dir(); }

  // |fuchsia::sys::ComponentController| implementation:
  void Detach() override;

 protected:
  ComponentHub* hub() { return &hub_; }

 private:
  fidl::Binding<fuchsia::sys::ComponentController> binding_;
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
      ComponentContainer<ComponentControllerImpl>* container,
      zx::job job, zx::process process, std::string url, std::string args,
      std::string label, fxl::RefPtr<Namespace> ns, ExportedDirType
      export_dir_type, zx::channel exported_dir, zx::channel client_request);
  ~ComponentControllerImpl() override;

  const std::string& koid() const { return koid_; }

  zx_status_t AddSubComponentHub(const component::HubInfo& hub_info);
  zx_status_t RemoveSubComponentHub(const component::HubInfo& hub_info);

  // |fuchsia::sys::ComponentController| implementation:
  void Kill() override;
  void Wait(WaitCallback callback) override;

 private:
  void Handler(async_t* async, async::WaitBase* wait, zx_status_t status,
               const zx_packet_signal* signal);

  bool SendReturnCodeIfTerminated();

  ComponentContainer<ComponentControllerImpl>* container_;
  zx::job job_;
  zx::process process_;
  const std::string koid_;
  std::vector<WaitCallback> wait_callbacks_;

  async::WaitMethod<ComponentControllerImpl, &ComponentControllerImpl::Handler>
      wait_;

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
      zx::channel exported_dir, zx::channel client_request);

  ~ComponentBridge() override;

  void SetParentJobId(const std::string& id);

  // |fuchsia::sys::ComponentController| implementation:
  void Kill() override;
  void Wait(WaitCallback callback) override;

 private:
  fuchsia::sys::ComponentControllerPtr remote_controller_;
  ComponentContainer<ComponentBridge>* container_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentBridge);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_COMPONENT_CONTROLLER_IMPL_H_
