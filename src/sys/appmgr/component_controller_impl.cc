// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/component_controller_impl.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/job.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cinttypes>
#include <string>
#include <utility>

#include <fbl/string_printf.h>
#include <fs/pseudo_file.h>
#include <fs/remote_dir.h>
#include <fs/service.h>
#include <task-utils/walker.h>

#include "fbl/ref_ptr.h"
#include "lib/inspect/service/cpp/service.h"
#include "lib/vfs/cpp/service.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/sys/appmgr/component_container.h"
#include "src/sys/appmgr/namespace.h"
#include "src/sys/appmgr/realm.h"

namespace component {

using fuchsia::sys::TerminationReason;

namespace {
zx::process DuplicateProcess(const zx::process& process) {
  zx::process ret;
  zx_status_t status = process.duplicate(ZX_RIGHT_SAME_RIGHTS, &ret);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate process handle: " << status;
  }
  return ret;
}

// TODO(fxbug.dev/46803): The out/diagnostics directory propagation for runners includes a retry.
// The reason of this is that flutter fills the out/ directory *after*
// serving it. Therefore we need to watch that directory to notify.
// Sadly the PseudoDir exposed in the SDK (and used by flutter) returns ZX_ERR_NOT_SUPPORTED on
// Watch. A solution using a watcher is implemented in fxr/366977 pending watch support.
const uint32_t MAX_RETRIES_OUT_DIAGNOSTICS = 30;
const uint32_t OUT_DIAGNOSTICS_RETRY_DELAY_MS = 500;

}  // namespace

ComponentRequestWrapper::ComponentRequestWrapper(
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request, int64_t default_return,
    fuchsia::sys::TerminationReason default_reason)
    : request_(std::move(request)), return_code_(default_return), reason_(default_reason) {}

ComponentRequestWrapper::~ComponentRequestWrapper() {
  if (request_) {
    FailedComponentController failed(return_code_, reason_, std::move(request_));
  }
}

ComponentRequestWrapper::ComponentRequestWrapper(ComponentRequestWrapper&& other) {
  *this = std::move(other);
}

void ComponentRequestWrapper::operator=(ComponentRequestWrapper&& other) {
  request_ = std::move(other.request_);
  return_code_ = std::move(other.return_code_);
  reason_ = std::move(other.reason_);
}

void ComponentRequestWrapper::SetReturnValues(int64_t return_code, TerminationReason reason) {
  return_code_ = return_code;
  reason_ = reason;
}

FailedComponentController::FailedComponentController(
    int64_t return_code, TerminationReason termination_reason,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
    : binding_(this), return_code_(return_code), termination_reason_(termination_reason) {
  if (controller) {
    binding_.Bind(std::move(controller));
  }
}

FailedComponentController::~FailedComponentController() {
  // This can be false if other side of the channel dies before this object dies.
  if (binding_.is_bound()) {
    binding_.events().OnTerminated(return_code_, termination_reason_);
  }
}

void FailedComponentController::Kill() {}

void FailedComponentController::Detach() {}

ComponentControllerBase::ComponentControllerBase(
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request, std::string url,
    std::string args, std::string label, std::string hub_instance_id, fxl::RefPtr<Namespace> ns,
    zx::channel exported_dir, zx::channel client_request, uint32_t diagnostics_max_retries)
    : executor_(async_get_default_dispatcher()),
      binding_(this),
      label_(std::move(label)),
      hub_instance_id_(std::move(hub_instance_id)),
      url_(std::move(url)),
      hub_(fbl::AdoptRef(new fs::PseudoDir())),
      ns_(std::move(ns)),
      weak_ptr_factory_(this),
      diagnostics_max_retries_(diagnostics_max_retries) {
  if (request.is_valid()) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this](zx_status_t /*status*/) { Kill(); });
  }
  if (!exported_dir) {
    return;
  }
  exported_dir_.Bind(std::move(exported_dir), async_get_default_dispatcher());

  if (client_request) {
    fdio_service_connect_at(exported_dir_.channel().get(), "svc", client_request.release());
  }

  ns_->set_component_id(hub_instance_id_);
  hub_.SetName(label_);
  hub_.AddEntry("url", url_);
  hub_.AddEntry("args", std::move(args));
  exported_dir_->Clone(fuchsia::io::OPEN_FLAG_DESCRIBE | fuchsia::io::OPEN_RIGHT_READABLE |
                           fuchsia::io::OPEN_RIGHT_WRITABLE,
                       cloned_exported_dir_.NewRequest());

  cloned_exported_dir_.events().OnOpen = [this](zx_status_t status,
                                                std::unique_ptr<fuchsia::io::NodeInfo> /*info*/) {
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "could not bind out directory for component" << label_ << "): " << status;
      return;
    }
    out_ready_ = true;
    auto output_dir = fbl::AdoptRef(new fs::RemoteDir(cloned_exported_dir_.Unbind().TakeChannel()));
    hub_.PublishOut(std::move(output_dir));
    NotifyDiagnosticsDirReady(diagnostics_max_retries_);
    TRACE_DURATION_BEGIN("appmgr", "ComponentController::OnDirectoryReady");
    SendOnDirectoryReadyEvent();
    TRACE_DURATION_END("appmgr", "ComponentController::OnDirectoryReady");
  };

  cloned_exported_dir_.set_error_handler(
      [this](zx_status_t status) { cloned_exported_dir_.Unbind(); });
}

