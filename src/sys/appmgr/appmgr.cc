// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/appmgr.h"

#include <fcntl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>

#include "lib/inspect/cpp/inspector.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/sys/appmgr/constants.h"

using fuchsia::sys::TerminationReason;

namespace component {
namespace {
constexpr zx::duration kCpuSamplePeriod = zx::min(1);
constexpr size_t kMaxInspectSize = 2 * 1024 * 1024 /* 2MB */;
}  // namespace

Appmgr::Appmgr(async_dispatcher_t* dispatcher, AppmgrArgs args)
    : inspector_(inspect::InspectSettings{.maximum_size = kMaxInspectSize}),
      cpu_watcher_(
          std::make_unique<CpuWatcher>(inspector_.GetRoot().CreateChild("cpu_stats"), zx::job())),
      publish_vfs_(dispatcher),
      publish_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      sysmgr_url_(std::move(args.sysmgr_url)),
      sysmgr_args_(std::move(args.sysmgr_args)),
      storage_watchdog_(StorageWatchdog(kRootDataDir, kRootCacheDir)),
      lifecycle_server_(this),
      lifecycle_executor_(dispatcher),
      lifecycle_allowlist_(std::move(args.lifecycle_allowlist)) {
  RecordSelfCpuStats();
  inspector_.GetRoot().CreateLazyNode(
      "inspect_stats",
      [this] {
        inspect::InspectStats stats = inspector_.GetStats();
        inspect::Inspector insp;
        insp.GetRoot().CreateUint("current_size", stats.size, &insp);
        insp.GetRoot().CreateUint("maximum_size", stats.maximum_size, &insp);
        insp.GetRoot().CreateUint("dynamic_links", stats.dynamic_child_count, &insp);
        return fit::make_result_promise(fit::ok(std::move(insp)));
      },
      &inspector_);

  // 0. Start storage watchdog for cache storage
  storage_watchdog_.Run(dispatcher);

  // 1. Create root realm.
  fxl::UniqueFD appmgr_config_dir(open("/pkgfs/packages/config-data/0/data/appmgr", O_RDONLY));
  fit::result<fbl::RefPtr<ComponentIdIndex>, ComponentIdIndex::Error> component_id_index =
      ComponentIdIndex::CreateFromAppmgrConfigDir(appmgr_config_dir);
  FX_CHECK(component_id_index) << "Cannot read component ID Index. error = "
                               << static_cast<int>(component_id_index.error());

  RealmArgs realm_args;
  if (args.loader) {
    FX_LOGS(INFO) << "Creating root realm with a custom loader";
    realm_args = RealmArgs::MakeWithCustomLoader(
        nullptr, internal::kRootLabel, kRootDataDir, kRootCacheDir, kRootTempDir,
        std::move(args.environment_services), args.run_virtual_console,
        std::move(args.root_realm_services), fuchsia::sys::EnvironmentOptions{},
        std::move(appmgr_config_dir), component_id_index.take_value(),
        std::move(args.loader.value()));
  } else {
    realm_args = RealmArgs::MakeWithAdditionalServices(
        nullptr, internal::kRootLabel, kRootDataDir, kRootCacheDir, kRootTempDir,
        std::move(args.environment_services), args.run_virtual_console,
        std::move(args.root_realm_services), fuchsia::sys::EnvironmentOptions{},
        std::move(appmgr_config_dir), component_id_index.take_value());
  }
  realm_args.cpu_watcher = cpu_watcher_.get();
  root_realm_ = Realm::Create(std::move(realm_args));
  FX_CHECK(root_realm_) << "Cannot create root realm ";

  // 2. Listen for lifecycle requests
  zx_status_t status;
  if (args.lifecycle_request != ZX_HANDLE_INVALID) {
    status = lifecycle_server_.Create(dispatcher, zx::channel(std::move(args.lifecycle_request)));
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to bind lifecycle service.";
      return;
    }
  }

