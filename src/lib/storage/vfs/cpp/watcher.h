// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_WATCHER_H_
#define SRC_LIB_STORAGE_VFS_CPP_WATCHER_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/zx/channel.h>
#include <zircon/device/vfs.h>

#include <memory>
#include <mutex>
#include <string_view>

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>

#include "src/lib/storage/vfs/cpp/vfs.h"

namespace fs {

// Implements directory watching , holding a list of watchers
class WatcherContainer {
 public:
  WatcherContainer();
  ~WatcherContainer();

  zx_status_t WatchDir(Vfs* vfs, Vnode* vn, fuchsia_io::wire::WatchMask mask, uint32_t options,
                       fidl::ServerEnd<fuchsia_io::DirectoryWatcher> server_end);

  // Notifies all VnodeWatchers in the watch list, if their mask indicates they are interested in
  // the incoming event.
  void Notify(std::string_view name, fuchsia_io::wire::WatchEvent event);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(WatcherContainer);

  // A simple structure which holds a channel to a watching client, as well as a mask of signals
  // they are interested in hearing about.
  struct VnodeWatcher : public fbl::DoublyLinkedListable<std::unique_ptr<VnodeWatcher>> {
    VnodeWatcher(fidl::ServerEnd<fuchsia_io::DirectoryWatcher> server_end,
                 fuchsia_io::wire::WatchMask mask);
    ~VnodeWatcher();

    fidl::ServerEnd<fuchsia_io::DirectoryWatcher> server_end;
    fuchsia_io::WatchMask mask;
  };

  std::mutex lock_;
  fbl::DoublyLinkedList<std::unique_ptr<VnodeWatcher>> watch_list_ __TA_GUARDED(lock_);
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_WATCHER_H_