void ComponentControllerBase::NotifyDiagnosticsDirReady(uint32_t max_retries) {
  auto promise =
      GetDiagnosticsDir()
          .and_then([self = weak_ptr_factory_.GetWeakPtr()](
                        fidl::InterfaceHandle<fuchsia::io::Directory>& dir) {
            if (self) {
              self->ns_->NotifyComponentDiagnosticsDirReady(self->url_, self->label_,
                                                            self->hub_instance_id_, std::move(dir));
            }
          })
          .or_else([self = weak_ptr_factory_.GetWeakPtr(), max_retries](zx_status_t& status) {
            if (self && status == ZX_ERR_NOT_FOUND) {
              async::PostDelayedTask(
                  self->executor_.dispatcher(),
                  [self = std::move(self), max_retries]() {
                    if (self && max_retries > 0) {
                      self->NotifyDiagnosticsDirReady(max_retries - 1);
                    }
                  },
                  zx::msec(OUT_DIAGNOSTICS_RETRY_DELAY_MS));
            }
            return fit::error(status);
          });
  executor_.schedule_task(std::move(promise));
}

fit::promise<fidl::InterfaceHandle<fuchsia::io::Directory>, zx_status_t>
ComponentControllerBase::GetDir(std::string path) {
  // This error would occur if the method was called when the component out/ directory wasn't ready
  // yet. This can be triggered when a listener is attached to a realm and notifies about existing
  // components. It could happen that the component exists, but its out is not ready yet. Under such
  // scenario, the listener will receive a START event for the existing component, but won't
  // receive a DIAGNOSTICS_DIR_READY event during the existing flow. The DIAGNOSTICS_READY_EVENT
  // will be triggered later once the out/ directory is ready if the component exposes a
  // diagnostics directory.
  if (!out_ready_) {
    return fit::make_result_promise<fidl::InterfaceHandle<fuchsia::io::Directory>>(
        fit::error(ZX_ERR_BAD_STATE));
  }
  fuchsia::io::NodePtr diagnostics_dir_node;
  fit::bridge<void, zx_status_t> bridge;
  diagnostics_dir_node.events().OnOpen =
      [completer = std::move(bridge.completer), label = label_](
          zx_status_t status, std::unique_ptr<fuchsia::io::NodeInfo> node_info) mutable {
        if (status != ZX_OK) {
          completer.complete_error(status);
        } else if (!node_info) {
          completer.complete_error(ZX_ERR_NOT_FOUND);
        } else if (!node_info->is_directory()) {
          completer.complete_error(ZX_ERR_NOT_DIR);
        } else {
          completer.complete_ok();
        }
      };

  const uint32_t flags = fuchsia::io::OPEN_FLAG_DESCRIBE | fuchsia::io::OPEN_RIGHT_READABLE |
                         fuchsia::io::OPEN_RIGHT_WRITABLE;
  exported_dir_->Open(flags, 0u /* mode */, path, diagnostics_dir_node.NewRequest());
  return bridge.consumer.promise().and_then([diagnostics_dir_node =
                                                 std::move(diagnostics_dir_node)]() mutable {
    auto diagnostics_dir =
        fidl::InterfaceHandle<fuchsia::io::Directory>(diagnostics_dir_node.Unbind().TakeChannel());
    return fit::make_result_promise<fidl::InterfaceHandle<fuchsia::io::Directory>, zx_status_t>(
        fit::ok(std::move(diagnostics_dir)));
  });
}

fit::promise<fidl::InterfaceHandle<fuchsia::io::Directory>, zx_status_t>
ComponentControllerBase::GetDiagnosticsDir() {
  return GetDir("diagnostics");
}

fit::promise<fidl::InterfaceHandle<fuchsia::io::Directory>, zx_status_t>
ComponentControllerBase::GetServiceDir() {
  return GetDir("svc");
}

