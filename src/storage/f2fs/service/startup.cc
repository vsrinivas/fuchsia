// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/service/startup.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>

#include "src/storage/f2fs/bcache.h"
#include "src/storage/f2fs/fsck.h"
#include "src/storage/f2fs/mkfs.h"

namespace f2fs {

StartupService::StartupService(async_dispatcher_t* dispatcher, ConfigureCallback cb)
    : fs::Service([dispatcher, this](fidl::ServerEnd<fuchsia_fs_startup::Startup> server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }),
      configure_(std::move(cb)) {}

void StartupService::Start(StartRequestView request, StartCompleter::Sync& completer) {
  auto bc_or = f2fs::CreateBcache(std::move(request->device));
  if (bc_or.is_error()) {
    completer.ReplyError(bc_or.status_value());
    return;
  }

  // TODO: parse option from request->options.
  if (auto status = configure_(std::move(*bc_or), MountOptions{}); status.is_error()) {
    completer.ReplyError(status.status_value());
    return;
  }
  completer.ReplySuccess();
}

void StartupService::Format(FormatRequestView request, FormatCompleter::Sync& completer) {
  bool readonly_device = false;
  auto bc_or = f2fs::CreateBcache(std::move(request->device), &readonly_device);
  if (bc_or.is_error()) {
    completer.ReplyError(bc_or.status_value());
    return;
  }

  if (readonly_device) {
    FX_LOGS(ERROR) << "failed to format f2fs: read only block device";
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  f2fs::MkfsOptions mkfs_options;
  // TODO: parse option from request->options.
  if (auto status = f2fs::Mkfs(mkfs_options, std::move(*bc_or)); status.is_error()) {
    FX_LOGS(ERROR) << "failed to format f2fs: " << status.status_string();
    completer.ReplyError(bc_or.status_value());
  }
  completer.ReplySuccess();
}

void StartupService::Check(CheckRequestView request, CheckCompleter::Sync& completer) {
  bool readonly_device = false;
  auto bc_or = f2fs::CreateBcache(std::move(request->device), &readonly_device);
  if (bc_or.is_error()) {
    completer.ReplyError(bc_or.status_value());
    return;
  }

  // TODO: parse option from request->options.
  FsckOptions fsck_options;
  fsck_options.repair = !readonly_device;

  if (auto status = Fsck(std::move(*bc_or), fsck_options); status != ZX_OK) {
    FX_LOGS(ERROR) << "Fsck failed " << status;
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

}  // namespace f2fs
