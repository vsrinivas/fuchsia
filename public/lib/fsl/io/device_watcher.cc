// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/io/device_watcher.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>

#include <lib/async/default.h>
#include <lib/fdio/io.h>
#include <zircon/device/vfs.h>

#include "lib/fxl/logging.h"

namespace fsl {

DeviceWatcher::DeviceWatcher(fxl::UniqueFD dir_fd, zx::channel dir_watch,
                             Callback callback)
    : dir_fd_(std::move(dir_fd)),
      dir_watch_(std::move(dir_watch)),
      callback_(std::move(callback)),
      wait_(this, dir_watch_.get(),
            ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED),
      weak_ptr_factory_(this) {
  auto status = wait_.Begin(async_get_default_dispatcher());
  FXL_DCHECK(status == ZX_OK);
}

std::unique_ptr<DeviceWatcher> DeviceWatcher::Create(std::string directory_path,
                                                     Callback callback) {
  // Open the directory.
  int open_result = open(directory_path.c_str(), O_DIRECTORY | O_RDONLY);
  if (open_result < 0) {
    FXL_LOG(ERROR) << "Failed to open " << directory_path
                   << ", errno=" << errno;
    return nullptr;
  }
  fxl::UniqueFD dir_fd(open_result);  // take ownership of fd here

  // Create the directory watch channel.
  vfs_watch_dir_t wd;
  wd.mask = VFS_WATCH_MASK_ADDED | VFS_WATCH_MASK_EXISTING;
  wd.options = 0;
  zx_handle_t dir_watch_handle;
  if (zx_channel_create(0, &wd.channel, &dir_watch_handle) < 0) {
    return nullptr;
  }
  ssize_t ioctl_result = ioctl_vfs_watch_dir(dir_fd.get(), &wd);
  if (ioctl_result < 0) {
    zx_handle_close(wd.channel);
    zx_handle_close(dir_watch_handle);
    FXL_LOG(ERROR) << "Failed to create device watcher for " << directory_path
                   << ", result=" << ioctl_result;
    return nullptr;
  }
  zx::channel dir_watch(dir_watch_handle);  // take ownership of handle here

  return std::unique_ptr<DeviceWatcher>(new DeviceWatcher(
      std::move(dir_fd), std::move(dir_watch), std::move(callback)));
}

void DeviceWatcher::Handler(async_dispatcher_t* dispatcher,
                            async::WaitBase* wait, zx_status_t status,
                            const zx_packet_signal* signal) {
  if (status != ZX_OK)
    return;

  if (signal->observed & ZX_CHANNEL_READABLE) {
    uint32_t size;
    uint8_t buf[VFS_WATCH_MSG_MAX];
    zx_status_t status =
        dir_watch_.read(0, buf, sizeof(buf), &size, nullptr, 0, nullptr);
    FXL_CHECK(status == ZX_OK) << "Failed to read from directory watch channel";

    auto weak = weak_ptr_factory_.GetWeakPtr();
    uint8_t* msg = buf;
    while (size >= 2) {
      unsigned event = *msg++;
      unsigned namelen = *msg++;
      if (size < (namelen + 2u)) {
        break;
      }
      if ((event == VFS_WATCH_EVT_ADDED) || (event == VFS_WATCH_EVT_EXISTING)) {
        callback_(dir_fd_.get(),
                  std::string(reinterpret_cast<char*>(msg), namelen));
        // Note: Callback may have destroyed the DeviceWatcher before returning.
        if (!weak) {
          return;
        }
      }
      msg += namelen;
      size -= namelen;
    }
    wait->Begin(dispatcher);  // ignore errors
    return;
  }

  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    // TODO(jeffbrown): Should we tell someone about this?
    dir_watch_.reset();
    return;
  }

  FXL_CHECK(false);
}

}  // namespace fsl
