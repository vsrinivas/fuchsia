// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <fs-management/admin.h>
#include <fs-management/launch.h>
#include <fs-management/mount.h>
#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <ramdevice-client/ramdisk.h>

#include "src/lib/isolated_devmgr/v2_component/fvm.h"
#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"

zx_status_t StartFilesystem(zx_handle_t device_handle, zx::channel* out_data_root) {
  init_options_t init_options = {
      .readonly = false,
      .verbose_mount = false,
      .collect_metrics = false,
      .wait_until_ready = true,
      .enable_journal = true,
      .enable_pager = true,
      .write_compression_algorithm = nullptr,
      .write_compression_level = -1,
      .fsck_after_every_transaction = false,
      .callback = launch_stdio_async,
  };

  // launch the filesystem process
  auto status = fs_init(device_handle, DISK_FORMAT_BLOBFS, &init_options,
                        out_data_root->reset_and_get_address());
  return status;
}

// This function is borrowed from isolated_devmgr.
// isolated_devmgr needs to do some setup work to get access to the /dev directory.
// That is not needed here because we use static routing to provide the /dev directory
// to this component.
zx::status<ramdisk_client_t*> CreateRamDisk(int block_size, int block_count) {
  auto status = zx::make_status(wait_for_device("/dev/misc/ramctl", zx::sec(10).get()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Timed-out waiting for ramctl: " << status.status_string();
    return status.take_error();
  }
  ramdisk_client_t* client;
  status = zx::make_status(ramdisk_create(block_size, block_count, &client));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not create ramdisk for test: " << status.status_string();
    return status.take_error();
  }
  return zx::ok(client);
}

int main(int argc, char** argv) {
  // TODO(xbhatnag): Parameterize these values.
  // ramdisk is approximately 50MB
  uint64_t device_block_size = 512;
  uint64_t device_block_count = 108'544;
  uint64_t fvm_slice_size = 1'048'576;

  FX_LOGS(INFO) << "Creating ramdisk...";
  auto ramdisk_or = CreateRamDisk(device_block_size, device_block_count);
  if (ramdisk_or.is_error()) {
    FX_LOGS(ERROR) << "Error creating ramdisk: " << ramdisk_or.status_string();
    return -1;
  }

  std::string ramdisk_path = ramdisk_get_path(ramdisk_or.value());

  FX_LOGS(INFO) << "Creating FVM partition at " << ramdisk_path;
  auto fvm_partition_or = isolated_devmgr::CreateFvmPartition(ramdisk_path, fvm_slice_size);
  if (fvm_partition_or.is_error()) {
    FX_LOGS(ERROR) << "Error creating FVM partition: " << fvm_partition_or.status_string();
    return -1;
  }

  std::string fvm_device_path = fvm_partition_or.value();

  FX_LOGS(INFO) << "Creating blobfs partition at " << fvm_device_path;
  auto status =
      mkfs(fvm_device_path.c_str(), DISK_FORMAT_BLOBFS, launch_stdio_sync, &default_mkfs_options);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error creating blobfs partition: " << zx_status_get_string(status);
    return -1;
  }

  FX_LOGS(INFO) << "Connecting to FVM block device...";
  zx::channel local, remote, blobfs_export_dir;
  status = zx::channel::create(0, &local, &remote);
  fdio_service_connect(fvm_device_path.c_str(), remote.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not connect to device handle : " << zx_status_get_string(status);
    return -1;
  }

  FX_LOGS(INFO) << "Starting blobfs process...";
  status = StartFilesystem(local.release(), &blobfs_export_dir);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not start filesystem : " << zx_status_get_string(status);
    return -1;
  }

  auto global_loop = new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto outgoing_vfs = fs::ManagedVfs(global_loop->dispatcher());

  FX_LOGS(INFO) << "Creating outgoing dir...";
  zx::channel dir_request(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  auto outgoing_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing_dir->AddEntry("blobfs",
                         fbl::MakeRefCounted<fs::RemoteDir>(std::move(blobfs_export_dir)));
  outgoing_vfs.ServeDirectory(outgoing_dir, std::move(dir_request));

  FX_LOGS(INFO) << "Starting outgoing dir loop...";
  global_loop->Run();
}