  // 3. Prepare to run sysmgr and install callback to actually start it once the
  //    logs are connected.
  auto run_sysmgr = [this, dispatcher] {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = sysmgr_url_;
    launch_info.arguments = fidl::Clone(sysmgr_args_);
    sysmgr_.events().OnDirectoryReady = [this] { sysmgr_running_ = true; };
    sysmgr_.events().OnTerminated = [dispatcher](zx_status_t exit_code,
                                                 TerminationReason termination_reason) {
      // If sysmgr exited for any reason, something went wrong, so trigger reboot.
      FX_LOGS(ERROR) << "sysmgr exited with status " << exit_code;
      fuchsia::hardware::power::statecontrol::AdminPtr power_admin;
      zx_status_t status =
          fdio_service_connect("/svc/fuchsia.hardware.power.statecontrol.Admin",
                               power_admin.NewRequest(dispatcher).TakeChannel().release());
      FX_CHECK(status == ZX_OK) << "Could not connect to power state control service: "
                                << zx_status_get_string(status);
      const auto reason = fuchsia::hardware::power::statecontrol::RebootReason::SYSTEM_FAILURE;
      auto cb = [](fuchsia::hardware::power::statecontrol::Admin_Reboot_Result result) {
        if (result.is_err()) {
          FX_LOGS(FATAL) << "Failed to reboot after sysmgr exited: "
                         << zx_status_get_string(result.err());
        }
      };
      power_admin->Reboot(reason, cb);
    };
    root_realm_->CreateComponent(std::move(launch_info), sysmgr_.NewRequest());
  };
  root_realm_->log_connector()->OnReady(run_sysmgr);

  // 4. Publish outgoing directories.
  // Connect to the tracing service, and then publish the root realm's hub
  // directory as 'hub/' and the first nested realm's (to be created by sysmgr)
  // service directory as 'svc/'.
  zx::channel svc_client_chan, svc_server_chan;
  status = zx::channel::create(0, &svc_client_chan, &svc_server_chan);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create channel: " << status;
    return;
  }
  status = root_realm_->BindFirstNestedRealmSvc(std::move(svc_server_chan));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to bind to root realm services: " << status;
    return;
  }
  status = fdio_service_connect_at(svc_client_chan.get(), "fuchsia.tracing.provider.Registry",
                                   args.trace_server_channel.release());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "failed to connect to tracing: " << status;
    // In test environments the tracing registry may not be available. If this
    // fails, let's still proceed.
  }

  if (args.pa_directory_request != ZX_HANDLE_INVALID) {
    auto svc = fbl::AdoptRef(new fs::RemoteDir(std::move(svc_client_chan)));
    auto diagnostics = fbl::AdoptRef(new fs::PseudoDir());
    diagnostics->AddEntry(
        fuchsia::inspect::Tree::Name_,
        fbl::AdoptRef(new fs::Service(
            [connector = inspect::MakeTreeHandler(&inspector_)](zx::channel chan) mutable {
              connector(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(chan)));
              return ZX_OK;
            })));

    // The following are services that appmgr exposes to the v2 world, but doesn't
    // expose to the sys realm.
    auto appmgr_svc = fbl::AdoptRef(new fs::PseudoDir());
    appmgr_svc->AddEntry(
        fuchsia::sys::internal::LogConnector::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          fidl::InterfaceRequest<fuchsia::sys::internal::LogConnector> request(std::move(channel));
          root_realm_->log_connector()->AddConnectorClient(std::move(request));
          return ZX_OK;
        })));
    appmgr_svc->AddEntry(
        fuchsia::sys::internal::ComponentEventProvider::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          return root_realm_->BindComponentEventProvider(
              fidl::InterfaceRequest<fuchsia::sys::internal::ComponentEventProvider>(
                  std::move(channel)));
        })));

    publish_dir_->AddEntry("hub", root_realm_->hub_dir());
    publish_dir_->AddEntry("svc", svc);
    publish_dir_->AddEntry("diagnostics", diagnostics);
    publish_dir_->AddEntry("appmgr_svc", appmgr_svc);
    publish_vfs_.ServeDirectory(publish_dir_, zx::channel(args.pa_directory_request));
  }

  async::PostTask(dispatcher, [this, dispatcher] { MeasureCpu(dispatcher); });
}

