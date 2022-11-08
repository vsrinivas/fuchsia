// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/appmgr.h"

#include <fcntl.h>
#include <fuchsia/appmgr/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <fbl/ref_ptr.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/sys/appmgr/constants.h"
#include "src/sys/appmgr/startup_service.h"

using fuchsia::sys::TerminationReason;

namespace component {
namespace {
constexpr zx::duration kCpuSamplePeriod = zx::min(1);
constexpr size_t kMaxInspectSize = 2 * 1024 * 1024 /* 2MB */;

const inspect::StringReference kCpuStats("cpu_stats");
const inspect::StringReference kInspectStats("inspect_stats");
const inspect::StringReference kCurrentSize("current_size");
const inspect::StringReference kMaximumSize("maximum_size");
const inspect::StringReference kDynamicLinks("dynamic_links");
const inspect::StringReference kStorageWatchdog("storage_watchdog");
}  // namespace

Appmgr::Appmgr(async_dispatcher_t* dispatcher, AppmgrArgs args)
    : inspector_(inspect::InspectSettings{.maximum_size = kMaxInspectSize}),
      cpu_watcher_(

          std::make_unique<CpuWatcher>(inspector_.GetRoot().CreateChild(kCpuStats),
                                       CpuWatcherParameters{
                                           .sample_period = kCpuSamplePeriod,
                                       },
                                       nullptr /* stats_reader */)),
      publish_vfs_(dispatcher),
      publish_dir_(fbl::MakeRefCounted<fs::PseudoDir>()),
      sysmgr_url_(std::move(args.sysmgr_url)),
      sysmgr_args_(std::move(args.sysmgr_args)),
      storage_watchdog_(StorageWatchdog(inspector_.GetRoot().CreateChild(kStorageWatchdog),
                                        kRootDataDir, kRootCacheDir)),
      storage_metrics_(
          {
              "/data/cache",
              "/data/persistent",
          },
          inspector_.GetRoot().CreateChild("storage_usage")),
      lifecycle_server_(this, std::move(args.stop_callback)),
      lifecycle_executor_(dispatcher),
      lifecycle_allowlist_(std::move(args.lifecycle_allowlist)),
      startup_service_() {
  // TODO(fxbug.dev/75726)
  inspector_.GetRoot().CreateLazyNode(
      kInspectStats,
      [&] {
        inspect::InspectStats stats = inspector_.GetStats();
        inspect::Inspector insp;
        insp.GetRoot().CreateUint(kCurrentSize, stats.size, &insp);
        insp.GetRoot().CreateUint(kMaximumSize, stats.maximum_size, &insp);
        insp.GetRoot().CreateUint(kDynamicLinks, stats.dynamic_child_count, &insp);
        return fpromise::make_result_promise(fpromise::ok(std::move(insp)));
      },
      &inspector_);

  // 0. Start storage watchdog for cache storage
  storage_watchdog_.Run(dispatcher);
  if (zx_status_t status = storage_metrics_.Run().status_value(); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to start polling storage usage: " << zx_status_get_string(status);
  }

  // 1. Create root realm.
  fbl::unique_fd appmgr_config_dir(
      open("/pkgfs/packages/config-data/0/meta/data/appmgr", O_RDONLY));
  if (!appmgr_config_dir.is_valid()) {
    FX_LOGS(ERROR) << "Could not open appmgr's config dir. error = " << appmgr_config_dir.get();
  }
  fpromise::result<fbl::RefPtr<ComponentIdIndex>, ComponentIdIndex::Error> component_id_index =
      ComponentIdIndex::CreateFromAppmgrConfigDir(appmgr_config_dir);
  FX_CHECK(component_id_index) << "Cannot read component ID Index. error = "
                               << static_cast<int>(component_id_index.error());

  RealmArgs realm_args;
  if (args.loader) {
    FX_LOGS(INFO) << "Creating root realm with a custom loader";
    realm_args = RealmArgs::MakeWithCustomLoader(
        nullptr, internal::kRootLabel, kRootDataDir, kRootCacheDir, kRootTempDir,
        std::move(args.environment_services), std::move(args.root_realm_services),
        fuchsia::sys::EnvironmentOptions{}, std::move(appmgr_config_dir),
        component_id_index.take_value(), std::move(args.loader.value()));
  } else {
    realm_args = RealmArgs::MakeWithAdditionalServices(
        nullptr, internal::kRootLabel, kRootDataDir, kRootCacheDir, kRootTempDir,
        std::move(args.environment_services), std::move(args.root_realm_services),
        fuchsia::sys::EnvironmentOptions{}, std::move(appmgr_config_dir),
        component_id_index.take_value());
  }
  realm_args.cpu_watcher = cpu_watcher_.get();
  root_realm_ = Realm::Create(std::move(realm_args));
  FX_CHECK(root_realm_) << "Cannot create root realm ";

  // 2. Listen for lifecycle requests
  if (args.lifecycle_request != ZX_HANDLE_INVALID) {
    if (zx_status_t status =
            lifecycle_server_.Create(dispatcher, zx::channel(std::move(args.lifecycle_request)));
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to bind lifecycle service.";
      return;
    }
  }

  // 3. Prepare to run sysmgr, if enabled, and install callback to actually start it once the logs
  //    are connected.
  if (!sysmgr_url_.empty()) {
    auto run_sysmgr = [this, dispatcher] {
      fuchsia::sys::LaunchInfo launch_info;
      launch_info.url = sysmgr_url_;
      launch_info.arguments = fidl::Clone(sysmgr_args_);
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
        const auto reason = fuchsia::hardware::power::statecontrol::RebootReason::SYSMGR_FAILURE;
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
  } else {
    FX_LOGS(INFO) << "Running appmgr without sysmgr";
    auto run_sysmgr = [this, dispatcher] {
      fuchsia::sys::EnvironmentOptions options = {.inherit_parent_services = true};
      fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
      sys_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
      sys_vfs_ = std::make_unique<fs::SynchronousVfs>(dispatcher);
      sys_vfs_->ServeDirectory(sys_dir_, service_list->host_directory.NewRequest().TakeChannel());
      root_realm_->CreateNestedEnvironment(sys_env_.NewRequest(), sys_env_controller_.NewRequest(),
                                           "sys", std::move(service_list), options);
    };

    root_realm_->log_connector()->OnReady(run_sysmgr);
  }

  // 4. Publish outgoing directories.
  // Connect to the tracing service, and then publish the root realm's hub
  // directory as 'hub/' and the first nested realm's
  // service directory as 'svc/' (either created by sysmgr, or appmgr itself if there is no sysmgr).
  fidl::InterfaceHandle<fuchsia::io::Node> handle;
  if (zx_status_t status = root_realm_->BindFirstNestedRealmSvc(handle.NewRequest());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to bind to root realm services: " << status;
    return;
  }
  if (zx_status_t status =
          fdio_service_connect_at(handle.channel().get(), "fuchsia.tracing.provider.Registry",
                                  args.trace_server_channel.release());
      status != ZX_OK) {
    FX_LOGS(WARNING) << "failed to connect to tracing: " << status;
    // In test environments the tracing registry may not be available. If this
    // fails, let's still proceed.
  }

  if (args.pa_directory_request != ZX_HANDLE_INVALID) {
    auto svc = fbl::MakeRefCounted<fs::RemoteDir>(handle.TakeChannel());
    auto diagnostics = fbl::MakeRefCounted<fs::PseudoDir>();
    diagnostics->AddEntry(
        fuchsia::inspect::Tree::Name_,
        fbl::MakeRefCounted<fs::Service>(
            [connector = inspect::MakeTreeHandler(&inspector_)](zx::channel chan) mutable {
              connector(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(chan)));
              return ZX_OK;
            }));

    // The following are services that appmgr exposes to the v2 world, but doesn't
    // expose to the sys realm.
    auto appmgr_svc = fbl::MakeRefCounted<fs::PseudoDir>();
    appmgr_svc->AddEntry(
        fuchsia::sys::internal::LogConnector::Name_,
        fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
          fidl::InterfaceRequest<fuchsia::sys::internal::LogConnector> request(std::move(channel));
          root_realm_->log_connector()->AddConnectorClient(std::move(request));
          return ZX_OK;
        }));
    appmgr_svc->AddEntry(
        fuchsia::sys::internal::ComponentEventProvider::Name_,
        fbl::MakeRefCounted<fs::Service>([this](zx::channel channel) {
          return root_realm_->BindComponentEventProvider(
              fidl::InterfaceRequest<fuchsia::sys::internal::ComponentEventProvider>(
                  std::move(channel)));
        }));

