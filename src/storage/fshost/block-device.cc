// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/client_end.h>
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

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fs-management/format.h>
#include <fs-management/mount.h>
#include <gpt/gpt.h>

#include "block-watcher.h"
#include "encrypted-volume.h"
#include "extract-metadata.h"
#include "pkgfs-launcher.h"
#include "src/devices/block/drivers/block-verity/verified-volume-client.h"
#include "src/storage/fshost/block-device-interface.h"
#include "src/storage/fshost/copier.h"
#include "src/storage/fshost/fshost-fs-provider.h"
#include "src/storage/fshost/minfs-manipulator.h"
#include "src/storage/fvm/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs.h"
#include "src/lib/uuid/uuid.h"

namespace fshost {
namespace {

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
  status = vvc->OpenForVerifiedRead(std::move(state->seal), zx::sec(5), inner_block_fd);
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
                      fidl::ServerEnd<fuchsia_io::Directory> export_root = {},
                      fidl::ClientEnd<fuchsia_fxfs::Crypt> crypt_client = {}) {
  FX_CHECK(argv[argv.size() - 1] == nullptr);
  FshostFsProvider fs_provider;
  DevmgrLauncher launcher(&fs_provider);
  zx::process proc;
  int handle_count = 1;
  zx_handle_t handles[3] = {device.TakeChannel().release()};
  uint32_t handle_ids[3] = {FS_HANDLE_BLOCK_DEVICE_ID};
  bool async = false;
  if (export_root) {
    handles[handle_count] = export_root.TakeChannel().release();
    handle_ids[handle_count] = PA_DIRECTORY_REQUEST;
    ++handle_count;
    async = true;
  }
  if (crypt_client) {
    handles[handle_count] = crypt_client.TakeChannel().release();
    handle_ids[handle_count] = PA_HND(PA_USER0, 2);
    ++handle_count;
  }
  if (zx_status_t status = launcher.Launch(
          *zx::job::default_job(), argv[0], argv.data(), nullptr, -1,
          /* TODO(fxbug.dev/32044) */ zx::resource(), handles, handle_ids, handle_count, &proc, 0);
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

// Unmounts the filesystem using fuchsia.fs (rather than DirectoryAdmin).
void Unmount(const fidl::ClientEnd<fuchsia_io::Directory>& export_root) {
  auto admin_or = fidl::CreateEndpoints<fuchsia_fs::Admin>();
  if (admin_or.is_error()) {
    FX_LOGS(ERROR) << "Unable to create fs.Admin endpoints";
    return;
  }
  if (zx_status_t status = fdio_service_connect_at(
          export_root.channel().get(), fidl::DiscoverableProtocolDefaultPath<fuchsia_fs::Admin>,
          std::move(admin_or->server).channel().release());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to connect to fs.Admin";
    return;
  }

  // Ignore errors; there's nothing we can do.
  auto resp = fidl::WireCall(admin_or->client)->Shutdown();
  if (resp.status() != ZX_OK)
    FX_LOGS(ERROR) << "Unmount failed: " << resp.status();
}

// Tries to mount Minfs and reads all data found on the minfs partition.  Errors are ignored.
Copier TryReadingMinfs(fidl::ClientEnd<fuchsia_io::Node> device) {
  fbl::Vector<const char*> argv = {"/pkg/bin/minfs", "mount", nullptr};
  auto export_root_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (export_root_or.is_error())
    return {};
  if (RunBinary(argv, std::move(device), std::move(export_root_or->server)) != ZX_OK)
    return {};

  zx_handle_t root_dir_handle;
  if (zx_status_t status = fs_root_handle(export_root_or->client.channel().get(), &root_dir_handle);
      status != ZX_OK) {
    return {};
  }

  fbl::unique_fd fd;
  if (zx_status_t status = fdio_fd_create(root_dir_handle, fd.reset_and_get_address());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_fd_create failed";
    return {};
  }

  // Clone the handle so that we can unmount.
  if (zx_status_t status = fdio_fd_clone(fd.get(), &root_dir_handle); status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_fd_clone failed";
    return {};
  }

  fidl::ClientEnd<fuchsia_io_admin::DirectoryAdmin> root_dir_client((zx::channel(root_dir_handle)));
  auto unmount = fit::defer([&root_dir_client] { fidl::WireCall(root_dir_client)->Unmount(); });

  if (auto copier_or = Copier::Read(std::move(fd)); copier_or.is_error()) {
    FX_LOGS(ERROR) << "Copier::Read: " << copier_or.status_string();
    return {};
  } else {
    return std::move(copier_or).value();
  }
}

// Sends a message requesting that the device be rebooted without waiting for a response. Returns an
// error if the message could not be sent.
zx::status<> RebootDevice() {
  using fuchsia_hardware_power_statecontrol::Admin;
  using fuchsia_hardware_power_statecontrol::wire::RebootReason;

  auto svc = service::OpenServiceRoot();
  if (svc.is_error()) {
    FX_LOGS(ERROR) << "Failed to open service root: " << svc.status_string();
    return svc.take_error();
  }
  auto client_end = service::ConnectAt<Admin>(*svc);
  if (client_end.is_error()) {
    FX_LOGS(ERROR) << "Failed to connect to fuchsia.hardware.power.statecontrol/Admin: "
                   << client_end.status_string();
    return client_end.take_error();
  }
  // Create an async loop for sending an async Reboot request. Return without running the loop
  // because we cannot wait for the response. The Reboot call waits for component-manager to do an
  // orderly shutdown which includes stopping fshost. If fshost is waiting for the response then it
  // won't shutdown. Component-manager has a 20 minute timeout for shutting down fshost that would
  // be reached if the response was waited for.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireClient client(*std::move(client_end), loop.dispatcher());
  client->Reboot(RebootReason::kFactoryDataReset,
                 [](fidl::WireResponse<Admin::Reboot>*) { /*ignored*/ });
  return zx::ok();
}

}  // namespace

std::string GetTopologicalPath(int fd) {
  fdio_cpp::UnownedFdioCaller disk_connection(fd);
  auto resp = fidl::WireCall<fuchsia_device::Controller>(
                  zx::unowned_channel(disk_connection.borrow_channel()))
                  ->GetTopologicalPath();
  if (resp.status() != ZX_OK) {
    FX_LOGS(WARNING) << "Unable to get topological path (fidl error): "
                     << zx_status_get_string(resp.status());
    return {};
  }
  if (resp->result.is_err()) {
    FX_LOGS(WARNING) << "Unable to get topological path: "
                     << zx_status_get_string(resp->result.err());
    return {};
  }
  const auto& path = resp->result.response().path;
  return {path.data(), path.size()};
}

BlockDevice::BlockDevice(FilesystemMounter* mounter, fbl::unique_fd fd, const Config* device_config)
    : mounter_(mounter),
      fd_(std::move(fd)),
      device_config_(device_config),
      topological_path_(GetTopologicalPath(fd_.get())) {}

disk_format_t BlockDevice::content_format() const {
  if (content_format_) {
    return *content_format_;
  }
  content_format_ = detect_disk_format(fd_.get());
  return *content_format_;
}

disk_format_t BlockDevice::GetFormat() { return format_; }

void BlockDevice::SetFormat(disk_format_t format) { format_ = format; }

const std::string& BlockDevice::partition_name() const {
  if (!partition_name_.empty()) {
    return partition_name_;
  }
  // The block device might not support the partition protocol in which case the connection will be
  // closed, so clone the channel in case that happens.
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  fidl::ClientEnd<fuchsia_hardware_block_partition::Partition> channel(
      zx::channel(fdio_service_clone(connection.borrow_channel())));
  auto resp = fidl::BindSyncClient(std::move(channel)).GetName();
  if (resp.status() != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partiton name (fidl error): "
                   << zx_status_get_string(resp.status());
    return partition_name_;
  }
  if (resp->status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partiton name: " << zx_status_get_string(resp->status);
    return partition_name_;
  }
  partition_name_ = std::string(resp->name.data(), resp->name.size());
  return partition_name_;
}

zx_status_t BlockDevice::GetInfo(fuchsia_hardware_block_BlockInfo* out_info) const {
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

const fuchsia_hardware_block_partition_GUID& BlockDevice::GetInstanceGuid() const {
  if (instance_guid_) {
    return *instance_guid_;
  }
  instance_guid_.emplace();
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  // The block device might not support the partition protocol in which case the connection will be
  // closed, so clone the channel in case that happens.
  zx::channel channel(fdio_service_clone(connection.borrow_channel()));
  zx_status_t io_status, call_status;
  io_status = fuchsia_hardware_block_partition_PartitionGetInstanceGuid(channel.get(), &call_status,
                                                                        &instance_guid_.value());
  if (io_status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partition instance GUID (fidl error: "
                   << zx_status_get_string(io_status) << ")";
  } else if (call_status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partition instance GUID: "
                   << zx_status_get_string(call_status);
  }
  return *instance_guid_;
}

const fuchsia_hardware_block_partition_GUID& BlockDevice::GetTypeGuid() const {
  if (type_guid_) {
    return *type_guid_;
  }
  type_guid_.emplace();
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  // The block device might not support the partition protocol in which case the connection will be
  // closed, so clone the channel in case that happens.
  zx::channel channel(fdio_service_clone(connection.borrow_channel()));
  zx_status_t io_status, call_status;
  io_status = fuchsia_hardware_block_partition_PartitionGetTypeGuid(channel.get(), &call_status,
                                                                    &type_guid_.value());
  if (io_status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partition type GUID (fidl error: "
                   << zx_status_get_string(io_status) << ")";
  } else if (call_status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get partition type GUID: " << zx_status_get_string(call_status);
  }
  return *type_guid_;
}

zx_status_t BlockDevice::AttachDriver(const std::string_view& driver) {
  FX_LOGS(INFO) << "Binding: " << driver;
  fdio_cpp::UnownedFdioCaller connection(fd_.get());
  zx_status_t call_status = ZX_OK;
  auto resp =
      fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(connection.borrow_channel()))
          ->Bind(::fidl::StringView::FromExternal(driver));
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
  FX_LOGS(INFO) << "unsealing zxcrypt with UUID "
                << uuid::Uuid(GetInstanceGuid().value).ToString();
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
  } else {
    thrd_detach(th);
  }
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
  } else {
    // Release our reference to the state now owned by the other thread.
    state.release();
    thrd_detach(th);
  }

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

zx_status_t BlockDevice::SetPartitionMaxSize(const std::string& fvm_path, uint64_t max_size) {
  // Get the partition GUID for talking to FVM.
  const fuchsia_hardware_block_partition_GUID& instance_guid = GetInstanceGuid();
  if (std::all_of(std::begin(instance_guid.value), std::end(instance_guid.value),
                  [](auto val) { return val == 0; }))
    return ZX_ERR_NOT_SUPPORTED;  // Not a partition, nothing to do.

  fbl::unique_fd fvm_fd(open(fvm_path.c_str(), O_RDONLY));
  if (!fvm_fd)
    return ZX_ERR_NOT_SUPPORTED;  // Not in FVM, nothing to do.

  // Actually set the limit.
  fdio_cpp::UnownedFdioCaller caller(fvm_fd.get());
  zx_status_t set_status;
  if (zx_status_t fidl_status = fuchsia_hardware_block_volume_VolumeManagerSetPartitionLimit(
          caller.channel()->get(), &instance_guid, max_size, &set_status);
      fidl_status != ZX_OK || set_status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to set partition limit for " << topological_path() << " to "
                   << max_size << " bytes.";
    FX_LOGS(ERROR) << "  FIDL error: " << zx_status_get_string(fidl_status)
                   << " FVM error: " << zx_status_get_string(set_status);
    return fidl_status != ZX_OK ? fidl_status : set_status;
  }

  return ZX_OK;
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
      FX_LOGS(INFO) << "Skipping blobfs consistency checker.";
      return ZX_OK;
    }

    case DISK_FORMAT_FACTORYFS: {
      FX_LOGS(INFO) << "Skipping factory consistency checker.";
      return ZX_OK;
    }

    case DISK_FORMAT_MINFS: {
      zx::ticks before = zx::ticks::now();
      auto timer = fit::defer([before]() {
        auto after = zx::ticks::now();
        auto duration = fzl::TicksToNs(after - before);
        FX_LOGS(INFO) << "fsck took " << duration.to_secs() << "." << duration.to_msecs() % 1000
                      << " seconds";
      });
      FX_LOGS(INFO) << "fsck of " << disk_format_string(format_) << " started";

      if (device_config_->is_set(Config::kDataFilesystemBinaryPath)) {
        FX_LOGS(INFO) << "Using "
                      << device_config_->ReadStringOptionValue(Config::kDataFilesystemBinaryPath);
        status = CheckFilesystem();
      } else {
        uint64_t device_size = info.block_size * info.block_count / minfs::kMinfsBlockSize;
        std::unique_ptr<block_client::BlockDevice> device;
        status = minfs::FdToBlockDevice(fd_, &device);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Cannot convert fd to block device: " << status;
          return status;
        }
        std::unique_ptr<minfs::Bcache> bc;
        status = minfs::Bcache::Create(std::move(device), static_cast<uint32_t>(device_size), &bc);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Could not initialize minfs bcache.";
          return status;
        }
        status = minfs::Fsck(std::move(bc), minfs::FsckOptions{.repair = true});
      }

      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "\n--------------------------------------------------------------\n"
                          "|\n"
                          "|   WARNING: fshost fsck failure!\n"
                          "|   Corrupt "
                       << disk_format_string(format_)
                       << " filesystem\n"
                          "|\n"
                          "|   If your system was shutdown cleanly (via 'dm poweroff'\n"
                          "|   or an OTA), report this device to the local-storage\n"
                          "|   team. Please file bugs with logs before and after reboot.\n"
                          "|\n"
                          "--------------------------------------------------------------";
        MaybeDumpMetadata(fd_.duplicate(), {.disk_format = DISK_FORMAT_MINFS});
        mounter_->ReportMinfsCorruption();
      } else {
        FX_LOGS(INFO) << "fsck of " << disk_format_string(format_) << " completed OK";
      }
      return status;
    }
    default:
      FX_LOGS(ERROR) << "Not checking unknown filesystem";
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
      FX_LOGS(ERROR) << "Not formatting blobfs.";
      return ZX_ERR_NOT_SUPPORTED;
    }
    case DISK_FORMAT_FACTORYFS: {
      FX_LOGS(ERROR) << "Not formatting factoryfs.";
      return ZX_ERR_NOT_SUPPORTED;
    }
    case DISK_FORMAT_MINFS: {
      const auto binary_path =
          device_config_->ReadStringOptionValue(Config::kDataFilesystemBinaryPath);
      if (!binary_path.empty()) {
        FX_LOGS(INFO) << "Formatting using " << binary_path;
        status = FormatCustomFilesystem(binary_path);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Failed to format: " << zx_status_get_string(status);
          return status;
        }
      } else {
        FX_LOGS(INFO) << "Formatting minfs.";
        uint64_t blocks = info.block_size * info.block_count / minfs::kMinfsBlockSize;
        std::unique_ptr<block_client::BlockDevice> device;
        zx_status_t status = minfs::FdToBlockDevice(fd_, &device);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Cannot convert fd to block device: " << status;
          return status;
        }
        std::unique_ptr<minfs::Bcache> bc;
        status = minfs::Bcache::Create(std::move(device), static_cast<uint32_t>(blocks), &bc);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Could not initialize minfs bcache.";
          return status;
        }
        minfs::MountOptions options = {};
        if ((status = minfs::Mkfs(options, bc.get())) != ZX_OK) {
          FX_LOGS(ERROR) << "Could not format minfs filesystem.";
          return status;
        }
        FX_LOGS(INFO) << "Minfs filesystem re-formatted. Expect data loss.";
      }
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
    case DISK_FORMAT_FACTORYFS: {
      FX_LOGS(INFO) << "BlockDevice::MountFilesystem(factoryfs)";
      MountOptions options;
      options.collect_metrics = false;
      options.readonly = true;

      zx_status_t status = mounter_->MountFactoryFs(std::move(block_device), options);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to mount factoryfs partition: " << zx_status_get_string(status)
                       << ".";
      }
      return status;
    }
    case DISK_FORMAT_BLOBFS: {
      FX_LOGS(INFO) << "BlockDevice::MountFilesystem(blobfs)";
      MountOptions options;
      options.collect_metrics = true;
      std::optional<std::string> algorithm = std::nullopt;
      std::optional<std::string> eviction_policy = std::nullopt;
      if (mounter_->boot_args()) {
        algorithm = mounter_->boot_args()->blobfs_write_compression_algorithm();
        if (algorithm == "ZSTD" || algorithm == "ZSTD_SEEKABLE") {
          // These two algorithms are deprecated.
          FX_LOGS(INFO) << "Ignoring " << *algorithm << " algorithm";
          algorithm = std::nullopt;
        }
        eviction_policy = mounter_->boot_args()->blobfs_eviction_policy();
      }
      options.write_compression_algorithm = algorithm ? algorithm->c_str() : nullptr;
      options.cache_eviction_policy = eviction_policy ? eviction_policy->c_str() : nullptr;
      if (device_config_->is_set(Config::kSandboxDecompression)) {
        options.sandbox_decompression = true;
      }
      zx_status_t status = mounter_->MountBlob(std::move(block_device), options);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to mount blobfs partition: " << zx_status_get_string(status)
                       << ".";
        return status;
      }
      mounter_->TryMountPkgfs();
      return ZX_OK;
    }
    case DISK_FORMAT_MINFS: {
      MountOptions options;
      FX_LOGS(INFO) << "BlockDevice::MountFilesystem(data partition)";
      zx_status_t status = MountData(&options);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to mount data partition: " << zx_status_get_string(status) << ".";
        MaybeDumpMetadata(fd_.duplicate(), {.disk_format = DISK_FORMAT_MINFS});
        return status;
      }
      mounter_->TryMountPkgfs();
      return ZX_OK;
    }
    default:
      FX_LOGS(ERROR) << "BlockDevice::MountFilesystem(unknown)";
      return ZX_ERR_NOT_SUPPORTED;
  }
}

