// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device.h"

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <inttypes.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/time.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <minfs/fsck.h>
#include <minfs/minfs.h>

#include "block-watcher.h"
#include "encrypted-volume.h"
#include "pkgfs-launcher.h"
#include "src/devices/block/drivers/block-verity/verified-volume-client.h"

namespace devmgr {
namespace {

const char kAllowAuthoringFactoryConfigFile[] = "/boot/config/allow-authoring-factory";

// Attempt to mount the device pointed to be the file descriptor at a known
// location.
//
// Returns ZX_ERR_ALREADY_BOUND if the device could be mounted, but something
// is already mounted at that location. Returns ZX_ERR_INVALID_ARGS if the
// GUID of the device does not match a known valid one. Returns
// ZX_ERR_NOT_SUPPORTED if the GUID is a system GUID. Returns ZX_OK if an
// attempt to mount is made, without checking mount success.
zx_status_t MountMinfs(FilesystemMounter* mounter, zx::channel block_device,
                       mount_options_t* options) {
  fuchsia_hardware_block_partition_GUID type_guid;
  zx_status_t io_status, status;
  io_status = fuchsia_hardware_block_partition_PartitionGetTypeGuid(block_device.get(), &status,
                                                                    &type_guid);
  if (io_status != ZX_OK) {
    return io_status;
  }
  if (status != ZX_OK) {
    return status;
  }

  if (gpt_is_sys_guid(type_guid.value, GPT_GUID_LEN)) {
    return ZX_ERR_NOT_SUPPORTED;
  } else if (gpt_is_data_guid(type_guid.value, GPT_GUID_LEN)) {
    return mounter->MountData(std::move(block_device), *options);
  } else if (gpt_is_install_guid(type_guid.value, GPT_GUID_LEN)) {
    options->readonly = true;
    return mounter->MountInstall(std::move(block_device), *options);
  } else if (gpt_is_durable_guid(type_guid.value, GPT_GUID_LEN)) {
    return mounter->MountDurable(std::move(block_device), *options);
  }
  printf("fshost: Unrecognized partition GUID for minfs; not mounting\n");
  return ZX_ERR_WRONG_TYPE;
}

// return value is ignored
int UnsealZxcryptThread(void* arg) {
  std::unique_ptr<int> fd_ptr(static_cast<int*>(arg));
  fbl::unique_fd fd(*fd_ptr);
  fbl::unique_fd devfs_root(open("/dev", O_RDONLY));
  EncryptedVolume volume(std::move(fd), std::move(devfs_root));
  volume.EnsureUnsealedAndFormatIfNeeded();
  return 0;
}

// Holds thread state for OpenVerityDeviceThread
struct VerityDeviceThreadState {
  fbl::unique_fd fd;
  digest::Digest seal;
};

// return value is ignored
int OpenVerityDeviceThread(void* arg) {
  std::unique_ptr<VerityDeviceThreadState> state(static_cast<VerityDeviceThreadState*>(arg));
  fbl::unique_fd devfs_root(open("/dev", O_RDONLY));

  std::unique_ptr<block_verity::VerifiedVolumeClient> vvc;
  zx_status_t status = block_verity::VerifiedVolumeClient::CreateFromBlockDevice(
      state->fd.get(), std::move(devfs_root),
      block_verity::VerifiedVolumeClient::Disposition::kDriverAlreadyBound, zx::sec(5), &vvc);
  if (status != ZX_OK) {
    printf("fshost: Couldn't create VerifiedVolumeClient: %s\n", zx_status_get_string(status));
    return 1;
  }

  fbl::unique_fd inner_block_fd;
  status = vvc->OpenForVerifiedRead(std::move(state->seal), zx::sec(5), inner_block_fd);
  if (status != ZX_OK) {
    printf("fshost: OpenForVerifiedRead failed: %s\n", zx_status_get_string(status));
    return 1;
  }
  return 0;
}

}  // namespace

BlockDevice::BlockDevice(FilesystemMounter* mounter, fbl::unique_fd fd)
    : mounter_(mounter), fd_(std::move(fd)), format_(detect_disk_format(fd_.get())) {}

disk_format_t BlockDevice::GetFormat() { return format_; }

void BlockDevice::SetFormat(disk_format_t format) { format_ = format; }

bool BlockDevice::Netbooting() { return mounter_->Netbooting(); }

zx_status_t BlockDevice::GetInfo(fuchsia_hardware_block_BlockInfo* out_info) {
  if (info_.has_value()) {
    memcpy(out_info, &*info_, sizeof(*out_info));
    return ZX_OK;
  }
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  zx_status_t io_status, call_status;
  io_status =
      fuchsia_hardware_block_BlockGetInfo(connection.borrow_channel(), &call_status, out_info);
  if (io_status != ZX_OK) {
    return io_status;
  }
  info_ = *out_info;
  return call_status;
}

zx_status_t BlockDevice::GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) {
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  zx_status_t io_status, call_status;
  io_status = fuchsia_hardware_block_partition_PartitionGetTypeGuid(connection.borrow_channel(),
                                                                    &call_status, out_guid);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return call_status;
}

