// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/watcher.h"

#include <string.h>
#include <sys/stat.h>

#include <memory>
#include <string_view>
#include <utility>

#include <fbl/alloc_checker.h>

#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fio = fuchsia_io;

namespace fs {

WatcherContainer::WatcherContainer() = default;
WatcherContainer::~WatcherContainer() = default;

WatcherContainer::VnodeWatcher::VnodeWatcher(
    fidl::ServerEnd<fuchsia_io::DirectoryWatcher> server_end, fuchsia_io::wire::WatchMask mask)
    : server_end(std::move(server_end)),
      mask(mask & ~(fio::wire::WatchMask::kExisting | fio::wire::WatchMask::kIdle)) {}

WatcherContainer::VnodeWatcher::~VnodeWatcher() = default;

// Transmission buffer for sending directory watcher notifications to clients. Allows enqueueing
// multiple messages in a buffer before sending an IPC message to a client.
class WatchBuffer {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(WatchBuffer);
  WatchBuffer() = default;

  zx_status_t AddMsg(const fidl::ServerEnd<fuchsia_io::DirectoryWatcher>& server_end,
                     fio::wire::WatchEvent event, std::string_view name) {
    size_t slen = name.length();
    size_t mlen = sizeof(vfs_watch_msg_t) + slen;
    if (mlen + watch_buf_size_ > sizeof(watch_buf_)) {
      // This message won't fit in the watch_buf; transmit first.
      zx_status_t status = Send(server_end);
      if (status != ZX_OK) {
        return status;
      }
    }
    vfs_watch_msg_t& vmsg = *reinterpret_cast<vfs_watch_msg_t*>(&watch_buf_[watch_buf_size_]);
    vmsg.event = static_cast<uint8_t>(event);
    vmsg.len = static_cast<uint8_t>(slen);
    memcpy(vmsg.name, name.data(), slen);
    watch_buf_size_ += mlen;
    return ZX_OK;
  }
  zx_status_t Send(const fidl::ServerEnd<fuchsia_io::DirectoryWatcher>& server_end) {
    if (watch_buf_size_ > 0) {
      // Only write if we have something to write
      zx_status_t status = server_end.channel().write(
          0, watch_buf_, static_cast<uint32_t>(watch_buf_size_), nullptr, 0);
      watch_buf_size_ = 0;
      if (status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }

 private:
  size_t watch_buf_size_ = 0;
  char watch_buf_[fio::wire::kMaxBuf]{};
};

zx_status_t WatcherContainer::WatchDir(Vfs* vfs, Vnode* vn, fio::wire::WatchMask mask,
                                       uint32_t options,
                                       fidl::ServerEnd<fuchsia_io::DirectoryWatcher> server_end) {
  if (!mask) {
    // No events to watch
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher(std::move(server_end), mask));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (mask & fio::wire::WatchMask::kExisting) {
    VdirCookie dircookie;
    memset(&dircookie, 0, sizeof(dircookie));
    char readdir_buf[FDIO_CHUNK_SIZE];
    WatchBuffer wb;
    {
      // Send "fio::wire::WatchEvent::kExisting" for all entries in readdir.
      while (true) {
        size_t actual;
        zx_status_t status =
            vfs->Readdir(vn, &dircookie, readdir_buf, sizeof(readdir_buf), &actual);
        if (status != ZX_OK || actual == 0) {
          break;
        }
        char* ptr = readdir_buf;
        while (actual >= sizeof(vdirent_t)) {
          auto dirent = reinterpret_cast<vdirent_t*>(ptr);
          if (dirent->name[0]) {
            wb.AddMsg(watcher->server_end, fio::wire::WatchEvent::kExisting,
                      std::string_view(dirent->name, dirent->size));
          }
          size_t entry_len = dirent->size + sizeof(vdirent_t);
          ZX_ASSERT(entry_len <= actual);  // Prevent underflow
          actual -= entry_len;
          ptr += entry_len;
        }
      }
    }

    // Send fio::wire::WatchEvent::kIdle to signify that readdir has completed.
    if (mask & fio::wire::WatchMask::kIdle) {
      wb.AddMsg(watcher->server_end, fio::wire::WatchEvent::kIdle, "");
    }

    wb.Send(watcher->server_end);
  }

  std::lock_guard lock(lock_);
  watch_list_.push_back(std::move(watcher));
  return ZX_OK;
}

void WatcherContainer::Notify(std::string_view name, fio::wire::WatchEvent event) {
  if (name.length() > fio::wire::kMaxFilename) {
    return;
  }

  std::lock_guard lock(lock_);

  if (watch_list_.is_empty()) {
    return;
  }

  uint8_t msg[sizeof(vfs_watch_msg_t) + fio::wire::kMaxFilename];
  size_t msg_length = sizeof(vfs_watch_msg_t) + name.length();
  vfs_watch_msg_t* vmsg = reinterpret_cast<vfs_watch_msg_t*>(msg);
  vmsg->event = static_cast<uint8_t>(event);
  vmsg->len = static_cast<uint8_t>(name.length());
  memcpy(vmsg->name, name.data(), name.length());

  for (auto it = watch_list_.begin(); it != watch_list_.end();) {
    if (!(it->mask & static_cast<fio::wire::WatchMask>(1 << static_cast<uint8_t>(event)))) {
      ++it;
      continue;
    }

    zx_status_t status =
        it->server_end.channel().write(0, msg, static_cast<uint32_t>(msg_length), nullptr, 0);
    if (status < 0) {
      // Lazily remove watchers when their handles cannot accept incoming watch messages.
      auto to_remove = it;
      ++it;
      watch_list_.erase(to_remove);
    } else {
      ++it;
    }
  }
}

}  // namespace fs