// Attempt to mount the device at a known location.
//
// Returns ZX_ERR_ALREADY_BOUND if the device could be mounted, but something
// is already mounted at that location. Returns ZX_ERR_INVALID_ARGS if the
// GUID of the device does not match a known valid one. Returns
// ZX_ERR_NOT_SUPPORTED if the GUID is a system GUID. Returns ZX_OK if an
// attempt to mount is made, without checking mount success.
zx_status_t BlockDevice::MountData(MountOptions* options) {
  fuchsia_hardware_block_partition_GUID type_guid;
  zx_status_t io_status, status;
  zx::channel block_device = CloneDeviceChannel();
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
    uint64_t minfs_max_size = device_config_->ReadUint64OptionValue(Config::kMinfsMaxBytes, 0);
    bool using_custom_fs = device_config_->is_set(Config::kDataFilesystemBinaryPath);
    if (!using_custom_fs && minfs_max_size > 0) {
      auto excluded_paths = ParseExcludedPaths(
          device_config_->ReadStringOptionValue(Config::kMinfsResizeExcludedPaths));
      constexpr uint64_t kMinfsResizeRequiredInodes = 4096;
      constexpr uint64_t kMinfsResizeDataSizeLimit = 10223616;  // 9.75 * 1024 * 1024
      MaybeResizeMinfsResult result =
          MaybeResizeMinfs(CloneDeviceChannel(), minfs_max_size, kMinfsResizeRequiredInodes,
                           kMinfsResizeDataSizeLimit, excluded_paths, mounter_->inspect_manager());
      switch (result) {
        case MaybeResizeMinfsResult::kMinfsMountable:
          break;
        case MaybeResizeMinfsResult::kRebootRequired:
          // fshost if a critical process so if requesting a reboot fails then try crashing fshost.
          ZX_ASSERT_MSG(RebootDevice().is_ok(), "Failed to request a reboot");
          return ZX_ERR_CANCELED;
      }
    }
    return mounter_->MountData(std::move(block_device), *options);
  } else if (gpt_is_install_guid(type_guid.value, GPT_GUID_LEN)) {
    options->readonly = true;
    return mounter_->MountInstall(std::move(block_device), *options);
  } else if (gpt_is_durable_guid(type_guid.value, GPT_GUID_LEN)) {
    return mounter_->MountDurable(std::move(block_device), *options);
  }
  FX_LOGS(ERROR) << "Unrecognized partition GUID for data partition; not mounting";
  return ZX_ERR_WRONG_TYPE;
}

