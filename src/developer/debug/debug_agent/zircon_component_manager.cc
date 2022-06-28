// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_component_manager.h"

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string>

#include "src/developer/debug/debug_agent/debugged_job.h"
#include "src/developer/debug/shared/component_utils.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

namespace {

uint64_t next_component_id = 1;

// Attempts to link a zircon socket into the new component's file descriptor number represented by
// |fd|. If successful, the socket will be connected and a (one way) communication channel with that
// file descriptor will be made.
zx::socket AddStdio(int fd, fuchsia::sys::LaunchInfo* launch_info) {
  zx::socket local;
  zx::socket target;

  zx_status_t status = zx::socket::create(0, &local, &target);
  if (status != ZX_OK)
    return zx::socket();

  auto io = std::make_unique<fuchsia::sys::FileDescriptor>();
  io->type0 = PA_HND(PA_FD, fd);
  io->handle0 = std::move(target);

  if (fd == STDOUT_FILENO) {
    launch_info->out = std::move(io);
  } else if (fd == STDERR_FILENO) {
    launch_info->err = std::move(io);
  } else {
    FX_NOTREACHED() << "Invalid file descriptor: " << fd;
    return zx::socket();
  }

  return local;
}

// Read the content of "elf/job_id" in the runtime directory of an ELF component.
// Return ZX_KOID_INVALID if no such file.
zx_koid_t ReadElfJobId(fuchsia::io::DirectoryHandle runtime_dir_handle) {
  fuchsia::io::DirectorySyncPtr runtime_dir = runtime_dir_handle.BindSync();
  fuchsia::io::FileSyncPtr job_id_file;
  zx_status_t status = runtime_dir->Open(
      fuchsia::io::OpenFlags::RIGHT_READABLE, 0, "elf/job_id",
      fidl::InterfaceRequest<fuchsia::io::Node>(job_id_file.NewRequest().TakeChannel()));
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }
  fuchsia::io::File2_Read_Result job_id_res;
  status = job_id_file->Read(fuchsia::io::MAX_TRANSFER_SIZE, &job_id_res);
  if (status != ZX_OK || !job_id_res.is_response()) {
    return ZX_KOID_INVALID;
  }
  std::string job_id_str(reinterpret_cast<const char*>(job_id_res.response().data.data()),
                         job_id_res.response().data.size());
  // We use std::strtoull here because std::stoull is not exception-safe.
  char* end;
  zx_koid_t job_id = std::strtoull(job_id_str.c_str(), &end, 10);
  if (end != job_id_str.c_str() + job_id_str.size()) {
    FX_LOGS(ERROR) << "Invalid elf/job_id: " << job_id_str;
    return ZX_KOID_INVALID;
  }
  return job_id;
}

// Class designed to help setup a component and then launch it. These setups are necessary because
// the agent needs some information about how the component will be launch before it actually
// launches it. This is because the debugger will set itself to "catch" the component when it starts
// as a process.
class V1ComponentLauncher {
 public:
  explicit V1ComponentLauncher(std::shared_ptr<sys::ServiceDirectory> services)
      : services_(std::move(services)) {}

  // Will fail if |argv| is invalid. The first element should be the component url needed to launch.
  zx_status_t Prepare(std::vector<std::string> argv,
                      ZirconComponentManager::ComponentDescription* description,
                      StdioHandles* handles);

  // The launcher has to be already successfully prepared. The lifetime of the controller is bound
  // to the lifetime of the component.
  fuchsia::sys::ComponentControllerPtr Launch();

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::sys::LaunchInfo launch_info_;
};

zx_status_t V1ComponentLauncher::Prepare(std::vector<std::string> argv,
                                         ZirconComponentManager::ComponentDescription* description,
                                         StdioHandles* handles) {
  FX_DCHECK(services_);
  FX_DCHECK(!argv.empty());

  auto pkg_url = argv.front();
  debug::ComponentDescription url_desc;
  if (!debug::ExtractComponentFromPackageUrl(pkg_url, &url_desc)) {
    FX_LOGS(WARNING) << "Invalid package url: " << pkg_url;
    return ZX_ERR_INVALID_ARGS;
  }

  // Prepare the launch info. The parameters to the component do not include
  // the component URL.
  launch_info_.url = argv.front();
  if (argv.size() > 1) {
    launch_info_.arguments.emplace();
    for (size_t i = 1; i < argv.size(); i++)
      launch_info_.arguments->push_back(std::move(argv[i]));
  }

  *description = {};
  description->component_id = next_component_id++;
  description->url = pkg_url;
  description->process_name = url_desc.component_name;
  description->filter = url_desc.component_name;

  *handles = {};
  handles->out = AddStdio(STDOUT_FILENO, &launch_info_);
  handles->err = AddStdio(STDERR_FILENO, &launch_info_);

  return ZX_OK;
}