    appmgr_svc->AddEntry(
        fuchsia::appmgr::Startup::Name_,
        fbl::MakeRefCounted<fs::Service>([this, dispatcher](zx::channel channel) {
          return startup_service_.Bind(
              dispatcher, fidl::InterfaceRequest<fuchsia::appmgr::Startup>(std::move(channel)));
        }));

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

  ShutdownCountdown(int component_count, fit::function<void(zx_status_t)> complete_callback)
      : component_count(component_count), complete_callback(std::move(complete_callback)) {}
};

std::vector<std::shared_ptr<fuchsia::process::lifecycle::LifecyclePtr>> Appmgr::Shutdown(
    fit::function<void(zx_status_t)> callback) {
  FX_LOGS(INFO) << "appmgr shutdown called.";

  std::vector<LifecycleComponent> lifecycle_components;
  FindLifecycleComponentsInRealm(root_realm_.get(), &lifecycle_components);

  auto components_remaining =
      std::make_shared<ShutdownCountdown>(lifecycle_components.size(), std::move(callback));

  std::vector<std::shared_ptr<fuchsia::process::lifecycle::LifecyclePtr>> child_lifecycles;

  if (components_remaining->component_count == 0) {
    FX_LOGS(INFO) << "No components expose lifecycle, continuing appmgr shutdown.";
    components_remaining->complete_callback(ZX_OK);
    return child_lifecycles;
  }

  // Schedule tasks to shutdown the running lifecycle components. These tasks will be performed
  // concurrently.
  for (auto& component : lifecycle_components) {
    auto lifecycle = std::make_shared<fuchsia::process::lifecycle::LifecyclePtr>();
    child_lifecycles.push_back(lifecycle);
    // Connect to its lifecycle service and tell it to shutdown.
    lifecycle_executor_.schedule_task(component.controller->GetServiceDir().and_then(
        [components_remaining, component = std::move(component),
         lifecycle](fidl::InterfaceHandle<fuchsia::io::Directory>& dir) {
          // The lifecycle_allowlist_ contains v1 components which expose their services over svc/
          // instead of the PA_LIFECYCLE channel.

          zx_status_t status = fdio_service_connect_at(
              dir.TakeChannel().release(), "fuchsia.process.lifecycle.Lifecycle",
              lifecycle->NewRequest().TakeChannel().release());
          if (status != ZX_OK) {
            FX_LOGS(ERROR) << "Failed to connect to fuchsia.process.lifecycle.Lifecycle for "
                           << component.moniker.url << ".";
            return;
          }

          lifecycle->set_error_handler(
              [components_remaining, url = component.moniker.url](zx_status_t status) {
                components_remaining->component_count--;
                if (components_remaining->component_count == 0) {
                  FX_LOGS(INFO) << "All lifecycle components shut down.";
                  components_remaining->complete_callback(ZX_OK);
                }
              });
          lifecycle->get()->Stop();

          // Keep the lifecycle from being destroyed to ensure the channel stays open.
        }));
  }
  return child_lifecycles;
}

void Appmgr::MeasureCpu(async_dispatcher_t* dispatcher) {
  cpu_watcher_->Measure();

  async::PostDelayedTask(
      dispatcher, [this, dispatcher] { MeasureCpu(dispatcher); }, kCpuSamplePeriod);
}

}  // namespace component
