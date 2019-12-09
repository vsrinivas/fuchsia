// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/string.h>
#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <lib/async/cpp/wait.h>

#include "pseudo_dir.h"
#include "vnode.h"

namespace fs {

// A remote directory which automatically removes itself once the remote channel
// being tracked is closed.
//
// This class is thread-compatible.
class TrackedRemoteDir : public RemoteDir {
 public:
  // Create a directory which is accessed remotely through |remote|.
  explicit TrackedRemoteDir(zx::channel remote);

  // Adds |this| as an entry to |container| with the label |name|.
  //
  // Begins monitoring |remote| (provided at construction-time) for |PEER_CLOSED|.
  // When this signal is activated, the |name| entry is removed from |container|.
  //
  // Returns |ZX_ERR_BAD_STATE| if this directory is already tracked.
  // Returns an error if an entry named |name| cannot be added to |container|.
  // Returns an error if the underlying handle cannot be monitored for peer closed.
  zx_status_t AddAsTrackedEntry(async_dispatcher_t* dispatcher, PseudoDir* container,
                                fbl::String name);

 private:
  void HandleClose(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                   const zx_packet_signal_t* signal);
  bool IsTracked() const;

  async::WaitMethod<TrackedRemoteDir, &TrackedRemoteDir::HandleClose> tracker_;
  fbl::String name_;
  PseudoDir* container_;
};

}  // namespace fs
