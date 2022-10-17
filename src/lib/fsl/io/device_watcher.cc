// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fsl/io/device_watcher.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/types.h>

#include <utility>

#include <fbl/unique_fd.h>

namespace fio = fuchsia_io;

namespace fsl {

DeviceWatcher::DeviceWatcher(async_dispatcher_t* dispatcher, fbl::unique_fd dir_fd,
                             fidl::ClientEnd<fuchsia_io::DirectoryWatcher> dir_watcher,
                             ExistsCallback exists_callback, IdleCallback idle_callback)
    : dir_fd_(std::move(dir_fd)),
      dir_watcher_(std::move(dir_watcher)),
      exists_callback_(std::move(exists_callback)),
      idle_callback_(std::move(idle_callback)),
      wait_(this, dir_watcher_.channel().get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED),
      weak_ptr_factory_(this) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  auto status = wait_.Begin(dispatcher);
  FX_DCHECK(status == ZX_OK);
}

std::unique_ptr<DeviceWatcher> DeviceWatcher::Create(const std::string& directory_path,
                                                     ExistsCallback exists_callback,
                                                     async_dispatcher_t* dispatcher) {
  return CreateWithIdleCallback(
      directory_path, std::move(exists_callback), [] {}, dispatcher);
}

std::unique_ptr<DeviceWatcher> DeviceWatcher::CreateWithIdleCallback(
    const std::string& directory_path, ExistsCallback exists_callback, IdleCallback idle_callback,
    async_dispatcher_t* dispatcher) {
  // Open the directory.
  int open_result = open(directory_path.c_str(), O_DIRECTORY | O_RDONLY);
  if (open_result < 0) {
    FX_LOGS(ERROR) << "Failed to open " << directory_path << ", errno=" << errno;
    return nullptr;
  }
  return CreateWithIdleCallback(fbl::unique_fd(open_result), std::move(exists_callback),
                                std::move(idle_callback), dispatcher);
}

std::unique_ptr<DeviceWatcher> DeviceWatcher::CreateWithIdleCallback(
    fbl::unique_fd dir_fd, ExistsCallback exists_callback, IdleCallback idle_callback,
    async_dispatcher_t* dispatcher) {
  zx::result endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  if (endpoints.is_error()) {
    return nullptr;
  }
  fdio_cpp::FdioCaller caller{std::move(dir_fd)};
  fio::wire::WatchMask mask =
      fio::wire::WatchMask::kAdded | fio::wire::WatchMask::kExisting | fio::wire::WatchMask::kIdle;
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fio::Directory>(caller.borrow_channel()))
                    ->Watch(mask, 0, std::move(endpoints->server));
  if (result.status() != ZX_OK || result.value().s != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create device watcher: outer status=" << result.status()
                   << " status=" << result.value().s;
    return nullptr;
  }

  // This weird handshake is necessary because fbl::unique_fd != fbl::unique_fd.
  fbl::unique_fd dir_fd_alt(caller.release().release());

  return std::unique_ptr<DeviceWatcher>(
      new DeviceWatcher(dispatcher, std::move(dir_fd_alt), std::move(endpoints->client),
                        std::move(exists_callback), std::move(idle_callback)));
}

void DeviceWatcher::Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                            zx_status_t status, const zx_packet_signal* signal) {
  if (status != ZX_OK)
    return;

  if (signal->observed & ZX_CHANNEL_READABLE) {
    uint32_t size;
    uint8_t buf[fio::wire::kMaxBuf];
    zx_status_t status =
        dir_watcher_.channel().read(0, buf, nullptr, sizeof(buf), 0, &size, nullptr);
    FX_CHECK(status == ZX_OK) << "Failed to read from directory watch channel";

    auto weak = weak_ptr_factory_.GetWeakPtr();
    uint8_t* msg = buf;
    while (size >= 2) {
      fio::wire::WatchEvent event = static_cast<fio::wire::WatchEvent>(*msg++);
      unsigned namelen = *msg++;
      if (size < (namelen + 2u)) {
        break;
      }
      if ((event == fio::wire::WatchEvent::kAdded) || (event == fio::wire::WatchEvent::kExisting)) {
        std::string filename(reinterpret_cast<char*>(msg), namelen);
        // "." is not a device, so ignore it.
        if (filename != ".") {
          exists_callback_(dir_fd_.get(), filename);
        }
      } else if (event == fio::wire::WatchEvent::kIdle) {
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
    dir_watcher_.reset();
    return;
  }

  FX_CHECK(false);
}

}  // namespace fsl
