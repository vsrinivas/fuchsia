// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"

#include <fbl/string_printf.h>
#include <fdio/util.h>
#include <fs/pseudo-file.h>
#include <fs/remote-dir.h>
#include <lib/async/default.h>

#include <cinttypes>
#include <utility>

#include "garnet/bin/appmgr/namespace.h"
#include "garnet/bin/appmgr/realm.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/functional/closure.h"

namespace component {

ComponentControllerImpl::ComponentControllerImpl(
    fidl::InterfaceRequest<ComponentController> request, Realm* realm,
    std::unique_ptr<archive::FileSystem> fs, zx::process process,
    std::string url, std::string args, std::string label,
    fxl::RefPtr<Namespace> ns, ExportedDirType export_dir_type,
    zx::channel exported_dir, zx::channel client_request)
    : binding_(this),
      realm_(realm),
      fs_(std::move(fs)),
      process_(std::move(process)),
      label_(std::move(label)),
      koid_(std::to_string(fsl::GetKoid(process_.get()))),
      hub_(fbl::AdoptRef(new fs::PseudoDir())),
      exported_dir_(std::move(exported_dir)),
      ns_(std::move(ns)),
      wait_(this, process_.get(), ZX_TASK_TERMINATED) {
  zx_status_t status = wait_.Begin(async_get_default());
  FXL_DCHECK(status == ZX_OK);
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
  hub_.SetJobId(realm_->koid());
  hub_.SetProcessId(koid_);
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

ComponentControllerImpl::~ComponentControllerImpl() {
  // Two ways we end up here:
  // 1) OnHandleReady() destroys this object; in which case, process is dead.
  // 2) Our owner destroys this object; in which case, the process may still be
  //    alive.
  if (process_)
    process_.kill();
}

HubInfo ComponentControllerImpl::HubInfo() {
  return component::HubInfo(label_, koid_, hub_.dir());
}

void ComponentControllerImpl::Kill() { process_.kill(); }

void ComponentControllerImpl::Detach() {
  binding_.set_error_handler(fxl::Closure());
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
  wait_callbacks_.push_back(callback);
  SendReturnCodeIfTerminated();
}

// Called when process terminates, regardless of if Kill() was invoked.
void ComponentControllerImpl::Handler(async_t* async, async::WaitBase* wait,
                                      zx_status_t status,
                                      const zx_packet_signal* signal) {
  FXL_DCHECK(status == ZX_OK);
  FXL_DCHECK(signal->observed == ZX_TASK_TERMINATED);
  if (!wait_callbacks_.empty()) {
    bool terminated = SendReturnCodeIfTerminated();
    FXL_DCHECK(terminated);
  }

  process_.reset();

  realm_->ExtractApplication(this);
  // The destructor of the temporary returned by ExtractApplication destroys
  // |this| at the end of the previous statement.
}

}  // namespace component
