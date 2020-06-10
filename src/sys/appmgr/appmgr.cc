// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/appmgr.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/cpp/termination_reason.h>
#include <lib/zx/time.h>

#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>

#include "lib/inspect/cpp/inspector.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/sys/appmgr/constants.h"

using fuchsia::sys::TerminationReason;

namespace component {
namespace {
constexpr zx::duration kMinSmsmgrBackoff = zx::msec(200);
constexpr zx::duration kMaxSysmgrBackoff = zx::sec(15);
constexpr zx::duration kSysmgrAliveReset = zx::sec(5);
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
      sysmgr_backoff_(kMinSmsmgrBackoff, kMaxSysmgrBackoff, kSysmgrAliveReset),
      sysmgr_retry_crashes_(args.retry_sysmgr_crash),
      sysmgr_permanently_failed_(false),
      storage_watchdog_(StorageWatchdog(kRootDataDir, kRootCacheDir)) {
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
  RealmArgs realm_args = RealmArgs::MakeWithAdditionalServices(
      nullptr, internal::kRootLabel, kRootDataDir, kRootCacheDir, kRootTempDir,
      std::move(args.environment_services), args.run_virtual_console,
      std::move(args.root_realm_services), fuchsia::sys::EnvironmentOptions{},
      std::move(appmgr_config_dir), component_id_index.take_value());
  realm_args.cpu_watcher = cpu_watcher_.get();
  root_realm_ = Realm::Create(std::move(realm_args));
  FX_CHECK(root_realm_) << "Cannot create root realm ";

  // 2. Publish outgoing directories.
  // Connect to the tracing service, and then publish the root realm's hub
  // directory as 'hub/' and the first nested realm's (to be created by sysmgr)
  // service directory as 'svc/'.
  zx::channel svc_client_chan, svc_server_chan;
  zx_status_t status = zx::channel::create(0, &svc_client_chan, &svc_server_chan);
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
    publish_dir_->AddEntry("hub", root_realm_->hub_dir());
    publish_dir_->AddEntry("svc", svc);
    publish_dir_->AddEntry("diagnostics", diagnostics);
    publish_vfs_.ServeDirectory(publish_dir_, zx::channel(args.pa_directory_request));
  }

  // 3. Run sysmgr
  auto run_sysmgr = [this] {
    sysmgr_backoff_.Start();
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = sysmgr_url_;
    launch_info.arguments = fidl::Clone(sysmgr_args_);
    sysmgr_.events().OnTerminated = [this](zx_status_t status,
                                           TerminationReason termination_reason) {
      if (termination_reason != TerminationReason::EXITED) {
        FX_LOGS(WARNING) << "sysmgr launch failed: "
                         << sys::TerminationReasonToString(termination_reason);
        sysmgr_permanently_failed_ = true;
      } else if (status == ZX_ERR_INVALID_ARGS) {
        FX_LOGS(WARNING) << "sysmgr reported invalid arguments";
        sysmgr_permanently_failed_ = true;
      } else {
        FX_LOGS(INFO) << "sysmgr exited with status " << status;
      }

      if (!sysmgr_retry_crashes_) {
        FX_CHECK(ZX_OK == status) << "sysmgr retries are disabled and it failed ("
                                  << sys::TerminationReasonToString(termination_reason)
                                  << ", status " << status << ")";
      }
    };
    root_realm_->CreateComponent(std::move(launch_info), sysmgr_.NewRequest());
  };

  if (!sysmgr_retry_crashes_) {
    run_sysmgr();
    return;
  }

  async::PostTask(dispatcher, [this, dispatcher, run_sysmgr] {
    run_sysmgr();

    auto retry_handler = [this, dispatcher, run_sysmgr](zx_status_t error) {
      if (sysmgr_permanently_failed_) {
        FX_LOGS(ERROR) << "sysmgr permanently failed. Check system configuration.";
        return;
      }

      auto delay_duration = sysmgr_backoff_.GetNext();
      FX_LOGS(WARNING) << fxl::StringPrintf("sysmgr failed, restarting in %.3fs",
                                            .001f * delay_duration.to_msecs());
      async::PostDelayedTask(dispatcher, run_sysmgr, delay_duration);
    };

    sysmgr_.set_error_handler(retry_handler);
  });

  async::PostTask(dispatcher, [this, dispatcher] { MeasureCpu(dispatcher); });
}

Appmgr::~Appmgr() = default;

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