fuchsia::sys::ComponentControllerPtr V1ComponentLauncher::Launch() {
  FX_DCHECK(services_);

  fuchsia::sys::LauncherSyncPtr launcher;
  services_->Connect(launcher.NewRequest());

  // Controller is a way to manage the newly created component. We need it in
  // order to receive the terminated events. Sadly, there is no component
  // started event. This also makes us need an async::Loop so that the fidl
  // plumbing can work.
  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info_), controller.NewRequest());

  return controller;
}

}  // namespace

ZirconComponentManager::ZirconComponentManager(std::shared_ptr<sys::ServiceDirectory> services)
    : services_(std::move(services)), event_stream_binding_(this), weak_factory_(this) {
  // 1. Subscribe to "debug_started" and "stopped" events.
  fuchsia::sys2::EventSourceSyncPtr event_source;
  services_->Connect(event_source.NewRequest());
  std::vector<fuchsia::sys2::EventSubscription> subscriptions;
  subscriptions.resize(2);
  subscriptions[0].set_event_name("debug_started");
  subscriptions[1].set_event_name("stopped");
  fuchsia::sys2::EventStreamHandle stream;
  event_stream_binding_.Bind(stream.NewRequest());
  fuchsia::sys2::EventSource_Subscribe_Result subscribe_res;
  event_source->Subscribe(std::move(subscriptions), std::move(stream), &subscribe_res);
  if (subscribe_res.is_err()) {
    FX_LOGS(ERROR) << "Failed to Subscribe: " << static_cast<uint32_t>(subscribe_res.err());
  }

  // 2. List existing components via fuchsia.sys2.RealmExplorer and fuchsia.sys2.RealmQuery.
  fuchsia::sys2::RealmExplorerSyncPtr realm_explorer;
  fuchsia::sys2::RealmQuerySyncPtr realm_query;
  services_->Connect(realm_explorer.NewRequest(), "fuchsia.sys2.RealmExplorer.root");
  services_->Connect(realm_query.NewRequest(), "fuchsia.sys2.RealmQuery.root");

  fuchsia::sys2::RealmExplorer_GetAllInstanceInfos_Result all_instance_infos_res;
  realm_explorer->GetAllInstanceInfos(&all_instance_infos_res);
  if (all_instance_infos_res.is_err()) {
    FX_LOGS(ERROR) << "Failed to GetAllInstanceInfos: "
                   << static_cast<uint32_t>(all_instance_infos_res.err());
    return;
  }
  fuchsia::sys2::InstanceInfoIteratorSyncPtr instance_it =
      all_instance_infos_res.response().iterator.BindSync();

  while (true) {
    std::vector<fuchsia::sys2::InstanceInfo> infos;
    instance_it->Next(&infos);
    if (infos.empty()) {
      break;
    }
    for (auto& info : infos) {
      if (info.state != fuchsia::sys2::InstanceState::STARTED || info.moniker.empty()) {
        continue;
      }
      fuchsia::sys2::RealmQuery_GetInstanceInfo_Result instace_info_res;
      realm_query->GetInstanceInfo(info.moniker, &instace_info_res);
      if (!instace_info_res.is_response() || !instace_info_res.response().resolved ||
          !instace_info_res.response().resolved->started ||
          !instace_info_res.response().resolved->started->runtime_dir) {
        continue;
      }
      // TODO: remove this after fxbug.dev/103480 is fixed.
      if (info.moniker == "./core/session-manager/session:session/workstation_session")
        continue;
      zx_koid_t job_id =
          ReadElfJobId(std::move(instace_info_res.response().resolved->started->runtime_dir));
      if (job_id != ZX_KOID_INVALID) {
        // Remove the "." at the beginning of the moniker. It's safe because moniker is not empty.
        running_component_info_[job_id] = {.url = info.url, .moniker = info.moniker.substr(1)};
      }
    }
  }
}

