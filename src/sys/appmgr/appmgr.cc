// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/appmgr.h"

#include "fcntl.h"
#include "lib/sys/cpp/termination_reason.h"
#include "src/lib/fxl/strings/string_printf.h"

using fuchsia::sys::TerminationReason;

namespace component {
namespace {
constexpr char kRootLabel[] = "app";
constexpr zx::duration kMinSmsmgrBackoff = zx::msec(200);
constexpr zx::duration kMaxSysmgrBackoff = zx::sec(15);
constexpr zx::duration kSysmgrAliveReset = zx::sec(5);
}  // namespace

Appmgr::Appmgr(async_dispatcher_t* dispatcher, AppmgrArgs args)
    : publish_vfs_(dispatcher),
      publish_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      sysmgr_url_(std::move(args.sysmgr_url)),
      sysmgr_args_(std::move(args.sysmgr_args)),
      sysmgr_backoff_(kMinSmsmgrBackoff, kMaxSysmgrBackoff, kSysmgrAliveReset),
      sysmgr_permanently_failed_(false),
      storage_watchdog_(StorageWatchdog("/data", "/data/cache")) {
  // 0. Start storage watchdog for cache storage
  storage_watchdog_.Run(dispatcher);

  // 1. Create root realm.
  fxl::UniqueFD appmgr_config_dir(open("/pkgfs/packages/config-data/0/data/appmgr", O_RDONLY));
  RealmArgs realm_args = RealmArgs::MakeWithAdditionalServices(
      nullptr, kRootLabel, "/data", "/data/cache", "/tmp", std::move(args.environment_services),
      args.run_virtual_console, std::move(args.root_realm_services),
      fuchsia::sys::EnvironmentOptions{}, std::move(appmgr_config_dir));
  root_realm_ = Realm::Create(std::move(realm_args));
  FXL_CHECK(root_realm_) << "Cannot create root realm ";

  // 2. Publish outgoing directories.
  // Publish the root realm's hub directory as 'hub/' and the first nested
  // realm's (to be created by sysmgr) service directory as 'svc/'.
  if (args.pa_directory_request != ZX_HANDLE_INVALID) {
    zx::channel svc_client_chan, svc_server_chan;
    zx_status_t status = zx::channel::create(0, &svc_client_chan, &svc_server_chan);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to create channel: " << status;
      return;
    }
    status = root_realm_->BindFirstNestedRealmSvc(std::move(svc_server_chan));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "failed to bind to root realm services: " << status;
      return;
    }
    auto svc = fbl::AdoptRef(new fs::RemoteDir(std::move(svc_client_chan)));
    publish_dir_->AddEntry("hub", root_realm_->hub_dir());
    publish_dir_->AddEntry("svc", svc);
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
        FXL_LOG(ERROR) << "sysmgr launch failed: "
                       << sys::TerminationReasonToString(termination_reason);
        sysmgr_permanently_failed_ = true;
      } else if (status == ZX_ERR_INVALID_ARGS) {
        FXL_LOG(ERROR) << "sysmgr reported invalid arguments";
        sysmgr_permanently_failed_ = true;
      } else {
        FXL_LOG(ERROR) << "sysmgr exited with status " << status;
      }
    };
    root_realm_->CreateComponent(std::move(launch_info), sysmgr_.NewRequest());
  };

  if (!args.retry_sysmgr_crash) {
    run_sysmgr();
    return;
  }

  async::PostTask(dispatcher, [this, dispatcher, run_sysmgr] {
    run_sysmgr();

    auto retry_handler = [this, dispatcher, run_sysmgr](zx_status_t error) {
      if (sysmgr_permanently_failed_) {
        FXL_LOG(ERROR) << "sysmgr permanently failed. Check system configuration.";
        return;
      }

      auto delay_duration = sysmgr_backoff_.GetNext();
      FXL_LOG(ERROR) << fxl::StringPrintf("sysmgr failed, restarting in %.3fs",
                                          .001f * delay_duration.to_msecs());
      async::PostDelayedTask(dispatcher, run_sysmgr, delay_duration);
    };

    sysmgr_.set_error_handler(retry_handler);
  });
}

Appmgr::~Appmgr() = default;

}  // namespace component
