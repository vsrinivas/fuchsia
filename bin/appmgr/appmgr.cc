// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/strings/string_printf.h>

#include "garnet/bin/appmgr/appmgr.h"

namespace component {
namespace {
constexpr char kRootLabel[] = "app";
constexpr zx::duration kMinSmsmgrBackoff = zx::msec(200);
constexpr zx::duration kMaxSysmgrBackoff = zx::sec(15);
constexpr zx::duration kSysmgrAliveReset = zx::sec(5);
}  // namespace

Appmgr::Appmgr(async_dispatcher_t* dispatcher, AppmgrArgs args)
    : loader_vfs_(dispatcher),
      loader_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      publish_vfs_(dispatcher),
      publish_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      sysmgr_url_(std::move(args.sysmgr_url)),
      sysmgr_args_(std::move(args.sysmgr_args)),
      sysmgr_backoff_(kMinSmsmgrBackoff, kMaxSysmgrBackoff, kSysmgrAliveReset),
      sysmgr_permanently_failed_(false) {
  // 1. Serve loader.
  loader_dir_->AddEntry(
      fuchsia::sys::Loader::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        root_loader_.AddBinding(
            fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
        return ZX_OK;
      })));

  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0) {
    FXL_LOG(FATAL) << "Appmgr unable to create channel.";
    return;
  }

  if (loader_vfs_.ServeDirectory(loader_dir_, std::move(h2)) != ZX_OK) {
    FXL_LOG(FATAL) << "Appmgr unable to serve directory.";
    return;
  }

  RealmArgs realm_args{nullptr, std::move(h1), kRootLabel,
                       args.run_virtual_console};
  root_realm_ = std::make_unique<Realm>(std::move(realm_args));

  // 2. Publish outgoing directories.
  if (args.pa_directory_request != ZX_HANDLE_INVALID) {
    auto svc = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
      return root_realm_->BindSvc(std::move(channel));
    }));
    publish_dir_->AddEntry("hub", root_realm_->hub_dir());
    publish_dir_->AddEntry("svc", svc);
    publish_vfs_.ServeDirectory(publish_dir_,
                                zx::channel(args.pa_directory_request));
  }

  // 3. Run sysmgr
  auto run_sysmgr = [this] {
    sysmgr_backoff_.Start();
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = sysmgr_url_;
    launch_info.arguments.reset(sysmgr_args_);
    // TODO(CP-82): Create a generic solution to the Wait race condition.
    auto req = sysmgr_.NewRequest();
    sysmgr_->Wait([this](zx_status_t status) {
      if (status == ZX_ERR_INVALID_ARGS) {
        FXL_LOG(ERROR) << "sysmgr reported invalid arguments";
        sysmgr_permanently_failed_ = true;
      } else {
        FXL_LOG(ERROR) << "sysmgr exited with status " << status;
      }
    });
    root_realm_->CreateComponent(std::move(launch_info), std::move(req));
  };

  if (!args.retry_sysmgr_crash) {
    run_sysmgr();
    return;
  }

  async::PostTask(dispatcher, [this, dispatcher, run_sysmgr] {
    run_sysmgr();

    auto retry_handler = [this, dispatcher, run_sysmgr] {
      if (sysmgr_permanently_failed_) {
        FXL_LOG(ERROR)
            << "sysmgr permanently failed. Check system configuration.";
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