zx_status_t BlockDevice::AttachDriver(const std::string_view& driver) {
  printf("fshost: Binding: %.*s\n", static_cast<int>(driver.length()), driver.data());
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  zx_status_t call_status = ZX_OK;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(connection.borrow_channel()),
      ::fidl::unowned_str(driver.data(), driver.length()));
  zx_status_t io_status = resp.status();
  if (io_status != ZX_OK) {
    return io_status;
  }
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  return call_status;
}

zx_status_t BlockDevice::UnsealZxcrypt() {
  printf("fshost: unsealing zxcrypt\n");
  // Bind and unseal the driver from a separate thread, since we
  // have to wait for a number of devices to do I/O and settle,
  // and we don't want to block block-watcher for any nontrivial
  // length of time.

  // We transfer fd to the spawned thread.  Since it's UB to cast
  // ints to pointers and back, we allocate the fd on the heap.
  int loose_fd = fd_.release();
  int* raw_fd_ptr = new int(loose_fd);
  thrd_t th;
  int err = thrd_create_with_name(&th, &UnsealZxcryptThread, raw_fd_ptr, "zxcrypt-unseal");
  if (err != thrd_success) {
    printf("fshost: failed to spawn zxcrypt worker thread\n");
    close(loose_fd);
    delete raw_fd_ptr;
    return ZX_ERR_INTERNAL;
  } else {
    thrd_detach(th);
  }
  return ZX_OK;
}

zx_status_t BlockDevice::OpenBlockVerityForVerifiedRead(std::string seal_hex) {
  printf("fshost: preparing block-verity\n");

  std::unique_ptr<VerityDeviceThreadState> state = std::make_unique<VerityDeviceThreadState>();
  zx_status_t rc = state->seal.Parse(seal_hex.c_str());
  if (rc != ZX_OK) {
    printf("block-verity seal %s did not parse as SHA256 hex digest: %s\n", seal_hex.c_str(),
           zx_status_get_string(rc));
    return rc;
  }

  // Transfer FD to thread state.
  state->fd = std::move(fd_);

  thrd_t th;
  int err = thrd_create_with_name(&th, OpenVerityDeviceThread, state.get(), "block-verity-open");
  if (err != thrd_success) {
    printf("fshost: failed to spawn block-verity worker thread\n");
    return ZX_ERR_INTERNAL;
  } else {
    // Release our reference to the state now owned by the other thread.
    state.release();
    thrd_detach(th);
  }

  return ZX_OK;
}

zx_status_t BlockDevice::IsTopologicalPathSuffix(const std::string_view& expected_path,
                                                 bool* is_path) {
  zx_status_t call_status;
  fbl::StringBuffer<PATH_MAX> path;
  path.Resize(path.capacity());
  size_t path_len;
  fdio_cpp::UnownedFdioCaller disk_connection(fd_.get());
  auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
      zx::unowned_channel(disk_connection.borrow_channel()));
  zx_status_t status = resp.status();

  if (status != ZX_OK) {
    return ZX_ERR_NOT_FOUND;
  }
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  } else {
    call_status = ZX_OK;
    auto& r = resp->result.response();
    path_len = r.path.size();
    if (path_len > PATH_MAX) {
      return ZX_ERR_INTERNAL;
    }
    memcpy(path.data(), r.path.data(), r.path.size());
  }

  if (call_status != ZX_OK) {
    return call_status;
  }
  if (path_len < expected_path.length()) {
    *is_path = false;
  } else {
    *is_path =
        std::string_view(path.begin() + path_len - expected_path.length()).compare(expected_path) ==
        0;
  }
  return ZX_OK;
}

zx_status_t BlockDevice::IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) {
  // Both the zxcrypt and minfs partitions have the same gpt guid, so here we
  // determine which it actually is. We do this by looking up the topological
  // path.
  return IsTopologicalPathSuffix(std::string_view("/zxcrypt/unsealed/block"), is_unsealed_zxcrypt);
}

