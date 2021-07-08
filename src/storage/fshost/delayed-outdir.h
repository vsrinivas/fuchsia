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
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace fshost {

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
        delayed_vfs_(fs::ManagedVfs(outgoing_dir_delayed_loop_->dispatcher())) {}

  ~DelayedOutdir() {
    if (!started_) {
      // if we haven't actually started the async loop yet, we need to so we can tear down any
      // connections that got created in the mean time.
      outgoing_dir_delayed_loop_->StartThread("delayed_outgoing_dir");
    }
    fit::bridge<zx_status_t> bridge;
    delayed_vfs_.Shutdown(bridge.completer.bind());
    auto promise_shutdown = bridge.consumer.promise_or(::fit::error());

    fit::result<zx_status_t, void> result = fit::run_single_threaded(std::move(promise_shutdown));
    if (!result.is_ok()) {
      FX_LOGS(ERROR) << "error running fit executor to shutdown delayed outdir vfs";
    } else if (result.value() != ZX_OK) {
      FX_LOGS(ERROR) << "error shutting down delayed outdir vfs: "
                     << zx_status_get_string(result.value());
    }
  }

  fbl::RefPtr<fs::RemoteDir> Initialize(fidl::ClientEnd<fuchsia_io::Directory> filesystems_client) {
    auto delayed_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    delayed_dir->AddEntry("fs", fbl::MakeRefCounted<fs::RemoteDir>(std::move(filesystems_client)));

    // Add the delayed vfs to the main one under /delayed
    auto delayed = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (!delayed.is_ok()) {
      FX_LOGS(ERROR) << "delayed outdir failed to create channel";
      return fbl::RefPtr<fs::RemoteDir>();
    }
    delayed_vfs_.ServeDirectory(delayed_dir, std::move(delayed->server));

    return fbl::MakeRefCounted<fs::RemoteDir>(std::move(delayed->client));
  }

  void Start() {
    outgoing_dir_delayed_loop_->StartThread("delayed_outgoing_dir");
    started_ = true;
  }

 private:
  std::unique_ptr<async::Loop> outgoing_dir_delayed_loop_;
  fs::ManagedVfs delayed_vfs_;
  bool started_ = false;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_DELAYED_OUTDIR_H_