void ComponentControllerBase::SendOnDirectoryReadyEvent() {
  // This can be false if
  // 1. Other side of the channel dies before this call happens.
  // 2. Component Controller request was not passed while creating the component.
  if (binding_.is_bound()) {
    binding_.events().OnDirectoryReady();
  }
}

void ComponentControllerBase::SendOnTerminationEvent(
    int64_t return_code, fuchsia::sys::TerminationReason termination_reason) {
  // `binding_.is_bound()` can be false if
  //  1. Other side of the channel dies before this call happens.
  //  2. Component Controller request was not passed while creating the component.
  if (on_terminated_event_sent_ || !binding_.is_bound()) {
    return;
  }
  FX_VLOGS(1) << "Sending termination callback with return code: " << return_code;
  binding_.events().OnTerminated(return_code, termination_reason);
  on_terminated_event_sent_ = true;
}

ComponentControllerBase::~ComponentControllerBase() { ns_->FlushAndShutdown(ns_); };

HubInfo ComponentControllerBase::HubInfo() {
  return component::HubInfo(label_, hub_instance_id_, hub_.dir());
}

void ComponentControllerBase::Detach() {
  binding_.set_error_handler([](zx_status_t /*status*/) {});
}

ComponentControllerImpl::ComponentControllerImpl(
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
    ComponentContainer<ComponentControllerImpl>* container, zx::job job, zx::process process,
    std::string url, std::string args, std::string label, fxl::RefPtr<Namespace> ns,
    zx::channel exported_dir, zx::channel client_request, zx::channel package_handle)
    : ComponentControllerBase(std::move(request), std::move(url), std::move(args), std::move(label),
                              std::to_string(fsl::GetKoid(process.get())), std::move(ns),
                              std::move(exported_dir), std::move(client_request)),
      container_(container),
      job_(std::move(job)),
      process_(std::move(process)),
      process_koid_(std::to_string(fsl::GetKoid(process_.get()))),
      job_koid_(std::to_string(fsl::GetKoid(job_.get()))),
      wait_(this, process_.get(), ZX_TASK_TERMINATED),
      system_diagnostics_(DuplicateProcess(process_)) {
  zx_status_t status = wait_.Begin(async_get_default_dispatcher());
  FX_DCHECK(status == ZX_OK);

  hub()->SetJobId(job_koid_);
  hub()->SetProcessId(process_koid_);

  // Serve connections to the system_diagnostics interface.
  auto system_diagnostics = fbl::MakeRefCounted<fs::PseudoDir>();
  system_diagnostics->AddEntry(
      fuchsia::inspect::Tree::Name_,
      fbl::AdoptRef(new fs::Service(
          [handler = inspect::MakeTreeHandler(&system_diagnostics_.inspector())](zx::channel chan) {
            handler(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(chan)));
            return ZX_OK;
          })));

  hub()->AddEntry("system_diagnostics", system_diagnostics);

  hub()->AddIncomingServices(this->incoming_services());

  if (package_handle.is_valid()) {
    hub()->AddPackageHandle(fbl::MakeRefCounted<fs::RemoteDir>(std::move(package_handle)));
  }

  zx::job watch_job;
  if (job_.duplicate(ZX_RIGHT_SAME_RIGHTS, &watch_job) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate job handle";
  }
  ComputeComponentInstancePath();
  auto realm = this->ns()->realm();
  if (realm && realm->cpu_watcher() && !instance_path_.empty()) {
    realm->cpu_watcher()->AddTask(instance_path_, std::move(watch_job));
  }
}

ComponentControllerImpl::~ComponentControllerImpl() {
  zx_info_handle_basic_t info = {};
  // Two ways we end up here:
  // 1) OnHandleReady() destroys this object; in which case, process is dead.
  // 2) Our owner destroys this object; in which case, the process may still be
  //    alive.
  if (job_) {
    job_.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    job_.kill();
    // Our owner destroyed this object before we could obtain a termination
    // reason.

    SendOnTerminationEvent(-1, TerminationReason::UNKNOWN);
  }

  auto realm = this->ns()->realm();
  if (realm && realm->cpu_watcher()) {
    if (!instance_path_.empty()) {
      realm->cpu_watcher()->RemoveTask(instance_path_);
    }
  }

  // Clean up system diagnostics before deleting the backing objects.
  hub()->dir()->RemoveEntry("system_diagnostics");
}

void ComponentControllerImpl::Kill() {
  FX_VLOGS(1) << "ComponentControllerImpl::Kill() called";
  TRACE_DURATION("appmgr", "ComponentController::Kill");
  if (job_) {
    job_.kill();
    job_.reset();
  }
}

