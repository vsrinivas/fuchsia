// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_DELAYED_OUTDIR_H_
#define SRC_STORAGE_FSHOST_DELAYED_OUTDIR_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/bridge.h>
#include <lib/fit/result.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>

namespace devmgr {

// TODO(fxbug.dev/39588): This class is used to create a new RemoteDir that doesn't
// respond to any messages until `.Start()` is called. This is important for
// signaling to devcoordinator and component manager when they can start
// accessing data from pkgfs. This solution is fairly hacky, and will hopefully
// not be very long lived. Ideally the filesystems will properly pipeline
// requests, wherein each filesystem would not respond to requests until it was
// initialized.
class DelayedOutdir {
 public:
  DelayedOutdir()
      : outgoing_dir_delayed_loop_(new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread)),
        delayed_vfs_(fs::ManagedVfs(outgoing_dir_delayed_loop_->dispatcher())),
        started_(false) {}

  ~DelayedOutdir() {
    if (started_) {
      // If we've been started, we need to shutdown the VFS before destroying
      // it, otherwise the VFS might panic.
      fit::bridge<zx_status_t> bridge;
      delayed_vfs_.Shutdown(bridge.completer.bind());
      auto promise_shutdown = bridge.consumer.promise_or(::fit::error());

      fit::result<zx_status_t, void> result = fit::run_single_threaded(std::move(promise_shutdown));
      if (!result.is_ok()) {
        printf("fshost: error running fit executor to shutdown delayed outdir vfs\n");
      } else if (result.value() != ZX_OK) {
        printf("fshost: error shutting down delayed outdir vfs: %s\n",
               zx_status_get_string(result.value()));
      }
    }
  }

  fbl::RefPtr<fs::RemoteDir> Initialize(zx::channel filesystems_client) {
    auto delayed_dir = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    delayed_dir->AddEntry(
        "fs", fbl::AdoptRef<fs::RemoteDir>(new fs::RemoteDir(std::move(filesystems_client))));

    // Add the delayed vfs to the main one under /delayed
    zx::channel delayed_client, delayed_server;
    zx_status_t status = zx::channel::create(0, &delayed_client, &delayed_server);
    if (status != ZX_OK) {
      printf("fshost: delayed outdir failed to create channel\n");
      return fbl::RefPtr<fs::RemoteDir>();
    }
    delayed_vfs_.ServeDirectory(delayed_dir, std::move(delayed_server));

    return fbl::AdoptRef<fs::RemoteDir>(new fs::RemoteDir(std::move(delayed_client)));
  }

  void Start() {
    outgoing_dir_delayed_loop_->StartThread("delayed_outgoing_dir");
    started_ = true;
  }

 private:
  std::unique_ptr<async::Loop> outgoing_dir_delayed_loop_;
  fs::ManagedVfs delayed_vfs_;
  bool started_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_DELAYED_OUTDIR_H_
