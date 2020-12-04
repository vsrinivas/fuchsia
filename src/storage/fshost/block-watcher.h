// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_BLOCK_WATCHER_H_
#define SRC_STORAGE_FSHOST_BLOCK_WATCHER_H_

#include <fuchsia/fshost/llcpp/fidl.h>

#include <memory>

#include <fbl/span.h>
#include <fs/service.h>

#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/fshost/fshost-options.h"

namespace devmgr {

class BlockWatcher {
 public:
  BlockWatcher(FsManager& fshost, FshostOptions options);

  void Run();

  // Increment the pause count for the block watcher.  This function will not return until the block
  // watcher is no longer running.  The block watcher will not receive any new device events while
  // paused.
  zx_status_t Pause();

  // Decrement the pause count for the block watcher.
  zx_status_t Resume();

 private:
  // Returns true if we received a WATCH_EVENT_IDLE and the watcher is paused.
  bool Callback(int dirfd, int event, const char* name);

  bool ProcessWatchMessages(fbl::Span<uint8_t> buf, int dirfd);

  // Returns kSignalWatcherPaused if the watcher is paused, ZX_CHANNEL_PEER_CLOSED if the watcher
  // channel was closed, and ZX_CHANNEL_READABLE if data was read, and 0 if some other error
  // occured. |buf| should be a buffer of size |buf_len|. |read_len| will be updated to contain the
  // actual number of bytes read.
  zx_signals_t WaitForWatchMessages(const zx::unowned_channel& watcher_chan, bool finished_startup,
                                    fbl::Span<uint8_t>& buf);

  std::mutex lock_;
  unsigned int pause_count_ TA_GUARDED(lock_) = 0;
  // Notified when watcher thread should resume, or when watcher thread is paused.
  std::condition_variable_any pause_condition_;
  zx::event pause_event_;
  bool is_paused_ TA_GUARDED(lock_) = false;

  FilesystemMounter mounter_;
  BlockDeviceManager device_manager_;
};

class BlockWatcherServer final : public llcpp::fuchsia::fshost::BlockWatcher::Interface {
 public:
  // Creates a new fs::Service backed by a new BlockWatcherServer, to be inserted into
  // a pseudo fs. |watcher| is unowned and must outlive the returned instance.
  static fbl::RefPtr<fs::Service> Create(async_dispatcher* dispatcher, BlockWatcher& watcher);

  void Pause(PauseCompleter::Sync& completer) override;
  void Resume(ResumeCompleter::Sync& completer) override;

 private:
  explicit BlockWatcherServer(BlockWatcher& watcher) : watcher_(watcher) {}

  BlockWatcher& watcher_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_BLOCK_WATCHER_H_
