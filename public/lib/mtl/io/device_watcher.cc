// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/io/device_watcher.h"

#include <fcntl.h>
#include <dirent.h>
#include <magenta/device/vfs.h>
#include <mxio/io.h>
#include <sys/types.h>

#include "lib/fxl/logging.h"

namespace mtl {

DeviceWatcher::DeviceWatcher(fxl::UniqueFD dir_fd,
                             mx::channel dir_watch,
                             Callback callback)
    : dir_fd_(std::move(dir_fd)),
      dir_watch_(std::move(dir_watch)),
      callback_(std::move(callback)),
      weak_ptr_factory_(this) {
  MessageLoop* message_loop = MessageLoop::GetCurrent();
  FXL_DCHECK(message_loop);

  handler_key_ = message_loop->AddHandler(
      this, dir_watch_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
}

DeviceWatcher::~DeviceWatcher() {
  MessageLoop::GetCurrent()->RemoveHandler(handler_key_);
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
  mx_handle_t dir_watch_handle;
  if (mx_channel_create(0, &wd.channel, &dir_watch_handle) < 0) {
    return nullptr;
  }
  ssize_t ioctl_result = ioctl_vfs_watch_dir(dir_fd.get(), &wd);
  if (ioctl_result < 0) {
    mx_handle_close(wd.channel);
    mx_handle_close(dir_watch_handle);
    FXL_LOG(ERROR) << "Failed to create device watcher for " << directory_path
                   << ", result=" << ioctl_result;
    return nullptr;
  }
  mx::channel dir_watch(dir_watch_handle);  // take ownership of handle here

  return std::unique_ptr<DeviceWatcher>(new DeviceWatcher(
      std::move(dir_fd), std::move(dir_watch), std::move(callback)));
}

void DeviceWatcher::OnHandleReady(mx_handle_t handle,
                                  mx_signals_t pending,
                                  uint64_t count) {
  if (pending & MX_CHANNEL_READABLE) {
    uint32_t size;
    uint8_t buf[VFS_WATCH_MSG_MAX];
    mx_status_t status =
        dir_watch_.read(0, buf, sizeof(buf), &size, nullptr, 0, nullptr);
    FXL_CHECK(status == MX_OK)
        << "Failed to read from directory watch channel";

    auto weak = weak_ptr_factory_.GetWeakPtr();
    uint8_t* msg = buf;
    while (size >= 2) {
        unsigned event = *msg++;
        unsigned namelen = *msg++;
        if (size < (namelen + 2u)) {
            break;
        }
        if ((event == VFS_WATCH_EVT_ADDED) || (event == VFS_WATCH_EVT_EXISTING)) {
            callback_(dir_fd_.get(), std::string(reinterpret_cast<char*>(msg), namelen));
            // Note: Callback may have destroyed the DeviceWatcher before returning.
            if (!weak) {
                break;
            }
        }
        msg += namelen;
        size -= namelen;
    }
    return;
  }

  if (pending & MX_CHANNEL_PEER_CLOSED) {
    // TODO(jeffbrown): Should we tell someone about this?
    dir_watch_.reset();
    return;
  }

  FXL_CHECK(false);
}

}  // namespace mtl
