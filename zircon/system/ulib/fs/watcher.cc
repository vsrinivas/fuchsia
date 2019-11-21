// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/watcher.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>

#ifdef __Fuchsia__
#include <fuchsia/io/llcpp/fidl.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fbl/auto_lock.h>
#endif

#include <lib/zx/channel.h>

#include <utility>

#include <fbl/alloc_checker.h>
#include <fs/vfs.h>
#include <fs/vnode.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

WatcherContainer::WatcherContainer() = default;
WatcherContainer::~WatcherContainer() = default;

WatcherContainer::VnodeWatcher::VnodeWatcher(zx::channel h, uint32_t mask)
    : h(std::move(h)),
      mask(mask & ~(fio::WATCH_MASK_EXISTING | fio::WATCH_MASK_IDLE)) {}

WatcherContainer::VnodeWatcher::~VnodeWatcher() {}

// Transmission buffer for sending directory watcher notifications to clients.
// Allows enqueueing multiple messages in a buffer before sending an IPC message
// to a client.
class WatchBuffer {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(WatchBuffer);
  WatchBuffer() = default;

  zx_status_t AddMsg(const zx::channel& c, unsigned event, fbl::StringPiece name);
  zx_status_t Send(const zx::channel& c);

 private:
  size_t watch_buf_size_ = 0;
  char watch_buf_[fio::MAX_BUF]{};
};

zx_status_t WatchBuffer::AddMsg(const zx::channel& c, unsigned event, fbl::StringPiece name) {
  size_t slen = name.length();
  size_t mlen = sizeof(vfs_watch_msg_t) + slen;
  if (mlen + watch_buf_size_ > sizeof(watch_buf_)) {
    // This message won't fit in the watch_buf; transmit first.
    zx_status_t status = Send(c);
    if (status != ZX_OK) {
      return status;
    }
  }
  vfs_watch_msg_t* vmsg =
      reinterpret_cast<vfs_watch_msg_t*>((uintptr_t)watch_buf_ + watch_buf_size_);
  vmsg->event = static_cast<uint8_t>(event);
  vmsg->len = static_cast<uint8_t>(slen);
  memcpy(vmsg->name, name.data(), slen);
  watch_buf_size_ += mlen;
  return ZX_OK;
}

zx_status_t WatchBuffer::Send(const zx::channel& c) {
  if (watch_buf_size_ > 0) {
    // Only write if we have something to write
    zx_status_t status = c.write(0, watch_buf_, static_cast<uint32_t>(watch_buf_size_), nullptr, 0);
    watch_buf_size_ = 0;
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t WatcherContainer::WatchDir(Vfs* vfs, Vnode* vn, uint32_t mask, uint32_t options,
                                       zx::channel channel) {
  if ((mask & fio::WATCH_MASK_ALL) == 0) {
    // No events to watch
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher(std::move(channel), mask));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (mask & fio::WATCH_MASK_EXISTING) {
    vdircookie_t dircookie;
    memset(&dircookie, 0, sizeof(dircookie));
    char readdir_buf[FDIO_CHUNK_SIZE];
    WatchBuffer wb;
    {
      // Send "fio::WATCH_EVENT_EXISTING" for all entries in readdir.
      while (true) {
        size_t actual;
        zx_status_t status =
            vfs->Readdir(vn, &dircookie, readdir_buf, sizeof(readdir_buf), &actual);
        if (status != ZX_OK || actual == 0) {
          break;
        }
        void* ptr = readdir_buf;
        while (actual >= sizeof(vdirent_t)) {
          auto dirent = reinterpret_cast<vdirent_t*>(ptr);
          if (dirent->name[0]) {
            wb.AddMsg(watcher->h, fio::WATCH_EVENT_EXISTING,
                      fbl::StringPiece(dirent->name, dirent->size));
          }
          size_t entry_len = dirent->size + sizeof(vdirent_t);
          ZX_ASSERT(entry_len <= actual);  // Prevent underflow
          actual -= entry_len;
          ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(entry_len) +
                                        reinterpret_cast<uintptr_t>(ptr));
        }
      }
    }

    // Send fio::WATCH_EVENT_IDLE to signify that readdir has completed.
    if (mask & fio::WATCH_MASK_IDLE) {
      wb.AddMsg(watcher->h, fio::WATCH_EVENT_IDLE, "");
    }

    wb.Send(watcher->h);
  }

  fbl::AutoLock lock(&lock_);
  watch_list_.push_back(std::move(watcher));
  return ZX_OK;
}

void WatcherContainer::Notify(fbl::StringPiece name, unsigned event) {
  if (name.length() > fio::MAX_FILENAME) {
    return;
  }

  fbl::AutoLock lock(&lock_);

  if (watch_list_.is_empty()) {
    return;
  }

  uint8_t msg[sizeof(vfs_watch_msg_t) + name.length()];
  vfs_watch_msg_t* vmsg = reinterpret_cast<vfs_watch_msg_t*>(msg);
  vmsg->event = static_cast<uint8_t>(event);
  vmsg->len = static_cast<uint8_t>(name.length());
  memcpy(vmsg->name, name.data(), name.length());

  for (auto it = watch_list_.begin(); it != watch_list_.end();) {
    if (!(it->mask & (1 << event))) {
      ++it;
      continue;
    }

    zx_status_t status = it->h.write(0, msg, static_cast<uint32_t>(sizeof(msg)), nullptr, 0);
    if (status < 0) {
      // Lazily remove watchers when their handles cannot accept incoming
      // watch messages.
      auto to_remove = it;
      ++it;
      watch_list_.erase(to_remove);
    } else {
      ++it;
    }
  }
}

}  // namespace fs
