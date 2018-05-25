// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_COMPONENT_CONTROLLER_IMPL_H_
#define GARNET_BIN_APPMGR_COMPONENT_CONTROLLER_IMPL_H_

#include <component/cpp/fidl.h>
#include <fs/pseudo-dir.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/process.h>

#include "garnet/bin/appmgr/hub/component_hub.h"
#include "garnet/bin/appmgr/hub/hub_info.h"
#include "garnet/bin/appmgr/namespace.h"
#include "garnet/lib/farfs/file_system.h"

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace component {
class Realm;

enum class ExportedDirType {
  // Legacy exported directory layout where each file / service is exposed at
  // the top level. Appmgr forwards a client's
  // |LaunchInfo.directory_request| to the top level directory.
  kLegacyFlatLayout,

  // A nested directory structure where appmgr expects 3 sub-directories-
  // (1) public - A client's |LaunchInfo.directory_request| is
  // forwarded to this directory.
  // (2) debug - This directory is used to expose debug files.
  // (3) ctrl - This deirectory is used to expose files to the system.
  kPublicDebugCtrlLayout,
};

class ComponentControllerImpl : public ComponentController {
 public:
  ComponentControllerImpl(fidl::InterfaceRequest<ComponentController> request,
                          Realm* realm, std::unique_ptr<archive::FileSystem> fs,
                          zx::process process, std::string url,
                          std::string args, std::string label,
                          fxl::RefPtr<Namespace> ns,
                          ExportedDirType export_dir_type,
                          zx::channel exported_dir, zx::channel client_request);
  ~ComponentControllerImpl() override;

  HubInfo HubInfo();

  const std::string& label() const { return label_; }
  const std::string& koid() const { return koid_; }
  const fbl::RefPtr<fs::PseudoDir>& hub_dir() const { return hub_.dir(); }

  // |ComponentController| implementation:
  void Kill() override;
  void Detach() override;
  void Wait(WaitCallback callback) override;

 private:
  void Handler(async_t* async, async::WaitBase* wait, zx_status_t status,
               const zx_packet_signal* signal);

  bool SendReturnCodeIfTerminated();

  fidl::Binding<ComponentController> binding_;
  Realm* realm_;
  std::unique_ptr<archive::FileSystem> fs_;
  zx::process process_;
  std::string label_;
  const std::string koid_;
  std::vector<WaitCallback> wait_callbacks_;
  ComponentHub hub_;

  zx::channel exported_dir_;

  fxl::RefPtr<Namespace> ns_;

  async::WaitMethod<ComponentControllerImpl, &ComponentControllerImpl::Handler>
      wait_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentControllerImpl);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_COMPONENT_CONTROLLER_IMPL_H_
