// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_component_manager.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

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

// Class designed to help setup a component and then launch it. These setups are necessary because
// the agent needs some information about how the component will be launch before it actually
// launches it. This is because the debugger will set itself to "catch" the component when it starts
// as a process.
class ComponentLauncher {
 public:
  explicit ComponentLauncher(std::shared_ptr<sys::ServiceDirectory> services)
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

zx_status_t ComponentLauncher::Prepare(std::vector<std::string> argv,
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
};

fuchsia::sys::ComponentControllerPtr ComponentLauncher::Launch() {
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
    : services_(std::move(services)), weak_factory_(this) {}

debug::Status ZirconComponentManager::LaunchComponent(DebuggedJob* root_job,
                                                      const std::vector<std::string>& argv,
                                                      uint64_t* component_id) {
  *component_id = 0;

  ComponentLauncher launcher(services_);
  ComponentDescription description;
  StdioHandles handles;
  if (zx_status_t status = launcher.Prepare(argv, &description, &handles); status != ZX_OK) {
    return debug::ZxStatus(status);
  }
  FX_DCHECK(expected_components_.count(description.filter) == 0);

  root_job->AppendFilter(description.filter);

  if (debug::IsDebugModeActive()) {
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
      mgr->OnComponentTerminated(return_code, description, reason);
  };

  ExpectedComponent expected_component;
  expected_component.description = description;
  expected_component.handles = std::move(handles);
  expected_component.controller = std::move(controller);
  expected_components_[description.filter] = std::move(expected_component);

  return debug::Status();
}

uint64_t ZirconComponentManager::OnProcessStart(const std::string& filter,
                                                StdioHandles& out_stdio) {
  if (auto it = expected_components_.find(filter); it != expected_components_.end()) {
    out_stdio = std::move(it->second.handles);

    uint64_t component_id = it->second.description.component_id;
    running_components_[component_id] = std::move(it->second.controller);

    expected_components_.erase(it);
    return component_id;
  }
  return 0;
}

void ZirconComponentManager::OnComponentTerminated(int64_t return_code,
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
  expected_components_.erase(description.filter);

  if (debug::IsDebugModeActive()) {
    std::stringstream ss;
    ss << "Still expecting the following components: " << expected_components_.size();
    for (auto& expected : expected_components_) {
      ss << std::endl << "* " << expected.first;
    }
    DEBUG_LOG(Process) << ss.str();
  }
}

}  // namespace debug_agent
