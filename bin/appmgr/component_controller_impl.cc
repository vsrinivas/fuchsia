// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"

#include <fbl/string_printf.h>
#include <fs/pseudo-file.h>
#include <fs/remote-dir.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include <lib/fit/function.h>

#include <cinttypes>
#include <utility>

#include "garnet/bin/appmgr/component_container.h"
#include "garnet/bin/appmgr/namespace.h"
#include "lib/fsl/handles/object_info.h"

namespace component {

ComponentControllerBase::ComponentControllerBase(
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
    std::string url, std::string args, std::string label,
    std::string hub_instance_id, fxl::RefPtr<Namespace> ns,
    ExportedDirType export_dir_type, zx::channel exported_dir,
    zx::channel client_request)
    : binding_(this),
      label_(std::move(label)),
      hub_instance_id_(std::move(hub_instance_id)),
      hub_(fbl::AdoptRef(new fs::PseudoDir())),
      exported_dir_(std::move(exported_dir)),
      ns_(std::move(ns)) {
  if (request.is_valid()) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this] { Kill(); });
  }

  if (!exported_dir_) {
    return;
  }

  if (client_request) {
    if (export_dir_type == ExportedDirType::kPublicDebugCtrlLayout) {
      fdio_service_connect_at(exported_dir_.get(), "public",
                              client_request.release());
    } else if (export_dir_type == ExportedDirType::kLegacyFlatLayout) {
      fdio_service_clone_to(exported_dir_.get(), client_request.release());
    }
  }

  hub_.SetName(label_);
  hub_.AddEntry("url", fbl::move(url));
  hub_.AddEntry("args", fbl::move(args));
  if (export_dir_type == ExportedDirType::kPublicDebugCtrlLayout) {
    zx_handle_t dir_client = fdio_service_clone(exported_dir_.get());
    if (dir_client == ZX_HANDLE_INVALID) {
      FXL_LOG(ERROR) << "Failed to clone exported directory.";
    } else {
      zx::channel chan(std::move(dir_client));
      hub_.PublishOut(fbl::AdoptRef(new fs::RemoteDir(fbl::move(chan))));
    }
  }
}

ComponentControllerBase::~ComponentControllerBase() {}

HubInfo ComponentControllerBase::HubInfo() {
  return component::HubInfo(label_, hub_instance_id_, hub_.dir());
}

void ComponentControllerBase::Detach() { binding_.set_error_handler(nullptr); }

ComponentControllerImpl::ComponentControllerImpl(
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
    ComponentContainer<ComponentControllerImpl>* container, zx::job job,
    zx::process process, std::string url, std::string args, std::string label,
    fxl::RefPtr<Namespace> ns, ExportedDirType export_dir_type,
    zx::channel exported_dir, zx::channel client_request)
    : ComponentControllerBase(
          std::move(request), std::move(url), std::move(args), std::move(label),
          std::to_string(fsl::GetKoid(process.get())), std::move(ns),
          std::move(export_dir_type), std::move(exported_dir),
          std::move(client_request)),
      container_(container),
      job_(std::move(job)),
      process_(std::move(process)),
      koid_(std::to_string(fsl::GetKoid(process_.get()))),
      wait_(this, process_.get(), ZX_TASK_TERMINATED) {
  zx_status_t status = wait_.Begin(async_get_default_dispatcher());
  FXL_DCHECK(status == ZX_OK);

  hub()->SetJobId(std::to_string(fsl::GetKoid(job_.get())));
  hub()->SetProcessId(koid_);
}

ComponentControllerImpl::~ComponentControllerImpl() {
  // Two ways we end up here:
  // 1) OnHandleReady() destroys this object; in which case, process is dead.
  // 2) Our owner destroys this object; in which case, the process may still be
  //    alive.
  if (job_)
    job_.kill();
}

void ComponentControllerImpl::Kill() {
  if (job_) {
    job_.kill();
    job_.reset();
  }
}

bool ComponentControllerImpl::SendReturnCodeIfTerminated() {
  // Get process info.
  zx_info_process_t process_info;
  zx_status_t result = process_.get_info(ZX_INFO_PROCESS, &process_info,
                                         sizeof(process_info), NULL, NULL);
  FXL_DCHECK(result == ZX_OK);

  if (process_info.exited) {
    // If the process has exited, call the callbacks.
    for (const auto& iter : wait_callbacks_) {
      iter(process_info.return_code);
    }
    wait_callbacks_.clear();
  }

  return process_info.exited;
}

void ComponentControllerImpl::Wait(WaitCallback callback) {
  wait_callbacks_.push_back(std::move(callback));
  SendReturnCodeIfTerminated();
}

zx_status_t ComponentControllerImpl::AddSubComponentHub(
    const component::HubInfo& hub_info) {
  hub()->EnsureComponentDir();
  return hub()->AddComponent(hub_info);
}

zx_status_t ComponentControllerImpl::RemoveSubComponentHub(
    const component::HubInfo& hub_info) {
  return hub()->RemoveComponent(hub_info);
}

// Called when process terminates, regardless of if Kill() was invoked.
void ComponentControllerImpl::Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                      zx_status_t status,
                                      const zx_packet_signal* signal) {
  FXL_DCHECK(status == ZX_OK);
  FXL_DCHECK(signal->observed == ZX_TASK_TERMINATED);
  if (!wait_callbacks_.empty()) {
    bool terminated = SendReturnCodeIfTerminated();
    FXL_DCHECK(terminated);
  }

  process_.reset();

  container_->ExtractComponent(this);
  // The destructor of the temporary returned by ExtractComponent destroys
  // |this| at the end of the previous statement.
}

ComponentBridge::ComponentBridge(
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
    fuchsia::sys::ComponentControllerPtr remote_controller,
    ComponentContainer<ComponentBridge>* container, std::string url,
    std::string args, std::string label, std::string hub_instance_id,
    fxl::RefPtr<Namespace> ns, ExportedDirType export_dir_type,
    zx::channel exported_dir, zx::channel client_request)
    : ComponentControllerBase(
          std::move(request), std::move(url), std::move(args), std::move(label),
          hub_instance_id, std::move(ns), std::move(export_dir_type),
          std::move(exported_dir), std::move(client_request)),
      remote_controller_(std::move(remote_controller)),
      container_(std::move(container)) {
  remote_controller_.set_error_handler(
      [this] { container_->ExtractComponent(this); });
  // The destructor of the temporary returned by ExtractComponent destroys
  // |this| at the end of the previous statement.
}

ComponentBridge::~ComponentBridge() {}

void ComponentBridge::SetParentJobId(const std::string& id) {
  hub()->SetJobId(id);
}

void ComponentBridge::Kill() { remote_controller_->Kill(); }

void ComponentBridge::Wait(WaitCallback callback) {
  remote_controller_->Wait(std::move(callback));
}

}  // namespace component