bool ComponentControllerImpl::SendReturnCodeIfTerminated() {
  // Get process info.
  zx_info_process_t process_info;
  zx_status_t result =
      process_.get_info(ZX_INFO_PROCESS, &process_info, sizeof(process_info), NULL, NULL);
  FX_DCHECK(result == ZX_OK);

  if (process_info.exited) {
    SendOnTerminationEvent(process_info.return_code, TerminationReason::EXITED);
  }

  return process_info.exited;
}

zx_status_t ComponentControllerImpl::AddSubComponentHub(const component::HubInfo& hub_info) {
  hub()->EnsureComponentDir();
  return hub()->AddComponent(hub_info);
}

zx_status_t ComponentControllerImpl::RemoveSubComponentHub(const component::HubInfo& hub_info) {
  return hub()->RemoveComponent(hub_info);
}

// Called when process terminates, regardless of if Kill() was invoked.
void ComponentControllerImpl::Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                      zx_status_t status, const zx_packet_signal* signal) {
  FX_DCHECK(status == ZX_OK);
  FX_DCHECK(signal->observed == ZX_TASK_TERMINATED);
  FX_VLOGS(1) << "ComponentControllerImpl::Handler() called";

  bool terminated = SendReturnCodeIfTerminated();
  FX_DCHECK(terminated);

  process_.reset();
  container_->ExtractComponent(this);
  // The destructor of the temporary returned by ExtractComponent destroys
  // |this| at the end of the previous statement.
}

void ComponentControllerImpl::ComputeComponentInstancePath() {
  if (!instance_path_.empty()) {
    return;
  }
  std::vector<std::string> path;
  auto realm = this->ns()->realm();
  if (realm) {
    instance_path_.push_back(this->label());
    auto cur = realm;
    while (cur) {
      instance_path_.push_back(cur->label());
      cur = cur->parent();
    }
    std::reverse(instance_path_.begin(), instance_path_.end());
    instance_path_.push_back(job_koid_);
  }
}

ComponentBridge::ComponentBridge(fidl::InterfaceRequest<fuchsia::sys::ComponentController> request,
                                 fuchsia::sys::ComponentControllerPtr remote_controller,
                                 ComponentContainer<ComponentBridge>* container, std::string url,
                                 std::string args, std::string label, std::string hub_instance_id,
                                 fxl::RefPtr<Namespace> ns, zx::channel exported_dir,
                                 zx::channel client_request,
                                 std::optional<zx::channel> package_handle)
    : ComponentControllerBase(std::move(request), std::move(url), std::move(args), std::move(label),
                              hub_instance_id, std::move(ns), std::move(exported_dir),
                              std::move(client_request), MAX_RETRIES_OUT_DIAGNOSTICS),
      remote_controller_(std::move(remote_controller)),
      container_(std::move(container)),
      termination_reason_(TerminationReason::UNKNOWN) {
  // Forward termination callbacks from the remote component over the bridge.
  remote_controller_.events().OnTerminated = [this](int64_t return_code,
                                                    TerminationReason termination_reason) mutable {
    // Propagate the events to the external proxy.
    if (on_terminated_event_) {
      on_terminated_event_(return_code, termination_reason);
    }

    SendOnTerminationEvent(return_code, termination_reason);
    remote_controller_.events().OnTerminated = nullptr;
    container_->ExtractComponent(this);
  };

  remote_controller_.events().OnDirectoryReady = [this] { SendOnDirectoryReadyEvent(); };

  remote_controller_.set_error_handler([this](zx_status_t status) {
    if (remote_controller_.events().OnTerminated != nullptr) {
      remote_controller_.events().OnTerminated(-1, TerminationReason::UNKNOWN);
    }
  });
  // The destructor of the temporary returned by ExtractComponent destroys
  // |this| at the end of the previous statement.

  hub()->AddIncomingServices(this->incoming_services());
  if (package_handle.has_value() && package_handle->is_valid()) {
    hub()->AddPackageHandle(fbl::MakeRefCounted<fs::RemoteDir>(std::move(*package_handle)));
  }
}

void ComponentBridge::NotifyStopped() {
  ns()->NotifyComponentStopped(url(), label(), hub_instance_id());
}

ComponentBridge::~ComponentBridge() {
  if (remote_controller_.events().OnTerminated) {
    SendOnTerminationEvent(-1, termination_reason_);
  }
}

void ComponentBridge::SetParentJobId(const std::string& id) { hub()->SetJobId(id); }

void ComponentBridge::Kill() { remote_controller_->Kill(); }

void ComponentBridge::SetTerminationReason(TerminationReason termination_reason) {
  termination_reason_ = termination_reason;
}

}  // namespace component
