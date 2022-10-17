// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/watcher.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/constants.h"
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
  std::pair<const char*, AddDeviceCallback> types[] = {
      {kBlockDeviceClassPrefix.data(), AddDeviceImpl<BlockDevice>},
      {kNandDeviceClassPrefix.data(), AddDeviceImpl<NandDevice>},
  };
  std::vector<Watcher> ret;
  for (auto& [path, callback] : types) {
    fbl::unique_fd dirfd(open(path, O_DIRECTORY | O_RDONLY));
    if (!dirfd) {
      FX_LOGS(ERROR) << "failed to open " << path << ": " << strerror(errno);
      continue;
    }

    fdio_cpp::FdioCaller caller(std::move(dirfd));

    ret.emplace_back(Watcher(path, std::move(caller), std::move(callback)));
  }

  return ret;
}

zx_status_t Watcher::ReinitWatcher() {
  watcher_.reset();
  zx::result server_end = fidl::CreateEndpoints<fio::DirectoryWatcher>(&watcher_);
  if (server_end.is_error()) {
    FX_PLOGS(ERROR, server_end.status_value()) << "failed to create watcher channel";
    return server_end.status_value();
  }

  auto mask = fio::wire::WatchMask::kMask;
  if (ignore_existing_) {
    mask &= ~fio::wire::WatchMask::kExisting;
  }
  auto result = fidl::WireCall(caller_.borrow_as<fio::Directory>())
                    ->Watch(mask, 0, std::move(server_end.value()));
  if (!result.ok()) {
    FX_LOGS(ERROR) << "failed to send watch: " << result.error();
    return result.status();
  }
  if (result.value().s != ZX_OK) {
    FX_LOGS(ERROR) << "watch failed: " << zx_status_get_string(result.value().s);
    return result.value().s;
  }

  return ZX_OK;
}

void Watcher::ProcessWatchMessages(cpp20::span<uint8_t> buf, WatcherCallback callback) {
  uint8_t* iter = buf.begin();
  while (iter + 2 <= buf.end()) {
    fio::wire::WatchEvent event = static_cast<fio::wire::WatchEvent>(*iter++);
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
    if (event == fio::wire::WatchEvent::kIdle) {
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
