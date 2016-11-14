// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/io/device_watcher.h"

#include <fcntl.h>
#include <dirent.h>
#include <magenta/device/device.h>
#include <mxio/io.h>
#include <sys/types.h>

#include "lib/ftl/logging.h"

namespace mtl {

DeviceWatcher::DeviceWatcher(ftl::UniqueFD dir_fd,
                             mx::channel dir_watch,
                             Callback callback)
    : dir_fd_(std::move(dir_fd)),
      dir_watch_(std::move(dir_watch)),
      callback_(std::move(callback)),
      weak_ptr_factory_(this) {
  MessageLoop* message_loop = MessageLoop::GetCurrent();
  FTL_DCHECK(message_loop);

  handler_key_ = message_loop->AddHandler(
      this, dir_watch_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);

  message_loop->task_runner()->PostTask(
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak)
          ListDevices(weak, weak->dir_fd_.get());
      });
}

DeviceWatcher::~DeviceWatcher() {
  MessageLoop::GetCurrent()->RemoveHandler(handler_key_);
}

std::unique_ptr<DeviceWatcher> DeviceWatcher::Create(std::string directory_path,
                                                     Callback callback) {
  // Open the directory.
  int open_result = open(directory_path.c_str(), O_DIRECTORY | O_RDONLY);
  if (open_result < 0) {
    FTL_LOG(ERROR) << "Failed to open " << directory_path
                   << ", errno=" << errno;
    return nullptr;
  }
  ftl::UniqueFD dir_fd(open_result);  // take ownership of fd here

  // Create the directory watch channel.
  mx_handle_t dir_watch_handle;
  ssize_t ioctl_result =
      ioctl_device_watch_dir(dir_fd.get(), &dir_watch_handle);
  if (ioctl_result < 0) {
    FTL_LOG(ERROR) << "Failed to create device watcher for " << directory_path
                   << ", result=" << ioctl_result;
    return nullptr;
  }
  mx::channel dir_watch(dir_watch_handle);  // take ownership of handle here

  return std::unique_ptr<DeviceWatcher>(new DeviceWatcher(
      std::move(dir_fd), std::move(dir_watch), std::move(callback)));
}

void DeviceWatcher::ListDevices(ftl::WeakPtr<DeviceWatcher> weak, int dir_fd) {
  FTL_DCHECK(weak);
  DIR* dir = fdopendir(dup(dir_fd));
  if (!dir)
    return;

  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') {
      if (entry->d_name[1] == 0)
        continue;
      if ((entry->d_name[1] == '.') && (entry->d_name[2] == 0))
        continue;
    }

    // Note: Callback may destroy DeviceWatcher before returning.
    weak->callback_(dir_fd, entry->d_name);
    if (!weak)
      break;
  }

  closedir(dir);
}

void DeviceWatcher::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (pending & MX_SIGNAL_READABLE) {
    uint32_t size;
    char name[MXIO_MAX_FILENAME + 1];
    mx_status_t status =
        dir_watch_.read(0, name, MXIO_MAX_FILENAME, &size, nullptr, 0, nullptr);
    FTL_CHECK(status == NO_ERROR)
        << "Failed to read from directory watch channel";

    // Note: Callback may destroy DeviceWatcher before returning.
    callback_(dir_fd_.get(), std::string(name, size));
    return;
  }

  if (pending & MX_SIGNAL_PEER_CLOSED) {
    // TODO(jeffbrown): Should we tell someone about this?
    dir_watch_.reset();
    return;
  }

  FTL_CHECK(false);
}

}  // namespace mtl
