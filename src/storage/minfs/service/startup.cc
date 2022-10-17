// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/service/startup.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs.h"

namespace minfs {

namespace {

MountOptions ParseMountOptions(fuchsia_fs_startup::wire::StartOptions start_options,
                               bool bcache_read_only) {
  MountOptions options;

  options.verbose = start_options.verbose;
  options.fsck_after_every_transaction = start_options.fsck_after_every_transaction;

  if (bcache_read_only) {
    options.writability = Writability::ReadOnlyDisk;
    options.repair_filesystem = false;
  } else if (start_options.read_only) {
    options.writability = Writability::ReadOnlyFilesystem;
  } else {
    options.writability = Writability::Writable;
  }

  return options;
}

MountOptions ParseFormatOptions(fuchsia_fs_startup::wire::FormatOptions format_options) {
  MountOptions options;

  options.verbose = format_options.verbose;
  options.fvm_data_slices = std::max(options.fvm_data_slices, format_options.fvm_data_slices);
  // We _need_ a writable filesystem to meaningfully format it.
  options.writability = Writability::Writable;

  return options;
}

}  // namespace

StartupService::StartupService(async_dispatcher_t* dispatcher, ConfigureCallback cb)
    : fs::Service([dispatcher, this](fidl::ServerEnd<fuchsia_fs_startup::Startup> server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }),
      configure_(std::move(cb)) {}

void StartupService::Start(StartRequestView request, StartCompleter::Sync& completer) {
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status =
      block_client::RemoteBlockDevice::Create(request->device.TakeChannel(), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    completer.ReplyError(status);
    return;
  }

  auto bcache_res = CreateBcache(std::move(device));
  if (bcache_res.is_error()) {
    FX_LOGS(ERROR) << "Could not initialize bcache";
    completer.ReplyError(bcache_res.status_value());
    return;
  }
  auto [bcache, bcache_read_only] = *std::move(bcache_res);

  zx::result<> res = configure_(std::move(bcache),
                                ParseMountOptions(std::move(request->options), bcache_read_only));
  if (res.is_error()) {
    completer.ReplyError(res.status_value());
    return;
  }
  completer.ReplySuccess();
}

void StartupService::Format(FormatRequestView request, FormatCompleter::Sync& completer) {
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status =
      block_client::RemoteBlockDevice::Create(request->device.TakeChannel(), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device: " << zx_status_get_string(status);
    completer.ReplyError(status);
    return;
  }

  auto bcache_res = CreateBcache(std::move(device));
  if (bcache_res.is_error()) {
    FX_LOGS(ERROR) << "Could not initialize bcache";
    completer.ReplyError(bcache_res.status_value());
    return;
  }
  auto [bcache, bcache_read_only] = *std::move(bcache_res);
  if (bcache_read_only) {
    FX_LOGS(ERROR) << "Failed to format minfs: read only block device";
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  zx::result mkfs_res = Mkfs(ParseFormatOptions(std::move(request->options)), bcache.get());
  if (mkfs_res.is_error()) {
    FX_LOGS(ERROR) << "Failed to format minfs: " << mkfs_res.status_string();
    completer.ReplyError(mkfs_res.status_value());
    return;
  }
  completer.ReplySuccess();
}

void StartupService::Check(CheckRequestView request, CheckCompleter::Sync& completer) {
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status =
      block_client::RemoteBlockDevice::Create(request->device.TakeChannel(), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    completer.ReplyError(status);
    return;
  }

  auto bcache_res = CreateBcache(std::move(device));
  if (bcache_res.is_error()) {
    FX_LOGS(ERROR) << "Could not initialize bcache";
    completer.ReplyError(bcache_res.status_value());
    return;
  }
  auto [bcache, bcache_read_only] = *std::move(bcache_res);

  FsckOptions fsck_options;
  fsck_options.read_only = bcache_read_only;
  fsck_options.repair = !bcache_read_only;
  auto bcache_or = Fsck(std::move(bcache), fsck_options);
  if (bcache_or.is_error()) {
    FX_LOGS(ERROR) << "Consistency check failed for minfs" << bcache_or.status_string();
    completer.ReplyError(bcache_or.status_value());
    return;
  }
  completer.ReplySuccess();
}

}  // namespace minfs