zx::channel BlockDevice::CloneDeviceChannel() const {
  fdio_cpp::UnownedFdioCaller caller(fd_.get());
  return zx::channel(fdio_service_clone(caller.borrow_channel()));
}

zx_status_t BlockDeviceInterface::Add(bool format_on_corruption) {
  switch (GetFormat()) {
    case DISK_FORMAT_NAND_BROKER: {
      return AttachDriver(kNandBrokerDriverPath);
    }
    case DISK_FORMAT_BOOTPART: {
      return AttachDriver(kBootpartDriverPath);
    }
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
    case DISK_FORMAT_FACTORYFS: {
      if (zx_status_t status = CheckFilesystem(); status != ZX_OK) {
        return status;
      }

      return MountFilesystem();
    }
    case DISK_FORMAT_ZXCRYPT: {
      return UnsealZxcrypt();
    }
    case DISK_FORMAT_BLOBFS: {
      if (zx_status_t status = CheckFilesystem(); status != ZX_OK) {
        return status;
      }
      return MountFilesystem();
    }
    case DISK_FORMAT_MINFS: {
      FX_LOGS(INFO) << "mounting data partition: format on corruption is "
                    << (format_on_corruption ? "enabled" : "disabled");
      if (zx_status_t status = CheckFilesystem(); status != ZX_OK) {
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
        if (status == ZX_ERR_CANCELED) {
          // If mounting was canceled then don't try to format minfs or mount again.
          return status;
        }
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
    case DISK_FORMAT_FAT:
    case DISK_FORMAT_VBMETA:
    case DISK_FORMAT_UNKNOWN:
    case DISK_FORMAT_FXFS:
    case DISK_FORMAT_F2FS:
    case DISK_FORMAT_COUNT_:
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
  if (zx_status_t status =
          fidl::WireCall<fuchsia_io::Node>(zx::unowned_channel(caller.borrow_channel()))
              ->Clone(fuchsia_io::wire::kCloneFlagSameRights, std::move(end_points_or->server))
              .status();
      status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(end_points_or->client));
}

zx_status_t BlockDevice::CheckCustomFilesystem(const std::string& binary_path) const {
  fbl::Vector<const char*> argv;
  argv.push_back(binary_path.c_str());
  argv.push_back("fsck");
  argv.push_back(nullptr);
  auto device_or = GetDeviceEndPoint();
  if (device_or.is_error()) {
    return device_or.error_value();
  }
  auto crypt_client_or = mounter_->GetCryptClient();
  if (crypt_client_or.is_error())
    return crypt_client_or.error_value();
  return RunBinary(argv, std::move(device_or).value(), {}, *std::move(crypt_client_or));
}

// This is a destructive operation and isn't atomic (i.e. not resilient to power interruption).
zx_status_t BlockDevice::FormatCustomFilesystem(const std::string& binary_path) const {
  // Try mounting minfs and slurp all existing data off.
  zx_handle_t handle;
  if (zx_status_t status = fdio_fd_clone(fd_.get(), &handle); status != ZX_OK)
    return status;
  fbl::unique_fd fd;
  if (zx_status_t status = fdio_fd_create(handle, fd.reset_and_get_address()); status != ZX_OK)
    return status;

  mounter_->inspect_manager().LogMinfsUpgradeProgress(
      InspectManager::MinfsUpgradeState::kReadOldPartition);
  Copier copier;
  {
    auto device_or = GetDeviceEndPoint();
    if (device_or.is_error())
      return device_or.error_value();
    copier = TryReadingMinfs(std::move(device_or).value());
  }

  fidl::ClientEnd<fuchsia_io::Node> device;
  if (auto device_or = GetDeviceEndPoint(); device_or.is_error()) {
    return device_or.error_value();
  } else {
    device = std::move(device_or).value();
  }

  fidl::UnownedClientEnd<fuchsia_hardware_block_volume::Volume> volume_client(
      device.channel().borrow());

  auto query_result = fidl::WireCall(volume_client)->Query();
  if (query_result.status() != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to query FVM information: "
                   << zx_status_get_string(query_result.status());
    return query_result.status();
  }

  auto query_response = query_result.Unwrap();
  if (query_response->status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to query FVM information: "
                   << zx_status_get_string(query_response->status);
    return query_response->status;
  }

  const uint64_t slice_size = query_response->info->slice_size;

  // Free all the existing slices.
  mounter_->inspect_manager().LogMinfsUpgradeProgress(
      InspectManager::MinfsUpgradeState::kWriteNewPartition);

  uint64_t slice = 1;
  // The -1 here is because of zxcrypt; zxcrypt will offset all slices by 1 to account for its
  // header.  zxcrypt isn't present in all cases, but that won't matter since minfs shouldn't be
  // using a slice so high.
  while (slice < fvm::kMaxVSlices - 1) {
    auto query_result = fidl::WireCall(volume_client)
                            ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(&slice, 1));
    if (query_result.status() != ZX_OK) {
      FX_LOGS(ERROR) << "Unable to query slices (slice: " << slice << ", max: " << fvm::kMaxVSlices
                     << "): " << zx_status_get_string(query_result.status());
      return query_result.status();
    }

    auto query_response = query_result.Unwrap();
    if (query_response->status != ZX_OK) {
      FX_LOGS(ERROR) << "Unable to query slices (slice: " << slice << ", max: " << fvm::kMaxVSlices
                     << "): " << zx_status_get_string(query_response->status);
      return query_response->status;
    }

    if (query_response->response_count == 0) {
      break;
    }

    for (uint64_t i = 0; i < query_response->response_count; ++i) {
      if (query_response->response[i].allocated) {
        auto shrink_result =
            fidl::WireCall(volume_client)->Shrink(slice, query_response->response[i].count);
        if (zx_status_t status = shrink_result.status() == ZX_OK ? shrink_result.Unwrap()->status
                                                                 : shrink_result.status();
            status != ZX_OK) {
          FX_LOGS(ERROR) << "Unable to shrink partition: " << zx_status_get_string(status);
          return status;
        }
      }
      slice += query_response->response[i].count;
    }
  }

  // -1 because zxcrypt steals a slice.
  int slice_count =
      device_config_->ReadUint64OptionValue(Config::kMinfsMaxBytes, 0) / slice_size - 1;

  if (slice_count < 0) {
    auto query_result = fidl::WireCall(volume_client)->Query();
    if (query_result.status() != ZX_OK)
      return query_result.status();
    const auto* response = query_result.Unwrap();
    if (response->status != ZX_OK)
      return response->status;
    // If a size is not specified, limit the size of the data partition so as not to use up all
    // FVM's space (thus limiting blobfs growth).  24 MiB should be enough.
    slice_count =
        std::min(24 * 1024 * 1024 / slice_size - 1,
                 response->info->pslice_total_count - response->info->pslice_allocated_count);
  }

  auto extend_result =
      fidl::WireCall(volume_client)
          ->Extend(1, slice_count - 1);  // Another -1 here because we get the first slice for free.
  if (zx_status_t status =
          extend_result.status() == ZX_OK ? extend_result.Unwrap()->status : extend_result.status();
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to extend partition (slice_count: " << slice_count
                   << "): " << zx_status_get_string(status);
    return status;
  }

  fbl::Vector<const char*> argv = {binary_path.c_str(), "mkfs", nullptr};

  auto crypt_client_or = mounter_->GetCryptClient();
  crypt_client_or = mounter_->GetCryptClient();
  if (crypt_client_or.is_error())
    return crypt_client_or.error_value();
  if (zx_status_t status = RunBinary(argv, std::move(device), {}, *std::move(crypt_client_or));
      status != ZX_OK) {
    return status;
  }

  // Now mount and then copy all the data back.
  if (zx_status_t status = fdio_fd_clone(fd_.get(), &handle); status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_fd_clone failed";
    return status;
  }
  if (zx_status_t status = fdio_fd_create(handle, fd.reset_and_get_address()); status != ZX_OK)
    return status;

  auto export_root_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (export_root_or.is_error())
    return export_root_or.error_value();

  argv[1] = "mount";
  auto device_or = GetDeviceEndPoint();
  if (device_or.is_error()) {
    return device_or.error_value();
  }
  crypt_client_or = mounter_->GetCryptClient();
  if (crypt_client_or.is_error())
    return crypt_client_or.error_value();
  if (zx_status_t status =
          RunBinary(argv, std::move(device_or).value(), std::move(export_root_or->server),
                    *std::move(crypt_client_or));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to mount after format";
    return status;
  }

  zx::channel root_client, root_server;
  if (zx_status_t status = zx::channel::create(0, &root_client, &root_server); status != ZX_OK)
    return status;

  if (auto resp =
          fidl::WireCall(export_root_or->client)
              ->Open(fuchsia_io::wire::kOpenRightReadable | fuchsia_io::wire::kOpenFlagPosix, 0,
                     fidl::StringView("root"), std::move(root_server));
      !resp.ok()) {
    return resp.status();
  }

  if (zx_status_t status = fdio_fd_create(root_client.release(), fd.reset_and_get_address());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_fd_create failed";
    return status;
  }

  if (zx_status_t status = copier.Write(std::move(fd)); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to copy data";
    return status;
  }

  Unmount(export_root_or->client);

  mounter_->inspect_manager().LogMinfsUpgradeProgress(InspectManager::MinfsUpgradeState::kFinished);

  return ZX_OK;
}

}  // namespace fshost
