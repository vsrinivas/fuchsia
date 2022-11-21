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
  // Use a closure to ensure that any sessions created are destroyed before we respond to the
  // request.
  //
  // TODO(https://fxbug.dev/97783): Consider removing this when multiple sessions are permitted.
  zx::result<> result = [&]() -> zx::result<> {
    std::unique_ptr<block_client::RemoteBlockDevice> device;
    zx_status_t status =
        block_client::RemoteBlockDevice::Create(std::move(request->device), &device);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Could not initialize block device";
      return zx::error(status);
    }

    auto bcache_res = CreateBcache(std::move(device));
    if (bcache_res.is_error()) {
      FX_PLOGS(ERROR, bcache_res.error_value()) << "Could not initialize bcache";
      return bcache_res.take_error();
    }
    auto [bcache, bcache_read_only] = *std::move(bcache_res);

    return configure_(std::move(bcache), ParseMountOptions(request->options, bcache_read_only));
  }();
  completer.Reply(result);
}

void StartupService::Format(FormatRequestView request, FormatCompleter::Sync& completer) {
  // Use a closure to ensure that any sessions created are destroyed before we respond to the
  // request.
  //
  // TODO(https://fxbug.dev/97783): Consider removing this when multiple sessions are permitted.
  zx::result<> result = [&]() -> zx::result<> {
    std::unique_ptr<block_client::RemoteBlockDevice> device;
    zx_status_t status =
        block_client::RemoteBlockDevice::Create(std::move(request->device), &device);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Could not initialize block device";
      return zx::error(status);
    }

    auto bcache_res = CreateBcache(std::move(device));
    if (bcache_res.is_error()) {
      FX_PLOGS(ERROR, bcache_res.error_value()) << "Could not initialize bcache";
      return bcache_res.take_error();
    }
    auto [bcache, bcache_read_only] = *std::move(bcache_res);
    if (bcache_read_only) {
      FX_LOGS(ERROR) << "Failed to format minfs: read only block device";
      return zx::error(ZX_ERR_BAD_STATE);
    }

    zx::result mkfs_res = Mkfs(ParseFormatOptions(request->options), bcache.get());
    if (mkfs_res.is_error()) {
      FX_PLOGS(ERROR, mkfs_res.error_value()) << "Failed to format minfs";
      return mkfs_res.take_error();
    }
    return zx::ok();
  }();
  completer.Reply(result);
}

void StartupService::Check(CheckRequestView request, CheckCompleter::Sync& completer) {
  // Use a closure to ensure that any sessions created are destroyed before we respond to the
  // request.
  //
  // TODO(https://fxbug.dev/97783): Consider removing this when multiple sessions are permitted.
  zx::result<> result = [&]() -> zx::result<> {
    std::unique_ptr<block_client::RemoteBlockDevice> device;
    zx_status_t status =
        block_client::RemoteBlockDevice::Create(std::move(request->device), &device);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Could not initialize block device";
      return zx::error(status);
    }

    auto bcache_res = CreateBcache(std::move(device));
    if (bcache_res.is_error()) {
      FX_PLOGS(ERROR, bcache_res.error_value()) << "Could not initialize bcache";
      return bcache_res.take_error();
    }
    auto [bcache, bcache_read_only] = *std::move(bcache_res);

    FsckOptions fsck_options;
    fsck_options.read_only = bcache_read_only;
    fsck_options.repair = !bcache_read_only;
    auto bcache_or = Fsck(std::move(bcache), fsck_options);
    if (bcache_or.is_error()) {
      FX_PLOGS(ERROR, bcache_or.error_value()) << "Consistency check failed for minfs";
      return bcache_or.take_error();
    }
    return zx::ok();
  }();
  completer.Reply(result);
}

}  // namespace minfs
