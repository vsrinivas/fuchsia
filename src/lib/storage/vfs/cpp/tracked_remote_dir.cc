// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/tracked_remote_dir.h"

#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/string.h>

#include "src/lib/storage/vfs/cpp/remote_dir.h"

namespace fs {

TrackedRemoteDir::TrackedRemoteDir(fidl::ClientEnd<fuchsia_io::Directory> remote)
    : RemoteDir(std::move(remote)),
      tracker_(this, client_end().channel()->get(), ZX_CHANNEL_PEER_CLOSED),
      container_(nullptr) {
  ZX_DEBUG_ASSERT(!IsTracked());
}

zx_status_t TrackedRemoteDir::AddAsTrackedEntry(async_dispatcher_t* dispatcher,
                                                PseudoDir* container, fbl::String name) {
  if (IsTracked()) {
    return ZX_ERR_BAD_STATE;
  }
  ZX_DEBUG_ASSERT(!IsTracked());
  ZX_DEBUG_ASSERT(!tracker_.is_pending());
  ZX_DEBUG_ASSERT(container_ == nullptr);
  ZX_DEBUG_ASSERT(container != nullptr);

  zx_status_t status = container->AddEntry(name, fbl::RefPtr(this));
  if (status != ZX_OK) {
    return status;
  }

  container_ = container;
  name_ = std::move(name);

  // When tracker_ completes, |HandleClose()| is invoked.
  return tracker_.Begin(dispatcher);
}

void TrackedRemoteDir::HandleClose(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  ZX_DEBUG_ASSERT(IsTracked());
  container_->RemoveEntry(name_, this);
  // After we have removed ourself from the PseudoDirectory, we may have been deleted. Do not
  // deference anything else within |this|.
}

bool TrackedRemoteDir::IsTracked() const { return container_ != nullptr; }

}  // namespace fs
