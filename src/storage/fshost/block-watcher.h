// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_BLOCK_WATCHER_H_
#define SRC_STORAGE_FSHOST_BLOCK_WATCHER_H_

#include <fidl/fuchsia.fshost/cpp/wire.h>

#include <condition_variable>
#include <memory>

#include <fbl/span.h>

#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/fs-manager.h"

namespace fshost {

class BlockWatcher {
 public:
  // Does not take ownership of |config|, which must refer to a valid object that outlives this
  // object.
  BlockWatcher(FsManager& fshost, const Config* config);
  ~BlockWatcher() { ShutDown(); }

  // Run the block watcher on a separate thread.
  void Run();

  // Increment the pause count for the block watcher.  This function will not return until the block
  // watcher is no longer running.  The block watcher will not receive any new device events while
  // paused.
  zx_status_t Pause();

  // Decrement the pause count for the block watcher.
  zx_status_t Resume();

  // Shut down the block watcher.  This will block until complete.
  void ShutDown();

 private:
  void Thread();

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
  // pause_count_ == -1 means shut down.
  int pause_count_ TA_GUARDED(lock_) = 0;
  // Notified when watcher thread should resume, or when watcher thread is paused.
  std::condition_variable_any pause_condition_;
  zx::event pause_event_;
  bool is_paused_ TA_GUARDED(lock_) = false;

  FilesystemMounter mounter_;
  BlockDeviceManager device_manager_;
  std::thread thread_;
};

class BlockWatcherServer final : public fidl::WireServer<fuchsia_fshost::BlockWatcher> {
 public:
  // Creates a new fs::Service backed by a new BlockWatcherServer, to be inserted into
  // a pseudo fs. |watcher| is unowned and must outlive the returned instance.
  static fbl::RefPtr<fs::Service> Create(async_dispatcher* dispatcher, BlockWatcher& watcher);

  void Pause(PauseRequestView request, PauseCompleter::Sync& completer) override;
  void Resume(ResumeRequestView request, ResumeCompleter::Sync& completer) override;

 private:
  explicit BlockWatcherServer(BlockWatcher& watcher) : watcher_(watcher) {}

  BlockWatcher& watcher_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_BLOCK_WATCHER_H_
