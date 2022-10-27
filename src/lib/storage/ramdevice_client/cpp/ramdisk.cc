// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.hardware.ramdisk/cpp/wire.h>
#include <inttypes.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramdisk.h>

constexpr char kRamctlDevPath[] = "/dev";
constexpr char kRamctlPath[] = "sys/platform/00:00:2d/ramctl";
constexpr char kBlockExtension[] = "block";

static zx_status_t driver_watcher_cb(int dirfd, int event, const char* fn, void* cookie) {
  char* wanted = static_cast<char*>(cookie);
  if (event == WATCH_EVENT_ADD_FILE && strcmp(fn, wanted) == 0) {
    return ZX_ERR_STOP;
  }
  return ZX_OK;
}

static zx_status_t wait_for_device_impl(int dir_fd, char* path, const zx::time& deadline) {
  // Peel off last path segment
  char* sep = strrchr(path, '/');
  if (path[0] == '\0' || (!sep)) {
    fprintf(stderr, "invalid device path '%s'\n", path);
    return ZX_ERR_BAD_PATH;
  }
  char* last = sep + 1;

  *sep = '\0';
  auto restore_path = fit::defer([sep] { *sep = '/'; });

  // Recursively check the path up to this point
  struct stat buf;
  if (fstatat(dir_fd, path, &buf, 0) != 0) {
    if (zx_status_t status = wait_for_device_impl(dir_fd, path, deadline); status != ZX_OK) {
      fprintf(stderr, "failed to bind '%s': %s\n", path, zx_status_get_string(status));
      return status;
    }
  }

  // Early exit if this segment is empty
  if (last[0] == '\0') {
    return ZX_OK;
  }

  // Open the parent directory
  fbl::unique_fd parent_dir(openat(dir_fd, path, O_RDONLY | O_DIRECTORY));
  if (!parent_dir) {
    fprintf(stderr, "unable to open '%s'\n", path);
    return ZX_ERR_NOT_FOUND;
  }

  // Wait for the next path segment to show up
  if (zx_status_t status =
          fdio_watch_directory(parent_dir.get(), driver_watcher_cb, deadline.get(), last);
      status != ZX_ERR_STOP) {
    fprintf(stderr, "error when waiting for '%s': %s\n", last, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

__EXPORT
zx_status_t wait_for_device_at(int dirfd, const char* path, zx_duration_t timeout) {
  if (!path || timeout == 0) {
    fprintf(stderr, "invalid args: path='%s', timeout=%" PRIu64 "\n", path, timeout);
    return ZX_ERR_INVALID_ARGS;
  }

  // Make a mutable copy
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  zx::time deadline = zx::deadline_after(zx::duration(timeout));
  return wait_for_device_impl(dirfd, tmp, deadline);
}

struct ramdisk_client {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(ramdisk_client);

  static zx_status_t Create(int dev_root_fd, std::string_view instance_name, zx::duration duration,
                            std::unique_ptr<ramdisk_client>* out) {
    fbl::String ramdisk_path = fbl::String::Concat({kRamctlPath, "/", instance_name});
    fbl::String block_path = fbl::String::Concat({ramdisk_path, "/", kBlockExtension});
    fbl::String path;
    fbl::unique_fd dirfd;
    if (dev_root_fd > -1) {
      dirfd.reset(dup(dev_root_fd));
      path = block_path;
    } else {
      dirfd.reset(open(kRamctlDevPath, O_RDONLY | O_DIRECTORY));
      path = fbl::String::Concat({kRamctlDevPath, "/", block_path});
    }
    if (!dirfd) {
      return ZX_ERR_BAD_STATE;
    }
    fdio_cpp::UnownedFdioCaller caller(dirfd);

    zx::result ramdisk_interface =
        component::ConnectAt<fuchsia_hardware_ramdisk::Ramdisk>(caller.directory(), ramdisk_path);
    if (ramdisk_interface.is_error()) {
      return ramdisk_interface.status_value();
    }

    // If binding to the block interface fails, ensure we still try to tear down the
    // ramdisk driver.
    auto cleanup = fit::defer([&ramdisk_interface]() {
      ramdisk_client::DestroyByHandle(std::move(ramdisk_interface.value()));
    });

    zx_status_t status = wait_for_device_at(dirfd.get(), block_path.c_str(), duration.get());
    if (status != ZX_OK) {
      return status;
    }

    zx::result block_interface =
        component::ConnectAt<fuchsia_hardware_block::Block>(caller.directory(), block_path);
    if (block_interface.is_error()) {
      return block_interface.status_value();
    }
    cleanup.cancel();
    *out = std::unique_ptr<ramdisk_client>(new ramdisk_client(
        std::move(dirfd), std::move(path), std::move(block_path),
        std::move(ramdisk_interface.value()), std::move(block_interface.value())));
    return ZX_OK;
  }

  zx_status_t Rebind() {
    const fidl::WireResult result = fidl::WireCall(block_interface_)->RebindDevice();
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    if (zx_status_t status = response.status; status != ZX_OK) {
      return status;
    }
    ramdisk_interface_.reset();

    // Ramdisk paths have the form: /dev/.../ramctl/ramdisk-xxx/block.
    // To rebind successfully, first, we rebind the "ramdisk-xxx" path,
    // and then we wait for "block" to rebind.

    // Wait for the "ramdisk-xxx" path to rebind.
    const char* sep = strrchr(relative_path_.c_str(), '/');
    char ramdisk_path[PATH_MAX];
    strlcpy(ramdisk_path, relative_path_.c_str(), sep - relative_path_.c_str() + 1);
    if (zx_status_t status =
            wait_for_device_impl(dev_root_fd_.get(), ramdisk_path, zx::deadline_after(zx::sec(3)));
        status != ZX_OK) {
      return status;
    }
    fdio_cpp::UnownedFdioCaller caller(dev_root_fd_);
    zx::result ramdisk_interface =
        component::ConnectAt<fuchsia_hardware_ramdisk::Ramdisk>(caller.directory(), ramdisk_path);
    if (ramdisk_interface.is_error()) {
      return ramdisk_interface.status_value();
    }
    ramdisk_interface_ = std::move(ramdisk_interface.value());

    // Wait for the "block" path to rebind.
    strlcpy(ramdisk_path, relative_path_.c_str(), sizeof(ramdisk_path));
    if (zx_status_t status =
            wait_for_device_impl(dev_root_fd_.get(), ramdisk_path, zx::deadline_after(zx::sec(3)));
        status != ZX_OK) {
      return status;
    }
    zx::result block_interface =
        component::ConnectAt<fuchsia_hardware_block::Block>(caller.directory(), relative_path_);
    if (block_interface.is_error()) {
      return block_interface.status_value();
    }
    block_interface_ = std::move(block_interface.value());
    return ZX_OK;
  }

  zx_status_t Destroy() {
    if (!ramdisk_interface_) {
      return ZX_ERR_BAD_STATE;
    }

    zx_status_t status = DestroyByHandle(std::move(ramdisk_interface_));
    if (status != ZX_OK) {
      return status;
    }
    block_interface_.reset();
    return ZX_OK;
  }

  fidl::UnownedClientEnd<fuchsia_device::Controller> controller_interface() const {
    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    return fidl::UnownedClientEnd<fuchsia_device::Controller>(ramdisk_interface().channel());
  }

  fidl::UnownedClientEnd<fuchsia_hardware_ramdisk::Ramdisk> ramdisk_interface() const {
    return ramdisk_interface_.borrow();
  }

  fidl::UnownedClientEnd<fuchsia_hardware_block::Block> block_interface() const {
    return block_interface_.borrow();
  }

  const fbl::String& path() const { return path_; }

  ~ramdisk_client() { Destroy(); }

 private:
  ramdisk_client(fbl::unique_fd dev_root_fd, fbl::String path, fbl::String relative_path,
                 fidl::ClientEnd<fuchsia_hardware_ramdisk::Ramdisk> ramdisk_interface,
                 fidl::ClientEnd<fuchsia_hardware_block::Block> block_interface)
      : dev_root_fd_(std::move(dev_root_fd)),
        path_(std::move(path)),
        relative_path_(std::move(relative_path)),
        ramdisk_interface_(std::move(ramdisk_interface)),
        block_interface_(std::move(block_interface)) {}

  static zx_status_t DestroyByHandle(fidl::ClientEnd<fuchsia_hardware_ramdisk::Ramdisk> ramdisk) {
    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    const fidl::WireResult result =
        fidl::WireCall(fidl::ClientEnd<fuchsia_device::Controller>(ramdisk.TakeChannel()))
            ->ScheduleUnbind();
    if (!result.ok()) {
      return result.status();
    }
    const fit::result response = result.value();
    if (response.is_error()) {
      return response.error_value();
    }
    return ZX_OK;
  }

  const fbl::unique_fd dev_root_fd_;
  // The fully qualified path.
  const fbl::String path_;
  // The path relative to dev_root_fd_.
  const fbl::String relative_path_;
  fidl::ClientEnd<fuchsia_hardware_ramdisk::Ramdisk> ramdisk_interface_;
  fidl::ClientEnd<fuchsia_hardware_block::Block> block_interface_;
};

// TODO(aarongreen): This is more generic than just fs-management, or even block devices.  Move this
// (and its tests) out of ramdisk and to somewhere else?
__EXPORT
zx_status_t wait_for_device(const char* path, zx_duration_t timeout) {
  return wait_for_device_at(/*dirfd=*/-1, path, timeout);
}

static zx::result<fidl::ClientEnd<fuchsia_hardware_ramdisk::RamdiskController>> open_ramctl(
    int dev_root_fd) {
  fbl::unique_fd dirfd;
  if (dev_root_fd > -1) {
    dirfd.reset(dup(dev_root_fd));
  } else {
    dirfd.reset(open(kRamctlDevPath, O_RDONLY | O_DIRECTORY));
  }
  if (!dirfd) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  fdio_cpp::FdioCaller caller(std::move(dirfd));
  return component::ConnectAt<fuchsia_hardware_ramdisk::RamdiskController>(caller.directory(),
                                                                           kRamctlPath);
}

static zx_status_t ramdisk_create_with_guid_internal(
    int dev_root_fd, uint64_t blk_size, uint64_t blk_count,
    fidl::ObjectView<fuchsia_hardware_ramdisk::wire::Guid> type_guid, ramdisk_client** out) {
  zx::result ramctl = open_ramctl(dev_root_fd);
  if (ramctl.is_error()) {
    return ramctl.status_value();
  }

  const fidl::WireResult result =
      fidl::WireCall(ramctl.value())->Create(blk_size, blk_count, type_guid);
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.s; status != ZX_OK) {
    return status;
  }

  std::unique_ptr<ramdisk_client> client;
  if (zx_status_t status =
          ramdisk_client::Create(dev_root_fd, response.name.get(), zx::sec(3), &client);
      status != ZX_OK) {
    return status;
  }
  *out = client.release();
  return ZX_OK;
}

__EXPORT
zx_status_t ramdisk_create_at(int dev_root_fd, uint64_t blk_size, uint64_t blk_count,
                              ramdisk_client** out) {
  return ramdisk_create_with_guid_internal(dev_root_fd, blk_size, blk_count, nullptr, out);
}

__EXPORT
zx_status_t ramdisk_create(uint64_t blk_size, uint64_t blk_count, ramdisk_client** out) {
  return ramdisk_create_at(/*dev_root_fd=*/-1, blk_size, blk_count, out);
}

__EXPORT
zx_status_t ramdisk_create_with_guid(uint64_t blk_size, uint64_t blk_count,
                                     const uint8_t* type_guid, size_t guid_len,
                                     ramdisk_client** out) {
  return ramdisk_create_at_with_guid(/*dev_root_fd=*/-1, blk_size, blk_count, type_guid, guid_len,
                                     out);
}

__EXPORT
zx_status_t ramdisk_create_at_with_guid(int dev_root_fd, uint64_t blk_size, uint64_t blk_count,
                                        const uint8_t* type_guid, size_t guid_len,
                                        ramdisk_client** out) {
  if (type_guid != nullptr && guid_len < ZBI_PARTITION_GUID_LEN) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ramdisk_create_with_guid_internal(
      dev_root_fd, blk_size, blk_count,
      fidl::ObjectView<fuchsia_hardware_ramdisk::wire::Guid>::FromExternal(
          reinterpret_cast<fuchsia_hardware_ramdisk::wire::Guid*>(const_cast<uint8_t*>(type_guid))),
      out);
}

__EXPORT
zx_status_t ramdisk_create_from_vmo(zx_handle_t raw_vmo, ramdisk_client** out) {
  return ramdisk_create_at_from_vmo(/*dev_root_fd=*/-1, raw_vmo, out);
}

__EXPORT
zx_status_t ramdisk_create_from_vmo_with_params(zx_handle_t raw_vmo, uint64_t block_size,
                                                const uint8_t* type_guid, size_t guid_len,
                                                ramdisk_client** out) {
  return ramdisk_create_at_from_vmo_with_params(/*dev_root_fd=*/-1, raw_vmo, block_size, type_guid,
                                                guid_len, out);
}

__EXPORT
zx_status_t ramdisk_create_at_from_vmo(int dev_root_fd, zx_handle_t vmo, ramdisk_client** out) {
  return ramdisk_create_at_from_vmo_with_params(dev_root_fd, vmo, /*block_size*/ 0,
                                                /*type_guid*/ nullptr, /*guid_len*/ 0, out);
}

__EXPORT
zx_status_t ramdisk_create_at_from_vmo_with_params(int dev_root_fd, zx_handle_t raw_vmo,
                                                   uint64_t block_size, const uint8_t* type_guid,
                                                   size_t guid_len, ramdisk_client** out) {
  if (type_guid != nullptr && guid_len < ZBI_PARTITION_GUID_LEN) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx::vmo vmo(raw_vmo);
  zx::result ramctl = open_ramctl(dev_root_fd);
  if (ramctl.is_error()) {
    return ramctl.status_value();
  }

  const fidl::WireResult result =
      fidl::WireCall(ramctl.value())
          ->CreateFromVmoWithParams(
              std::move(vmo), block_size,
              fidl::ObjectView<fuchsia_hardware_ramdisk::wire::Guid>::FromExternal(
                  reinterpret_cast<fuchsia_hardware_ramdisk::wire::Guid*>(
                      const_cast<uint8_t*>(type_guid))));
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.s; status != ZX_OK) {
    return status;
  }

  std::unique_ptr<ramdisk_client> client;
  if (zx_status_t status =
          ramdisk_client::Create(dev_root_fd, response.name.get(), zx::sec(3), &client);
      status != ZX_OK) {
    return status;
  }
  *out = client.release();
  return ZX_OK;
}

__EXPORT
zx_handle_t ramdisk_get_block_interface(const ramdisk_client_t* client) {
  return client->block_interface().channel()->get();
}

__EXPORT
const char* ramdisk_get_path(const ramdisk_client_t* client) { return client->path().c_str(); }

__EXPORT
zx_status_t ramdisk_sleep_after(const ramdisk_client* client, uint64_t block_count) {
  const fidl::WireResult result =
      fidl::WireCall(client->ramdisk_interface())->SleepAfter(block_count);
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.s;
}

__EXPORT
zx_status_t ramdisk_wake(const ramdisk_client* client) {
  const fidl::WireResult result = fidl::WireCall(client->ramdisk_interface())->Wake();
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.s;
}

__EXPORT
zx_status_t ramdisk_grow(const ramdisk_client* client, uint64_t required_size) {
  const fidl::WireResult result = fidl::WireCall(client->ramdisk_interface())->Grow(required_size);
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.s;
}

__EXPORT
zx_status_t ramdisk_set_flags(const ramdisk_client* client, uint32_t flags) {
  const fidl::WireResult result = fidl::WireCall(client->ramdisk_interface())->SetFlags(flags);
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.s;
}

__EXPORT
zx_status_t ramdisk_get_block_counts(const ramdisk_client* client,
                                     ramdisk_block_write_counts_t* out_counts) {
  static_assert(sizeof(ramdisk_block_write_counts_t) ==
                    sizeof(fuchsia_hardware_ramdisk::wire::BlockWriteCounts),
                "Cannot convert between C library / FIDL block counts");

  const fidl::WireResult result = fidl::WireCall(client->ramdisk_interface())->GetBlockCounts();
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.s; status != ZX_OK) {
    return status;
  }
  memcpy(out_counts, response.counts.get(), sizeof(ramdisk_block_write_counts_t));
  return ZX_OK;
}

__EXPORT
zx_status_t ramdisk_rebind(ramdisk_client_t* client) { return client->Rebind(); }

__EXPORT
zx_status_t ramdisk_destroy(ramdisk_client* client) {
  zx_status_t status = client->Destroy();
  delete client;
  return status;
}
