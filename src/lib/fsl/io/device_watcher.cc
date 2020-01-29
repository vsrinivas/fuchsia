// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fsl/io/device_watcher.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/io.h>
#include <lib/fdio/cpp/caller.h>
#include <sys/types.h>
#include <zircon/device/vfs.h>

#include <utility>

#include <fbl/unique_fd.h>

#include "src/lib/fxl/logging.h"

namespace fsl {

DeviceWatcher::DeviceWatcher(fbl::unique_fd dir_fd, zx::channel dir_watch,
                             ExistsCallback exists_callback, IdleCallback idle_callback)
    : dir_fd_(std::move(dir_fd)),
      dir_watch_(std::move(dir_watch)),
      exists_callback_(std::move(exists_callback)),
      idle_callback_(std::move(idle_callback)),
      wait_(this, dir_watch_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED),
      weak_ptr_factory_(this) {
  auto status = wait_.Begin(async_get_default_dispatcher());
  FXL_DCHECK(status == ZX_OK);
}

std::unique_ptr<DeviceWatcher> DeviceWatcher::Create(const std::string& directory_path,
                                                     ExistsCallback exists_callback) {
  return CreateWithIdleCallback(directory_path, std::move(exists_callback), [] {});
}

std::unique_ptr<DeviceWatcher> DeviceWatcher::CreateWithIdleCallback(
    const std::string& directory_path, ExistsCallback exists_callback, IdleCallback idle_callback) {
  // Open the directory.
  int open_result = open(directory_path.c_str(), O_DIRECTORY | O_RDONLY);
  if (open_result < 0) {
    FXL_LOG(ERROR) << "Failed to open " << directory_path << ", errno=" << errno;
    return nullptr;
  }
  fbl::unique_fd dir_fd(open_result);
  zx::channel client, server;
  if (zx::channel::create(0, &client, &server) != ZX_OK) {
    return nullptr;
  }
  fdio_cpp::FdioCaller caller{std::move(dir_fd)};
  uint32_t mask =
      fuchsia_io_WATCH_MASK_ADDED | fuchsia_io_WATCH_MASK_EXISTING | fuchsia_io_WATCH_MASK_IDLE;
  zx_status_t status;
  zx_status_t io_status =
      fuchsia_io_DirectoryWatch(caller.borrow_channel(), mask, 0, server.release(), &status);
  if (io_status != ZX_OK || status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create device watcher for " << directory_path
                   << ", status=" << status;
    return nullptr;
  }

  // This weird handshake is necessary because fbl::unique_fd != fbl::unique_fd.
  fbl::unique_fd dir_fd_alt(caller.release().release());

  return std::unique_ptr<DeviceWatcher>(new DeviceWatcher(std::move(dir_fd_alt), std::move(client),
                                                          std::move(exists_callback),
                                                          std::move(idle_callback)));
}

void DeviceWatcher::Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                            zx_status_t status, const zx_packet_signal* signal) {
  if (status != ZX_OK)
    return;

  if (signal->observed & ZX_CHANNEL_READABLE) {
    uint32_t size;
    uint8_t buf[fuchsia_io_MAX_BUF];
    zx_status_t status = dir_watch_.read(0, buf, nullptr, sizeof(buf), 0, &size, nullptr);
    FXL_CHECK(status == ZX_OK) << "Failed to read from directory watch channel";

    auto weak = weak_ptr_factory_.GetWeakPtr();
    uint8_t* msg = buf;
    while (size >= 2) {
      unsigned event = *msg++;
      unsigned namelen = *msg++;
      if (size < (namelen + 2u)) {
        break;
      }
      if ((event == fuchsia_io_WATCH_EVENT_ADDED) || (event == fuchsia_io_WATCH_EVENT_EXISTING)) {
        exists_callback_(dir_fd_.get(), std::string(reinterpret_cast<char*>(msg), namelen));
      } else if (event == fuchsia_io_WATCH_EVENT_IDLE) {
        idle_callback_();
        // Only call the idle callback once.  In case there is some captured
        // context, remove the function, or rather set it to an empty function,
        // in case we try to call it again.
        idle_callback_ = [] {};
      }
      // Note: Callback may have destroyed the DeviceWatcher before returning.
      if (!weak) {
        return;
      }
      msg += namelen;
      size -= namelen + 2;
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