Appmgr::~Appmgr() = default;

void Appmgr::FindLifecycleComponentsInRealm(Realm* realm,
                                            std::vector<LifecycleComponent>* lifecycle_components) {
  // Look through child realms
  for (auto it = realm->children().begin(); it != realm->children().end(); ++it) {
    FindLifecycleComponentsInRealm(it->first, lifecycle_components);
  }

  // Look for applications in the lifecycle allow list
  auto applications = realm->applications();
  for (auto it = applications.begin(); it != applications.end(); ++it) {
    auto controller = it->first;

    FuchsiaPkgUrl package_url;
    package_url.Parse(controller->url());
    Moniker component_moniker = Realm::ComputeMoniker(realm, package_url);

    if (lifecycle_allowlist_.find(component_moniker) == lifecycle_allowlist_.end()) {
      continue;
    }

    FX_LOGS(INFO) << component_moniker.url << " is in the lifecycle allow list.";
    lifecycle_components->emplace_back(it->second, component_moniker);
  }
}

struct ShutdownCountdown {
  int component_count;
  fit::function<void(zx_status_t)> complete_callback;
  std::vector<fuchsia::process::lifecycle::LifecyclePtr> lifecycle_ptrs;

  ShutdownCountdown(int component_count, fit::function<void(zx_status_t)> complete_callback)
      : component_count(component_count), complete_callback(std::move(complete_callback)) {}
};

void Appmgr::Shutdown(fit::function<void(zx_status_t)> callback) {
  FX_LOGS(INFO) << "appmgr shutdown called.";

  std::vector<LifecycleComponent> lifecycle_components;
  FindLifecycleComponentsInRealm(root_realm_.get(), &lifecycle_components);

  auto components_remaining = std::make_shared<ShutdownCountdown>(
      lifecycle_components.size(), std::move(callback));

  // Schedule tasks to shutdown the running lifecycle components. These tasks will be performed
  // concurrently.
  for (auto component : lifecycle_components) {
    // Connect to its lifecycle service and tell it to shutdown.
    lifecycle_executor_.schedule_task(component.controller->GetServiceDir().and_then(
        [components_remaining, component](fidl::InterfaceHandle<fuchsia::io::Directory>& dir) {
          // The lifecycle_allowlist_ contains v1 components which expose their services over svc/
          // instead of the PA_LIFECYCLE channel.
          fuchsia::process::lifecycle::LifecyclePtr lifecycle;
          zx_status_t status = fdio_service_connect_at(
              dir.TakeChannel().release(), "fuchsia.process.lifecycle.Lifecycle",
              lifecycle.NewRequest().TakeChannel().release());
          if (status != ZX_OK) {
            FX_LOGS(ERROR) << "Failed to connect to fuchsia.process.lifecycle.Lifecycle for "
                           << component.moniker.url << ".";
            return;
          }

          lifecycle.set_error_handler(
              [components_remaining, url = component.moniker.url](zx_status_t status) {
                components_remaining.get()->component_count--;
                if (components_remaining.get()->component_count == 0) {
                  FX_LOGS(INFO) << "All lifecycle components shut down.";
                  components_remaining.get()->complete_callback(ZX_OK);
                }
              });

          lifecycle->Stop();

          // Keep the lifecycle from being destroyed to ensure the channel stays open.
          components_remaining->lifecycle_ptrs.push_back(std::move(lifecycle));
        }));
  }
}

void Appmgr::RecordSelfCpuStats() {
  zx::job my_job;
  zx_info_handle_basic_t info = {};
  zx_status_t err = zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &my_job);
  if (err != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to initialize job " << err;
  } else {
    err = my_job.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (err != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to get job info " << err;
    }
  }
  cpu_watcher_->AddTask({"appmgr.cm", std::to_string(info.koid)}, std::move(my_job));
}

void Appmgr::MeasureCpu(async_dispatcher_t* dispatcher) {
  cpu_watcher_->Measure();

  async::PostDelayedTask(
      dispatcher, [this, dispatcher] { MeasureCpu(dispatcher); }, kCpuSamplePeriod);
}

}  // namespace component
