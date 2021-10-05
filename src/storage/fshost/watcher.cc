// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/watcher.h"

#include <lib/syslog/cpp/macros.h>

#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/nand-device.h"

namespace fshost {
namespace {
namespace fio = fuchsia_io;

template <typename T>
zx_status_t AddDeviceImpl(BlockDeviceManager& manager, FilesystemMounter* mounter,
                          fbl::unique_fd fd) {
  T device(mounter, std::move(fd), manager.config());
  return manager.AddDevice(device);
}

}  // namespace

std::vector<Watcher> Watcher::CreateWatchers() {
  std::vector<Watcher> ret;
  for (size_t i = 0; i < kWatcherTypeMax; i++) {
    fbl::unique_fd dirfd(open(kWatcherPaths[i], O_DIRECTORY | O_RDONLY));
    if (!dirfd) {
      FX_LOGS(ERROR) << "failed to open " << kWatcherPaths[i] << ": " << strerror(errno);
      continue;
    }

    fdio_cpp::FdioCaller caller(std::move(dirfd));

    AddDeviceCallback callback;
    switch (WatcherType(i)) {
      case WatcherType::kWatcherTypeBlock:
        callback = AddDeviceImpl<BlockDevice>;
        break;
      case WatcherType::kWatcherTypeNand:
        callback = AddDeviceImpl<NandDevice>;
        break;
      default:
        ZX_ASSERT_MSG(false, "Invalid watcher type %zu", i);
    }

    ret.emplace_back(Watcher(WatcherType(i), std::move(caller), std::move(callback)));
  }

  return ret;
}

zx_status_t Watcher::ReinitWatcher() {
  watcher_.reset();
  zx::channel watcher, server;
  zx_status_t status = zx::channel::create(0, &watcher, &server);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create watcher channel: " << zx_status_get_string(status);
    return status;
  }

  auto mask = fio::wire::kWatchMaskAll;
  if (ignore_existing_) {
    mask &= ~fio::wire::kWatchMaskExisting;
  }
  auto result =
      fidl::WireCall<fio::Directory>(caller_.channel())->Watch(mask, 0, std::move(server));
  if (!result.ok()) {
    FX_LOGS(ERROR) << "failed to send watch: " << result.error();
    return result.status();
  }
  if (result->s != ZX_OK) {
    FX_LOGS(ERROR) << "watch failed: " << zx_status_get_string(result->s);
    return result->s;
  }

  watcher_ = std::move(watcher);
  return ZX_OK;
}

void Watcher::ProcessWatchMessages(cpp20::span<uint8_t> buf, WatcherCallback callback) {
  uint8_t* iter = buf.begin();
  while (iter + 2 <= buf.end()) {
    uint8_t event = *iter++;
    uint8_t name_len = *iter++;

    if (iter + name_len > buf.end()) {
      break;
    }

    // Save the first byte of the next message,
    // and null-terminate the name.
    uint8_t tmp = iter[name_len];
    iter[name_len] = 0;

    if (callback(*this, caller_.fd().get(), event, reinterpret_cast<const char*>(iter))) {
      // We received a WATCH_EVENT_IDLE, and the watcher is paused.
      // Bail out early.
      ignore_existing_ = true;
      return;
    }
    if (event == fio::wire::kWatchEventIdle) {
      // WATCH_EVENT_IDLE - but the watcher is not paused. Finish processing this set of messages,
      // but set ignore_existing_ to indicate that we should start checking for the pause signal.
      ignore_existing_ = true;
      break;
    }

    iter[name_len] = tmp;

    iter += name_len;
  }
}

zx_status_t Watcher::AddDevice(BlockDeviceManager& manager, FilesystemMounter* mounter,
                               fbl::unique_fd fd) {
  return add_device_(manager, mounter, std::move(fd));
}

}  // namespace fshost