void ZirconComponentManager::OnEvent(fuchsia::sys2::Event event) {
  if (!event.has_header() || !event.header().has_moniker() || event.header().moniker().empty() ||
      !event.has_event_result() || !event.event_result().is_payload()) {
    return;
  }
  switch (event.header().event_type()) {
    case fuchsia::sys2::EventType::DEBUG_STARTED:
      if (event.event_result().payload().is_debug_started() &&
          event.event_result().payload().debug_started().has_runtime_dir()) {
        zx_koid_t job_id = ReadElfJobId(std::move(
            *event.mutable_event_result()->payload().debug_started().mutable_runtime_dir()));
        if (job_id != ZX_KOID_INVALID) {
          running_component_info_[job_id] = {.url = event.header().component_url(),
                                             .moniker = event.header().moniker().substr(1)};
          DEBUG_LOG(Process) << "Component started job_id=" << job_id
                             << " moniker=" << running_component_info_[job_id].moniker
                             << " url=" << running_component_info_[job_id].url;
        }
      }
      break;
    case fuchsia::sys2::EventType::STOPPED: {
      std::string moniker = event.header().moniker().substr(1);
      for (auto it = running_component_info_.begin(); it != running_component_info_.end(); it++) {
        if (it->second.moniker == moniker) {
          DEBUG_LOG(Process) << "Component stopped job_id=" << it->first
                             << " moniker=" << it->second.moniker << " url=" << it->second.url;
          running_component_info_.erase(it);
          break;
        }
      }
      break;
    }
    default:
      FX_NOTREACHED();
  }
}

std::optional<ZirconComponentManager::ComponentInfo> ZirconComponentManager::FindComponentInfo(
    zx_koid_t job_koid) const {
  if (auto it = running_component_info_.find(job_koid); it != running_component_info_.end())
    return it->second;
  return std::nullopt;
}

debug::Status ZirconComponentManager::LaunchComponent(DebuggedJob* root_job,
                                                      const std::vector<std::string>& argv,
                                                      uint64_t* component_id) {
  *component_id = 0;

  V1ComponentLauncher launcher(services_);
  ComponentDescription description;
  StdioHandles handles;
  if (zx_status_t status = launcher.Prepare(argv, &description, &handles); status != ZX_OK) {
    return debug::ZxStatus(status);
  }
  FX_DCHECK(expected_v1_components_.count(description.filter) == 0);

  root_job->AppendFilter(description.filter);

  if (debug::IsDebugLoggingActive()) {
    std::stringstream ss;

    ss << "Launching component. " << std::endl
       << "Url: " << description.url << std::endl
       << ", name: " << description.process_name << std::endl
       << ", filter: " << description.filter << std::endl
       << ", component_id: " << description.component_id << std::endl;

    auto& filters = root_job->filters();
    ss << "Current component filters: " << filters.size();
    for (auto& filter : filters) {
      ss << std::endl << "* " << filter.filter;
    }

    DEBUG_LOG(Process) << ss.str();
  }

  *component_id = description.component_id;

  // Launch the component.
  auto controller = launcher.Launch();
  if (!controller) {
    FX_LOGS(WARNING) << "Could not launch component " << description.url;
    return debug::Status("Could not launch component.");
  }

  // TODO(donosoc): This should hook into the debug agent so it can correctly
  //                shutdown the state associated with waiting for this
  //                component.
  controller.events().OnTerminated = [mgr = weak_factory_.GetWeakPtr(), description](
                                         int64_t return_code,
                                         fuchsia::sys::TerminationReason reason) {
    // If the agent is gone, there isn't anything more to do.
    if (mgr)
      mgr->OnV1ComponentTerminated(return_code, description, reason);
  };

  ExpectedV1Component expected_component;
  expected_component.description = description;
  expected_component.handles = std::move(handles);
  expected_component.controller = std::move(controller);
  expected_v1_components_[description.filter] = std::move(expected_component);

  return debug::Status();
}

uint64_t ZirconComponentManager::OnProcessStart(const std::string& filter,
                                                StdioHandles& out_stdio) {
  if (auto it = expected_v1_components_.find(filter); it != expected_v1_components_.end()) {
    out_stdio = std::move(it->second.handles);

    uint64_t component_id = it->second.description.component_id;
    running_v1_components_[component_id] = std::move(it->second.controller);

    expected_v1_components_.erase(it);
    return component_id;
  }
  return 0;
}

void ZirconComponentManager::OnV1ComponentTerminated(int64_t return_code,
                                                     const ComponentDescription& description,
                                                     fuchsia::sys::TerminationReason reason) {
  DEBUG_LOG(Process) << "Component " << description.url << " exited with "
                     << sys::HumanReadableTerminationReason(reason);

  // TODO(donosoc): This need to be communicated over to the client.
  if (reason != fuchsia::sys::TerminationReason::EXITED) {
    FX_LOGS(WARNING) << "Component " << description.url << " exited with "
                     << sys::HumanReadableTerminationReason(reason);
  }

  // We look for the filter and remove it.
  // If we couldn't find it, the component was already caught and cleaned.
  expected_v1_components_.erase(description.filter);

  if (debug::IsDebugLoggingActive()) {
    std::stringstream ss;
    ss << "Still expecting the following components: " << expected_v1_components_.size();
    for (auto& expected : expected_v1_components_) {
      ss << std::endl << "* " << expected.first;
    }
    DEBUG_LOG(Process) << ss.str();
  }
}

}  // namespace debug_agent
