// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device.manager/cpp/markers.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <inttypes.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/time.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cctype>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <gpt/gpt.h>
#include <gpt/guid.h>

#include "block-watcher.h"
#include "constants.h"
#include "encrypted-volume.h"
#include "extract-metadata.h"
#include "src/devices/block/drivers/block-verity/verified-volume-client.h"
#include "src/lib/files/file.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/fs_management/cpp/options.h"
#include "src/lib/uuid/uuid.h"
#include "src/storage/fshost/block-device-interface.h"
#include "src/storage/fshost/fxfs.h"
#include "src/storage/fshost/utils.h"
#include "src/storage/fvm/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs.h"

namespace fshost {
namespace {

using fs_management::DiskFormat;

const char kAllowAuthoringFactoryConfigFile[] = "/boot/config/allow-authoring-factory";

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
    FX_LOGS(ERROR) << "Couldn't create VerifiedVolumeClient: " << zx_status_get_string(status);
    return 1;
  }

  fbl::unique_fd inner_block_fd;
  status = vvc->OpenForVerifiedRead(state->seal, zx::sec(5), inner_block_fd);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "OpenForVerifiedRead failed: " << zx_status_get_string(status);
    return 1;
  }
  return 0;
}

// Runs the binary indicated in `argv`, which must always be terminated with nullptr.
// `device_channel`, containing a handle to the block device, is passed to the binary.  If
// `export_root` is specified, the binary is launched asynchronously.  Otherwise, this waits for the
// binary to terminate and returns the status.
zx_status_t RunBinary(const fbl::Vector<const char*>& argv,
                      fidl::ClientEnd<fuchsia_io::Node> device,
                      fidl::ServerEnd<fuchsia_io::Directory> export_root = {}) {
  FX_CHECK(argv[argv.size() - 1] == nullptr);
  zx::process proc;
  int handle_count = 1;
  zx_handle_t handles[2] = {device.TakeChannel().release()};
  uint32_t handle_ids[2] = {FS_HANDLE_BLOCK_DEVICE_ID};
  bool async = false;
  if (export_root) {
    handles[handle_count] = export_root.TakeChannel().release();
    handle_ids[handle_count] = PA_DIRECTORY_REQUEST;
    ++handle_count;
    async = true;
  }
  if (zx_status_t status = Launch(*zx::job::default_job(), argv[0], argv.data(), nullptr, -1,
                                  /* TODO(fxbug.dev/32044) */ zx::resource(), handles, handle_ids,
                                  handle_count, &proc);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to launch binary: " << argv[0];
    return status;
  }

  if (async)
    return ZX_OK;

  if (zx_status_t status = proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Error waiting for process to terminate";
    return status;
  }

  zx_info_process_t info;
  if (zx_status_t status = proc.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get process info";
    return status;
  }

  if (!(info.flags & ZX_INFO_PROCESS_FLAG_EXITED) || info.return_code != 0) {
    FX_LOGS(ERROR) << "flags: " << info.flags << ", return_code: " << info.return_code;
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

Copier TryReadingFilesystem(fidl::ClientEnd<fuchsia_io::Directory> export_root) {
  auto root_dir_or = fs_management::FsRootHandle(export_root);
  if (root_dir_or.is_error())
    return {};

  fbl::unique_fd fd;
  if (zx_status_t status =
          fdio_fd_create(root_dir_or->TakeChannel().release(), fd.reset_and_get_address());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_fd_create failed: " << zx_status_get_string(status);
    return {};
  }

  // Clone the handle so that we can unmount.
  zx::channel root_dir_handle;
  if (zx_status_t status = fdio_fd_clone(fd.get(), root_dir_handle.reset_and_get_address());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_fd_clone failed: " << zx_status_get_string(status);
    return {};
  }

  fidl::ClientEnd<fuchsia_io::Directory> root_dir_client(std::move(root_dir_handle));
  auto unmount = fit::defer([&export_root] {
    auto admin_client = service::ConnectAt<fuchsia_fs::Admin>(export_root);
    if (admin_client.is_ok()) {
      [[maybe_unused]] auto result = fidl::WireCall(*admin_client)->Shutdown();
    }
  });

  auto copier_or = Copier::Read(std::move(fd));
  if (copier_or.is_error()) {
    FX_LOGS(ERROR) << "Copier::Read: " << copier_or.status_string();
    return {};
  }
  return std::move(copier_or).value();
}

// Tries to mount Minfs and reads all data found on the minfs partition.  Errors are ignored.
Copier TryReadingMinfs(fidl::ClientEnd<fuchsia_io::Node> device) {
  fbl::Vector<const char*> argv = {kMinfsPath, "mount", nullptr};
  auto export_root_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (export_root_or.is_error())
    return {};
  if (RunBinary(argv, std::move(device), std::move(export_root_or->server)) != ZX_OK)
    return {};
  return TryReadingFilesystem(std::move(export_root_or->client));
}

}  // namespace

std::string GetTopologicalPath(int fd) {
  fdio_cpp::UnownedFdioCaller disk_connection(fd);
  auto resp =
      fidl::WireCall(disk_connection.borrow_as<fuchsia_device::Controller>())->GetTopologicalPath();
  if (resp.status() != ZX_OK) {
    FX_LOGS(WARNING) << "Unable to get topological path (fidl error): "
                     << zx_status_get_string(resp.status());
    return {};
  }
  if (resp->is_error()) {
    FX_LOGS(WARNING) << "Unable to get topological path: "
                     << zx_status_get_string(resp->error_value());
    return {};
  }
  const auto& path = resp->value()->path;
  return {path.data(), path.size()};
}

fs_management::MountOptions GetBlobfsMountOptions(const fshost_config::Config& config,
                                                  const FshostBootArgs* boot_args) {
  fs_management::MountOptions options;
  options.component_child_name = "blobfs";
  options.write_compression_level = -1;
  options.sandbox_decompression = config.sandbox_decompression();
  if (boot_args) {
    if (boot_args->blobfs_write_compression_algorithm()) {
      // Ignore invalid options.
      if (boot_args->blobfs_write_compression_algorithm() == "ZSTD_CHUNKED" ||
          boot_args->blobfs_write_compression_algorithm() == "UNCOMPRESSED") {
        options.write_compression_algorithm = boot_args->blobfs_write_compression_algorithm();
      }
    }
    if (boot_args->blobfs_eviction_policy()) {
      // Ignore invalid options.
      if (boot_args->blobfs_eviction_policy() == "NEVER_EVICT" ||
          boot_args->blobfs_eviction_policy() == "EVICT_IMMEDIATELY") {
        options.cache_eviction_policy = boot_args->blobfs_eviction_policy();
      }
    }
  }
  return options;
}

BlockDevice::BlockDevice(FilesystemMounter* mounter, fbl::unique_fd fd,
                         const fshost_config::Config* device_config)
    : mounter_(mounter),
      device_config_(device_config),
      fd_(std::move(fd)),
      content_format_(fs_management::kDiskFormatUnknown),
      topological_path_(GetTopologicalPath(fd_.get())) {}

zx::status<std::unique_ptr<BlockDeviceInterface>> BlockDevice::OpenBlockDevice(
    const char* topological_path) const {
  fbl::unique_fd fd(open(topological_path, O_RDWR, S_IFBLK));
  if (!fd) {
    FX_LOGS(WARNING) << "Failed to open block device " << topological_path << ": "
                     << strerror(errno);
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return OpenBlockDeviceByFd(std::move(fd));
}

zx::status<std::unique_ptr<BlockDeviceInterface>> BlockDevice::OpenBlockDeviceByFd(
    fbl::unique_fd fd) const {
  return zx::ok(std::make_unique<BlockDevice>(mounter_, std::move(fd), device_config_));
}

void BlockDevice::AddData(Copier copier) { source_data_ = std::move(copier); }

zx::status<Copier> BlockDevice::ExtractData() {
  if (content_format() != fs_management::kDiskFormatMinfs) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  auto device_or = GetDeviceEndPoint();
  if (device_or.is_error())
    return device_or.take_error();
  return zx::ok(TryReadingMinfs(std::move(device_or).value()));
}

DiskFormat BlockDevice::content_format() const {
  if (content_format_ != fs_management::kDiskFormatUnknown) {
    return content_format_;
  }
  content_format_ = fs_management::DetectDiskFormat(fd_.get());
  return content_format_;
}

DiskFormat BlockDevice::GetFormat() { return format_; }

void BlockDevice::SetFormat(DiskFormat format) { format_ = format; }

const std::string& BlockDevice::partition_name() const {
  if (!partition_name_.empty()) {
    return partition_name_;
  }
  // The block device might not support the partition protocol in which case the connection will
  // be closed, so clone the channel in case that happens.
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  fidl::ClientEnd<fuchsia_hardware_block_partition::Partition> channel(
      zx::channel(fdio_service_clone(connection.borrow_channel())));
  auto resp = fidl::WireSyncClient(std::move(channel))->GetName();
  if (resp.status() != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partiton name (fidl error): "
                   << zx_status_get_string(resp.status());
    return partition_name_;
  }
  if (resp.value().status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partiton name: " << zx_status_get_string(resp.value().status);
    return partition_name_;
  }
  partition_name_ = std::string(resp.value().name.data(), resp.value().name.size());
  return partition_name_;
}

zx::status<fuchsia_hardware_block::wire::BlockInfo> BlockDevice::GetInfo() const {
  if (info_.has_value()) {
    return zx::ok(*info_);
  }
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  auto res = fidl::WireCall(connection.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  if (!res.ok()) {
    return zx::error(res.status());
  }
  if (res->status != ZX_OK) {
    return zx::error(res->status);
  }
  info_ = *res->info.get();
  return zx::ok(*info_);
}

const fuchsia_hardware_block_partition::wire::Guid& BlockDevice::GetInstanceGuid() const {
  if (instance_guid_) {
    return *instance_guid_;
  }
  instance_guid_.emplace();
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  // The block device might not support the partition protocol in which case the connection will
  // be closed, so clone the channel in case that happens.
  auto response = fidl::WireCall(fidl::ClientEnd<fuchsia_hardware_block_partition::Partition>(
                                     zx::channel(fdio_service_clone(connection.borrow_channel()))))
                      ->GetInstanceGuid();
  if (response.status() != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partition instance GUID (fidl error: "
                   << zx_status_get_string(response.status()) << ")";
  } else if (response.value().status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partition instance GUID: "
                   << zx_status_get_string(response.value().status);
  } else {
    *instance_guid_ = *response.value().guid;
  }
  return *instance_guid_;
}

const fuchsia_hardware_block_partition::wire::Guid& BlockDevice::GetTypeGuid() const {
  if (type_guid_) {
    return *type_guid_;
  }
  type_guid_.emplace();
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  // The block device might not support the partition protocol in which case the connection will
  // be closed, so clone the channel in case that happens.
  auto response = fidl::WireCall(fidl::ClientEnd<fuchsia_hardware_block_partition::Partition>(
                                     zx::channel(fdio_service_clone(connection.borrow_channel()))))
                      ->GetTypeGuid();
  if (response.status() != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partition type GUID (fidl error: "
                   << zx_status_get_string(response.status()) << ")";
  } else if (response.value().status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partition type GUID: "
                   << zx_status_get_string(response.value().status);
  } else {
    *type_guid_ = *response.value().guid;
  }
  return *type_guid_;
}

zx_status_t BlockDevice::AttachDriver(const std::string_view& driver) {
  FX_LOGS(INFO) << "Binding: " << driver;
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  auto resp = fidl::WireCall(connection.borrow_as<fuchsia_device::Controller>())
                  ->Bind(::fidl::StringView::FromExternal(driver));
  zx_status_t io_status = resp.status();
  if (io_status != ZX_OK) {
    return io_status;
  }
  if (resp->is_error()) {
    FX_PLOGS(ERROR, resp->error_value()) << "Failed to attach driver: " << driver;
    return resp->error_value();
  }
  return ZX_OK;
}

zx_status_t BlockDevice::UnsealZxcrypt() {
  FX_LOGS(INFO) << "unsealing zxcrypt with UUID "
                << uuid::Uuid(GetInstanceGuid().value.data()).ToString();
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
    FX_LOGS(ERROR) << "failed to spawn zxcrypt worker thread";
    close(loose_fd);
    delete raw_fd_ptr;
    return ZX_ERR_INTERNAL;
  }
  thrd_detach(th);

  return ZX_OK;
}

zx_status_t BlockDevice::OpenBlockVerityForVerifiedRead(std::string seal_hex) {
  FX_LOGS(INFO) << "preparing block-verity";

  std::unique_ptr<VerityDeviceThreadState> state = std::make_unique<VerityDeviceThreadState>();
  zx_status_t rc = state->seal.Parse(seal_hex.c_str());
  if (rc != ZX_OK) {
    FX_LOGS(ERROR) << "block-verity seal " << seal_hex
                   << " did not parse as SHA256 hex digest: " << zx_status_get_string(rc);
    return rc;
  }

  // Transfer FD to thread state.
  state->fd = std::move(fd_);

  thrd_t th;
  int err = thrd_create_with_name(&th, OpenVerityDeviceThread, state.get(), "block-verity-open");
  if (err != thrd_success) {
    FX_LOGS(ERROR) << "failed to spawn block-verity worker thread";
    return ZX_ERR_INTERNAL;
  }
  // Release our reference to the state now owned by the other thread.
  static_cast<void>(state.release());
  thrd_detach(th);

  return ZX_OK;
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

bool BlockDevice::IsRamDisk() const {
  auto ramdisk_prefix = device_config_->ramdisk_prefix();
  ZX_DEBUG_ASSERT(!ramdisk_prefix.empty());
  return topological_path().compare(0, ramdisk_prefix.length(), ramdisk_prefix) == 0;
}

zx_status_t BlockDevice::SetPartitionMaxSize(const std::string& fvm_path, uint64_t max_byte_size) {
  // Get the partition GUID for talking to FVM.
  const fuchsia_hardware_block_partition::wire::Guid& instance_guid = GetInstanceGuid();
  if (std::all_of(std::begin(instance_guid.value), std::end(instance_guid.value),
                  [](auto val) { return val == 0; }))
    return ZX_ERR_NOT_SUPPORTED;  // Not a partition, nothing to do.

  fbl::unique_fd fvm_fd(open(fvm_path.c_str(), O_RDONLY));
  if (!fvm_fd)
    return ZX_ERR_NOT_SUPPORTED;  // Not in FVM, nothing to do.
  fdio_cpp::UnownedFdioCaller fvm_caller(fvm_fd.get());

  // Get the FVM slice size.
  auto info_response =
      fidl::WireCall(fidl::UnownedClientEnd<fuchsia_hardware_block_volume::VolumeManager>(
                         fvm_caller.borrow_channel()))
          ->GetInfo();
  if (info_response.status() != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to request FVM Info: "
                   << zx_status_get_string(info_response.status());
    return info_response.status();
  }
  if (info_response.value().status != ZX_OK || !info_response.value().info) {
    FX_LOGS(ERROR) << "FVM info request failed: "
                   << zx_status_get_string(info_response.value().status);
    return info_response.value().status;
  }
  uint64_t slice_size = info_response.value().info->slice_size;

  // Set the limit (convert to slice units, rounding down).
  uint64_t max_slice_count = max_byte_size / slice_size;
  auto response =
      fidl::WireCall(fidl::UnownedClientEnd<fuchsia_hardware_block_volume::VolumeManager>(
                         fvm_caller.borrow_channel()))
          ->SetPartitionLimit(instance_guid, max_slice_count);
  if (response.status() != ZX_OK || response.value().status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to set partition limit for " << topological_path() << " to "
                   << max_byte_size << " bytes (" << max_slice_count << " slices).";
    if (response.status() != ZX_OK) {
      FX_LOGS(ERROR) << "  FIDL error: " << zx_status_get_string(response.status());
      return response.status();
    }
    FX_LOGS(ERROR) << " FVM error: " << zx_status_get_string(response.value().status);
    return response.value().status;
  }

  return ZX_OK;
}

zx_status_t BlockDevice::SetPartitionName(const std::string& fvm_path, std::string_view name) {
  // Get the partition GUID for talking to FVM.
  const fuchsia_hardware_block_partition::wire::Guid& instance_guid = GetInstanceGuid();
  if (std::all_of(std::begin(instance_guid.value), std::end(instance_guid.value),
                  [](auto val) { return val == 0; }))
    return ZX_ERR_NOT_SUPPORTED;  // Not a partition, nothing to do.

  fbl::unique_fd fvm_fd(open(fvm_path.c_str(), O_RDONLY));
  if (!fvm_fd)
    return ZX_ERR_NOT_SUPPORTED;  // Not in FVM, nothing to do.

  // Actually set the name.
  fdio_cpp::UnownedFdioCaller caller(fvm_fd.get());
  auto response =
      fidl::WireCall(fidl::UnownedClientEnd<fuchsia_hardware_block_volume::VolumeManager>(
                         caller.borrow_channel()))
          ->SetPartitionName(instance_guid, fidl::StringView::FromExternal(name));
  if (response.status() != ZX_OK || response->is_error()) {
    FX_LOGS(ERROR) << "Unable to set partition name for " << topological_path() << " to '" << name
                   << "'.";
    if (response.status() != ZX_OK) {
      FX_LOGS(ERROR) << "  FIDL error: " << zx_status_get_string(response.status());
      return response.status();
    }
    FX_LOGS(ERROR) << " FVM error: " << zx_status_get_string(response->error_value());
    return response->error_value();
  }

  return ZX_OK;
}

bool BlockDevice::ShouldCheckFilesystems() { return mounter_->ShouldCheckFilesystems(); }

zx_status_t BlockDevice::CheckFilesystem() {
  if (!ShouldCheckFilesystems()) {
    return ZX_OK;
  }

  zx::status info = GetInfo();
  if (info.is_error()) {
    return info.status_value();
  }

  const std::array<DiskFormat, 3> kFormatsToCheck = {
      fs_management::kDiskFormatMinfs,
      fs_management::kDiskFormatF2fs,
      fs_management::kDiskFormatFxfs,
  };
  if (std::find(kFormatsToCheck.begin(), kFormatsToCheck.end(), format_) == kFormatsToCheck.end()) {
    FX_LOGS(INFO) << "Skipping consistency checker for partition of type "
                  << DiskFormatString(format_);
    return ZX_OK;
  }

  zx::ticks before = zx::ticks::now();
  auto timer = fit::defer([before]() {
    auto after = zx::ticks::now();
    auto duration = fzl::TicksToNs(after - before);
    FX_LOGS(INFO) << "fsck took " << duration.to_secs() << "." << duration.to_msecs() % 1000
                  << " seconds";
  });
  FX_LOGS(INFO) << "fsck of " << DiskFormatString(format_) << " partition started";

  zx_status_t status;
  switch (format_) {
    case fs_management::kDiskFormatF2fs:
    case fs_management::kDiskFormatFxfs: {
      status = CheckCustomFilesystem(format_);
      break;
    }
    case fs_management::kDiskFormatMinfs: {
      // With minfs, we can run the library directly without needing to start a new process.
      uint64_t device_size = info->block_size * info->block_count / minfs::kMinfsBlockSize;
      auto device_or = minfs::FdToBlockDevice(fd_);
      if (device_or.is_error()) {
        FX_LOGS(ERROR) << "Cannot convert fd to block device: " << device_or.error_value();
        return device_or.error_value();
      }
      auto bc_or =
          minfs::Bcache::Create(std::move(device_or.value()), static_cast<uint32_t>(device_size));
      if (bc_or.is_error()) {
        FX_LOGS(ERROR) << "Could not initialize minfs bcache.";
        return bc_or.error_value();
      }
      status =
          minfs::Fsck(std::move(bc_or.value()), minfs::FsckOptions{.repair = true}).status_value();
      break;
    }
    default:
      __builtin_unreachable();
  }
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "\n--------------------------------------------------------------\n"
                      "|\n"
                      "|   WARNING: fshost fsck failure!\n"
                      "|   Corrupt "
                   << DiskFormatString(format_)
                   << " filesystem\n"
                      "|\n"
                      "|   Please file a bug to the Storage component in http://fxbug.dev,\n"
                      "|   including a device snapshot collected with `ffx target snapshot` if\n"
                      "|   possible.\n"
                      "|\n"
                      "--------------------------------------------------------------";
    MaybeDumpMetadata(fd_.duplicate(), {.disk_format = format_});
    mounter_->ReportPartitionCorrupted(format_);
  } else {
    FX_LOGS(INFO) << "fsck of " << DiskFormatString(format_) << " completed OK";
  }
  return status;
}

zx_status_t BlockDevice::FormatFilesystem() {
  zx::status info = GetInfo();
  if (info.is_error()) {
    return info.status_value();
  }

  // There might be a previously cached content format; forget that now since it could change.
  content_format_ = fs_management::kDiskFormatUnknown;

  switch (format_) {
    case fs_management::kDiskFormatBlobfs: {
      FX_LOGS(ERROR) << "Not formatting blobfs.";
      return ZX_ERR_NOT_SUPPORTED;
    }
    case fs_management::kDiskFormatFactoryfs: {
      FX_LOGS(ERROR) << "Not formatting factoryfs.";
      return ZX_ERR_NOT_SUPPORTED;
    }
    case fs_management::kDiskFormatFxfs:
    case fs_management::kDiskFormatF2fs: {
      zx_status_t status = FormatCustomFilesystem(format_);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to format: " << zx_status_get_string(status);
      }
      return status;
    }
    case fs_management::kDiskFormatMinfs: {
      // With minfs, we can run the library directly without needing to start a new process.
      FX_LOGS(INFO) << "Formatting minfs.";
      uint64_t blocks = info->block_size * info->block_count / minfs::kMinfsBlockSize;
      auto device_or = minfs::FdToBlockDevice(fd_);
      if (device_or.is_error()) {
        FX_LOGS(ERROR) << "Cannot convert fd to block device: " << device_or.error_value();
        return device_or.status_value();
      }
      auto bc_or =
          minfs::Bcache::Create(std::move(device_or.value()), static_cast<uint32_t>(blocks));
      if (bc_or.is_error()) {
        FX_LOGS(ERROR) << "Could not initialize minfs bcache.";
        return bc_or.error_value();
      }
      minfs::MountOptions options = {};
      if (zx_status_t status = minfs::Mkfs(options, bc_or.value().get()).status_value();
          status != ZX_OK) {
        FX_LOGS(ERROR) << "Could not format minfs filesystem.";
        return status;
      }
      FX_LOGS(INFO) << "Minfs filesystem re-formatted. Expect data loss.";
      return ZX_OK;
    }
    default:
      FX_LOGS(ERROR) << "Not formatting unknown filesystem.";
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
    case fs_management::kDiskFormatFactoryfs: {
      FX_LOGS(INFO) << "BlockDevice::MountFilesystem(factoryfs)";
      fs_management::MountOptions options;
      options.readonly = true;

      zx_status_t status = mounter_->MountFactoryFs(std::move(block_device), options);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to mount factoryfs partition: " << zx_status_get_string(status)
                       << ".";
      }
      return status;
    }
    case fs_management::kDiskFormatBlobfs: {
      FX_LOGS(INFO) << "BlockDevice::MountFilesystem(blobfs)";
      if (zx_status_t status = mounter_->MountBlob(
              std::move(block_device),
              GetBlobfsMountOptions(*device_config_, mounter_->boot_args().get()));
          status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Failed to mount blobfs partition";
        return status;
      }
      return ZX_OK;
    }
    case fs_management::kDiskFormatFxfs:
    case fs_management::kDiskFormatF2fs:
    case fs_management::kDiskFormatMinfs: {
      fs_management::MountOptions options;

      std::optional<Copier> copier = std::move(source_data_);
      source_data_.reset();

      FX_LOGS(INFO) << "BlockDevice::MountFilesystem(data partition)";
      if (zx_status_t status = MountData(options, std::move(copier), std::move(block_device));
          status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to mount data partition: " << zx_status_get_string(status) << ".";
        MaybeDumpMetadata(fd_.duplicate(), {.disk_format = format_});
        return status;
      }
      return ZX_OK;
    }
    default:
      FX_LOGS(ERROR) << "BlockDevice::MountFilesystem(unknown)";
      return ZX_ERR_NOT_SUPPORTED;
  }
}

// Attempt to mount the device at a known location.
//
// If |copier| is set, the data will be copied into the data filesystem before exposing the
// filesystem to clients.  This is only supported for the data guid (i.e. not the durable guid).
//
// Returns ZX_ERR_ALREADY_BOUND if the device could be mounted, but something
// is already mounted at that location. Returns ZX_ERR_INVALID_ARGS if the
// GUID of the device does not match a known valid one. Returns
// ZX_ERR_NOT_SUPPORTED if the GUID is a system GUID. Returns ZX_OK if an
// attempt to mount is made, without checking mount success.
zx_status_t BlockDevice::MountData(const fs_management::MountOptions& options,
                                   std::optional<Copier> copier, zx::channel block_device) {
  const uint8_t* guid = GetTypeGuid().value.data();
  FX_LOGS(INFO) << "Detected type GUID " << gpt::KnownGuid::TypeDescription(guid)
                << " for data partition";

  if (gpt_is_sys_guid(guid, GPT_GUID_LEN)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (gpt_is_data_guid(guid, GPT_GUID_LEN)) {
    return mounter_->MountData(std::move(block_device), std::move(copier), options, format_);
  }
  if (gpt_is_durable_guid(guid, GPT_GUID_LEN)) {
    if (copier) {
      FX_LOGS(ERROR) << "Copier is not supported for durable partitions";
      return ZX_ERR_NOT_SUPPORTED;
    }
    return mounter_->MountDurable(std::move(block_device), options);
  }
  FX_LOGS(ERROR) << "Unrecognized type GUID for data partition; not mounting";
  return ZX_ERR_WRONG_TYPE;
}

zx_status_t BlockDeviceInterface::Add(bool format_on_corruption) {
  switch (GetFormat()) {
    case fs_management::kDiskFormatNandBroker: {
      return AttachDriver(kNandBrokerDriverPath);
    }
    case fs_management::kDiskFormatBootpart: {
      return AttachDriver(kBootpartDriverPath);
    }
    case fs_management::kDiskFormatGpt: {
      return AttachDriver(kGPTDriverPath);
    }
    case fs_management::kDiskFormatFvm: {
      return AttachDriver(kFVMDriverPath);
    }
    case fs_management::kDiskFormatMbr: {
      return AttachDriver(kMBRDriverPath);
    }
    case fs_management::kDiskFormatBlockVerity: {
      if (zx_status_t status = AttachDriver(kBlockVerityDriverPath); status != ZX_OK) {
        return status;
      }

      if (!ShouldAllowAuthoringFactory()) {
        zx::status<std::string> seal_text = VeritySeal();
        if (seal_text.is_error()) {
          FX_LOGS(ERROR) << "Couldn't get block-verity seal: " << seal_text.status_string();
          return seal_text.error_value();
        }

        return OpenBlockVerityForVerifiedRead(seal_text.value());
      }

      return ZX_OK;
    }
    case fs_management::kDiskFormatFactoryfs: {
      if (zx_status_t status = CheckFilesystem(); status != ZX_OK) {
        return status;
      }

      return MountFilesystem();
    }
    case fs_management::kDiskFormatZxcrypt: {
      return UnsealZxcrypt();
    }
    case fs_management::kDiskFormatBlobfs: {
      if (zx_status_t status = CheckFilesystem(); status != ZX_OK) {
        return status;
      }
      return MountFilesystem();
    }
    case fs_management::kDiskFormatFxfs:
    case fs_management::kDiskFormatF2fs:
    case fs_management::kDiskFormatMinfs: {
      FX_LOGS(INFO) << "mounting data partition with format " << DiskFormatString(GetFormat())
                    << ": format on corruption is "
                    << (format_on_corruption ? "enabled" : "disabled");
      if (content_format() != GetFormat()) {
        FX_LOGS(INFO) << "Data doesn't appear to be formatted yet.  Formatting...";
        if (zx_status_t status = FormatFilesystem(); status != ZX_OK) {
          return status;
        }
      } else if (zx_status_t status = CheckFilesystem(); status != ZX_OK) {
        if (!format_on_corruption) {
          FX_LOGS(INFO) << "formatting data partition on this target is disabled";
          return status;
        }
        if (zx_status_t status = FormatFilesystem(); status != ZX_OK) {
          return status;
        }
      }
      if (zx_status_t status = MountFilesystem(); status != ZX_OK) {
        FX_LOGS(ERROR) << "failed to mount filesystem: " << zx_status_get_string(status);
        if (!format_on_corruption) {
          FX_LOGS(ERROR) << "formatting minfs on this target is disabled";
          return status;
        }
        if ((status = FormatFilesystem()) != ZX_OK) {
          return status;
        }
        return MountFilesystem();
      }
      return ZX_OK;
    }
    case fs_management::kDiskFormatFat:
    case fs_management::kDiskFormatVbmeta:
    case fs_management::kDiskFormatUnknown:
    case fs_management::kDiskFormatCount:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

// Clones the device handle.
zx::status<fidl::ClientEnd<fuchsia_io::Node>> BlockDevice::GetDeviceEndPoint() const {
  auto end_points_or = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (end_points_or.is_error())
    return end_points_or.take_error();

  fdio_cpp::UnownedFdioCaller caller(fd_);
  if (zx_status_t status = fidl::WireCall(caller.borrow_as<fuchsia_io::Node>())
                               ->Clone(fuchsia_io::wire::OpenFlags::kCloneSameRights,
                                       std::move(end_points_or->server))
                               .status();
      status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(end_points_or->client));
}

zx_status_t BlockDevice::CheckCustomFilesystem(fs_management::DiskFormat format) const {
  auto device_or = GetDeviceEndPoint();
  if (device_or.is_error()) {
    return device_or.error_value();
  }

  if (format == fs_management::kDiskFormatFxfs) {
    // Fxfs runs as a component.
    constexpr char startup_service_path[] = "/fxfs/svc/fuchsia.fs.startup.Startup";
    auto startup_client_end = service::Connect<fuchsia_fs_startup::Startup>(startup_service_path);
    if (startup_client_end.is_error()) {
      FX_PLOGS(ERROR, startup_client_end.error_value())
          << "Failed to connect to startup service at " << startup_service_path;
      return startup_client_end.error_value();
    }
    auto startup_client = fidl::WireSyncClient(std::move(*startup_client_end));
    fidl::ClientEnd<fuchsia_hardware_block::Block> block_client_end(device_or->TakeChannel());
    fs_management::FsckOptions options;
    auto res = startup_client->Check(std::move(block_client_end), options.as_check_options());
    if (!res.ok()) {
      FX_PLOGS(ERROR, res.status()) << "Failed to fsck (FIDL error)";
      return res.status();
    }
    if (res.value().is_error()) {
      FX_PLOGS(ERROR, res.value().error_value()) << "Fsck failed";
      return res.value().error_value();
    }
    return ZX_OK;
  }
  const std::string binary_path(BinaryPathForFormat(format));
  if (binary_path.empty()) {
    FX_LOGS(ERROR) << "Unsupported data format";
    return ZX_ERR_INVALID_ARGS;
  }

  return RunBinary({binary_path.c_str(), "fsck", nullptr}, std::move(device_or).value());
}

// This is a destructive operation and isn't atomic (i.e. not resilient to power interruption).
zx_status_t BlockDevice::FormatCustomFilesystem(fs_management::DiskFormat format) {
  // Try mounting minfs and slurp all existing data off.
  if (content_format() == fs_management::kDiskFormatMinfs) {
    FX_LOGS(INFO) << "Attempting to read existing Minfs data";
    auto device_or = GetDeviceEndPoint();
    if (device_or.is_error())
      return device_or.error_value();
    if (Copier copier = TryReadingMinfs(std::move(device_or).value()); !copier.empty()) {
      FX_LOGS(INFO) << "Successfully read Minfs data";
      source_data_.emplace(std::move(copier));
    }
  }

  FX_LOGS(INFO) << "Formatting " << DiskFormatString(format);
  fidl::ClientEnd<fuchsia_io::Node> device;
  if (auto device_or = GetDeviceEndPoint(); device_or.is_error()) {
    return device_or.error_value();
  } else {
    device = std::move(device_or).value();
  }

  fidl::UnownedClientEnd<fuchsia_hardware_block_volume::Volume> volume_client(
      device.channel().borrow());
  uint64_t target_bytes = device_config_->data_max_bytes();
  if (format == fs_management::kDiskFormatF2fs) {
    // f2fs always requires at least a certain size.
    target_bytes = std::max(target_bytes, kDefaultF2fsMinBytes);
  }
  const bool inside_zxcrypt = (topological_path_.find("zxcrypt") != std::string::npos);
  FX_LOGS(INFO) << "Resizing data volume, target = " << target_bytes << " bytes";
  auto result = ResizeVolume(volume_client, target_bytes, inside_zxcrypt);
  if (result.is_error()) {
  }
  auto actual_size = ResizeVolume(volume_client, target_bytes, inside_zxcrypt);
  if (actual_size.is_error()) {
    FX_PLOGS(ERROR, actual_size.status_value()) << "Failed to resize data volume";
    return result.status_value();
  }
  if (format == fs_management::kDiskFormatF2fs && *actual_size < kDefaultF2fsMinBytes) {
    FX_LOGS(ERROR) << "Only allocated " << *actual_size << " bytes but needed "
                   << kDefaultF2fsMinBytes;
    return ZX_ERR_NO_SPACE;
  } else if (*actual_size < target_bytes) {
    FX_LOGS(WARNING) << "Only allocated " << *actual_size << " bytes";
  }

  if (format == fs_management::kDiskFormatFxfs) {
    auto block_device = GetDeviceEndPoint();
    if (block_device.is_error()) {
      FX_PLOGS(ERROR, block_device.status_value()) << "Failed to get device endpoint";
      return block_device.status_value();
    }
    if (auto status = FormatFxfsAndInitDataVolume(
            fidl::ClientEnd<fuchsia_hardware_block::Block>(std::move(block_device)->TakeChannel()),
            *device_config_);
        status.is_error()) {
      FX_PLOGS(ERROR, status.status_value()) << "Failed to format Fxfs";
      return status.status_value();
    }
  } else {
    const std::string binary_path(BinaryPathForFormat(format));
    if (binary_path.empty()) {
      FX_LOGS(ERROR) << "Unsupported data format";
      return ZX_ERR_INVALID_ARGS;
    }

    if (zx_status_t status = RunBinary({binary_path.c_str(), "mkfs", nullptr}, std::move(device));
        status != ZX_OK) {
      return status;
    }
  }
  content_format_ = format_;

  return ZX_OK;
}

}  // namespace fshost