zx_status_t BlockDevice::FormatZxcrypt() {
  fbl::unique_fd devfs_root_fd(open("/dev", O_RDONLY));
  if (!devfs_root_fd) {
    return ZX_ERR_NOT_FOUND;
  }
  EncryptedVolume volume(fd_.duplicate(), std::move(devfs_root_fd));
  return volume.Format();
}

zx::status<std::string> BlockDevice::VeritySeal() {
  return mounter_->boot_args()->block_verity_seal();
}

bool BlockDevice::ShouldAllowAuthoringFactory() {
  // Checks for presence of /boot/config/allow-authoring-factory
  fbl::unique_fd allow_authoring_factory_fd(open(kAllowAuthoringFactoryConfigFile, O_RDONLY));
  return allow_authoring_factory_fd.is_valid();
}

bool BlockDevice::ShouldCheckFilesystems() { return mounter_->ShouldCheckFilesystems(); }

zx_status_t BlockDevice::CheckFilesystem() {
  if (!ShouldCheckFilesystems()) {
    return ZX_OK;
  }

  zx_status_t status;
  fuchsia_hardware_block_BlockInfo info;
  if ((status = GetInfo(&info)) != ZX_OK) {
    return status;
  }

  switch (format_) {
    case DISK_FORMAT_BLOBFS: {
      fprintf(stderr, "fshost: Skipping blobfs consistency checker.\n");
      return ZX_OK;
    }

    case DISK_FORMAT_FACTORYFS: {
      fprintf(stderr, "fshost: Skipping factory consistency checker.\n");
      return ZX_OK;
    }

    case DISK_FORMAT_MINFS: {
      zx::ticks before = zx::ticks::now();
      auto timer = fbl::MakeAutoCall([before]() {
        auto after = zx::ticks::now();
        auto duration = fzl::TicksToNs(after - before);
        printf("fshost: fsck took %" PRId64 ".%" PRId64 " seconds\n", duration.to_secs(),
               duration.to_msecs() % 1000);
      });
      printf("fshost: fsck of %s started\n", disk_format_string_[format_]);
      uint64_t device_size = info.block_size * info.block_count / minfs::kMinfsBlockSize;
      std::unique_ptr<block_client::BlockDevice> device;
      zx_status_t status = minfs::FdToBlockDevice(fd_, &device);
      if (status != ZX_OK) {
        fprintf(stderr, "fshost: Cannot convert fd to block device: %d\n", status);
        return status;
      }
      std::unique_ptr<minfs::Bcache> bc;
      status = minfs::Bcache::Create(std::move(device), static_cast<uint32_t>(device_size), &bc);
      if (status != ZX_OK) {
        fprintf(stderr, "fshost: Could not initialize minfs bcache.\n");
        return status;
      }
      status = minfs::Fsck(std::move(bc), minfs::FsckOptions{.repair = true});

      if (status != ZX_OK) {
        mounter_->mutable_metrics()->LogMinfsCorruption();
        mounter_->FlushMetrics();
        fprintf(stderr, "--------------------------------------------------------------\n");
        fprintf(stderr, "|                                                             \n");
        fprintf(stderr, "|   WARNING: fshost fsck failure!                             \n");
        fprintf(stderr, "|   Corrupt %s filesystem\n", disk_format_string_[format_]);
        fprintf(stderr, "|                                                             \n");
        fprintf(stderr, "|   If your system was shutdown cleanly (via 'dm poweroff'    \n");
        fprintf(stderr, "|   or an OTA), report this device to the local-storage       \n");
        fprintf(stderr, "|   team. Please file bugs with logs before and after reboot. \n");
        fprintf(stderr, "|   Please use the 'filesystem' and 'minfs' component tag.    \n");
        fprintf(stderr, "|                                                             \n");
        fprintf(stderr, "--------------------------------------------------------------\n");
      } else {
        printf("fshost: fsck of %s completed OK\n", disk_format_string_[format_]);
      }
      return status;
    }
    default:
      fprintf(stderr, "fshost: Not checking unknown filesystem\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t BlockDevice::FormatFilesystem() {
  zx_status_t status;
  fuchsia_hardware_block_BlockInfo info;
  if ((status = GetInfo(&info)) != ZX_OK) {
    return status;
  }

  switch (format_) {
    case DISK_FORMAT_BLOBFS: {
      fprintf(stderr, "fshost: Not formatting blobfs.\n");
      return ZX_ERR_NOT_SUPPORTED;
    }
    case DISK_FORMAT_FACTORYFS: {
      fprintf(stderr, "fshost: Not formatting factoryfs.\n");
      return ZX_ERR_NOT_SUPPORTED;
    }
    case DISK_FORMAT_MINFS: {
      fprintf(stderr, "fshost: Formatting minfs.\n");
      uint64_t blocks = info.block_size * info.block_count / minfs::kMinfsBlockSize;
      std::unique_ptr<block_client::BlockDevice> device;
      zx_status_t status = minfs::FdToBlockDevice(fd_, &device);
      if (status != ZX_OK) {
        fprintf(stderr, "fshost: Cannot convert fd to block device: %d\n", status);
        return status;
      }
      std::unique_ptr<minfs::Bcache> bc;
      status = minfs::Bcache::Create(std::move(device), static_cast<uint32_t>(blocks), &bc);
      if (status != ZX_OK) {
        fprintf(stderr, "fshost: Could not initialize minfs bcache.\n");
        return status;
      }
      minfs::MountOptions options = {};
      if ((status = minfs::Mkfs(options, bc.get())) != ZX_OK) {
        fprintf(stderr, "fshost: Could not format minfs filesystem.\n");
        return status;
      }
      printf("fshost: Minfs filesystem re-formatted. Expect data loss.\n");
      return ZX_OK;
    }
    default:
      fprintf(stderr, "fshost: Not formatting unknown filesystem.\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t BlockDevice::MountFilesystem() {
  if (!fd_) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx::channel block_device;
  {
    fdio_cpp::UnownedFdioCaller disk_connection(fd_.get());
    zx::unowned_channel channel(disk_connection.borrow_channel());
    block_device.reset(fdio_service_clone(channel->get()));
  }

  switch (format_) {
    case DISK_FORMAT_FACTORYFS: {
      fprintf(stderr, "fshost: BlockDevice::MountFilesystem(factoryfs)\n");
      mount_options_t options = default_mount_options;
      options.enable_journal = false;
      options.collect_metrics = false;
      options.readonly = true;

      zx_status_t status = mounter_->MountFactoryFs(std::move(block_device), options);
      if (status != ZX_OK) {
        printf("fshost: Failed to mount factoryfs partition: %s.\n", zx_status_get_string(status));
      }
      return status;
    }
    case DISK_FORMAT_BLOBFS: {
      fprintf(stderr, "fshost: BlockDevice::MountFilesystem(blobfs)\n");
      mount_options_t options = default_mount_options;
      options.enable_journal = true;
      options.collect_metrics = true;
      if (mounter_->boot_args()) {
        options.enable_pager = mounter_->boot_args()->blobfs_enable_userpager();
        auto algorithm = mounter_->boot_args()->blobfs_write_compression_algorithm();
        if (algorithm) {
          options.write_compression_algorithm = algorithm->c_str();
        }
      }
      zx_status_t status = mounter_->MountBlob(std::move(block_device), options);
      if (status != ZX_OK) {
        printf("fshost: Failed to mount blobfs partition: %s.\n", zx_status_get_string(status));
        return status;
      }
      mounter_->TryMountPkgfs();
      return ZX_OK;
    }
    case DISK_FORMAT_MINFS: {
      mount_options_t options = default_mount_options;
      fprintf(stderr, "fshost: BlockDevice::MountFilesystem(minfs)\n");
      zx_status_t status = MountMinfs(mounter_, std::move(block_device), &options);
      if (status != ZX_OK) {
        printf("fshost: Failed to mount minfs partition: %s.\n", zx_status_get_string(status));
        return status;
      }
      mounter_->TryMountPkgfs();
      return ZX_OK;
    }
    default:
      fprintf(stderr, "fshost: BlockDevice::MountFilesystem(unknown)\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t BlockDeviceInterface::Add() {
  disk_format_t df = GetFormat();
  fuchsia_hardware_block_BlockInfo info;
  zx_status_t status;
  if ((status = GetInfo(&info)) != ZX_OK) {
    return status;
  }

  if (info.flags & BLOCK_FLAG_BOOTPART) {
    return AttachDriver(kBootpartDriverPath);
  }

  switch (df) {
    case DISK_FORMAT_GPT: {
      return AttachDriver(kGPTDriverPath);
    }
    case DISK_FORMAT_FVM: {
      return AttachDriver(kFVMDriverPath);
    }
    case DISK_FORMAT_MBR: {
      return AttachDriver(kMBRDriverPath);
    }
    case DISK_FORMAT_BLOCK_VERITY: {
      zx_status_t rc = AttachDriver(kBlockVerityDriverPath);
      if (rc != ZX_OK) {
        return rc;
      }

      if (!ShouldAllowAuthoringFactory()) {
        zx::status<std::string> seal_text = VeritySeal();
        if (seal_text.is_error()) {
          printf("Couldn't get block-verity seal: %s\n", seal_text.status_string());
          return seal_text.error_value();
        }

        return OpenBlockVerityForVerifiedRead(seal_text.value());
      }

      return ZX_OK;
    }
    case DISK_FORMAT_ZXCRYPT: {
      if (!Netbooting()) {
        return UnsealZxcrypt();
      }
      return ZX_OK;
    }
    default:
      break;
  }

  fuchsia_hardware_block_partition_GUID guid;
  if ((status = GetTypeGUID(&guid)) != ZX_OK) {
    return status;
  }

  // If we're in netbooting mode, then only bind drivers for partition
  // containers and the install partition, not regular filesystems.
  if (Netbooting()) {
    if (gpt_is_install_guid(guid.value, GPT_GUID_LEN)) {
      printf("fshost: mounting install partition\n");
      return MountFilesystem();
    }
    return ZX_OK;
  }

  switch (df) {
    case DISK_FORMAT_BLOBFS: {
      const uint8_t expected_guid[GPT_GUID_LEN] = GUID_BLOB_VALUE;
      if (memcmp(guid.value, expected_guid, GPT_GUID_LEN)) {
        return ZX_ERR_INVALID_ARGS;
      }
      if ((status = CheckFilesystem()) != ZX_OK) {
        return status;
      }

      return MountFilesystem();
    }
    case DISK_FORMAT_MINFS: {
      printf("fshost: mounting minfs\n");

      const uint8_t expected_data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
      const uint8_t expected_durable_guid[GPT_GUID_LEN] = GPT_DURABLE_TYPE_GUID;
      if (((memcmp(guid.value, expected_data_guid, GPT_GUID_LEN)) &&
           (memcmp(guid.value, expected_durable_guid, GPT_GUID_LEN)))) {
        return ZX_ERR_INVALID_ARGS;
      }

      if (CheckFilesystem() != ZX_OK) {
        if ((status = FormatFilesystem()) != ZX_OK) {
          return status;
        }
      }
      status = MountFilesystem();
      if (status != ZX_OK) {
        printf("fshost: failed to mount filesystem: %s\n", zx_status_get_string(status));
        return status;
      }
      return ZX_OK;
    }
    case DISK_FORMAT_FACTORYFS: {
      const uint8_t expected_guid[GPT_GUID_LEN] = GPT_FACTORY_TYPE_GUID;
      if (memcmp(guid.value, expected_guid, GPT_GUID_LEN)) {
        return ZX_ERR_INVALID_ARGS;
      }
      if ((status = CheckFilesystem()) != ZX_OK) {
        return status;
      }

      return MountFilesystem();
    }
    default:
      // If the disk format is unknown but we know it should be the data
      // partition, format the disk properly.
      if (gpt_is_data_guid(guid.value, GPT_GUID_LEN)) {
        printf("fshost: Data partition has unknown format\n");
        bool is_unsealed_zxcrypt;
        if (IsUnsealedZxcrypt(&is_unsealed_zxcrypt) != ZX_OK) {
          return ZX_ERR_NOT_SUPPORTED;
        }
        if (is_unsealed_zxcrypt) {
          printf("fshost: Formatting as minfs partition\n");
          SetFormat(DISK_FORMAT_MINFS);
          status = FormatFilesystem();
          if (status != ZX_OK) {
            return status;
          }
        } else {
          printf("fshost: Formatting as zxcrypt partition\n");
          SetFormat(DISK_FORMAT_ZXCRYPT);
          status = FormatZxcrypt();
          if (status != ZX_OK) {
            return status;
          }
        }
        return Add();
      }

      // if we know it's supposed to be a factoryfs partition, bind the block-verity driver.
      if (gpt_is_factory_guid(guid.value, GPT_GUID_LEN)) {
        bool is_already_bound;
        if (IsTopologicalPathSuffix(std::string_view("/mutable/block"), &is_already_bound) !=
            ZX_OK) {
          return ZX_ERR_NOT_SUPPORTED;
        }
        if (is_already_bound) {
          // This is the child device of a block device that already has the
          // verity driver bound.  Don't bind, lest we cause a second layer of indirection!
          return ZX_OK;
        }

        if (IsTopologicalPathSuffix(std::string_view("/verified/block"), &is_already_bound) !=
            ZX_OK) {
          return ZX_ERR_NOT_SUPPORTED;
        }
        if (is_already_bound) {
          // This is the child device of a block device that already has the
          // verity driver bound.  Don't bind, lest we cause a second layer of indirection!
          return ZX_OK;
        }

        printf("fshost: binding block-verity driver\n");
        return AttachDriver(kBlockVerityDriverPath);
      }

      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace devmgr
