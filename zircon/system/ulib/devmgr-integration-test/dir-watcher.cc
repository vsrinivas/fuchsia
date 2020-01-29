// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>

#include <fbl/unique_fd.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/watcher.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/clock.h>

namespace devmgr_integration_test {

// static
__EXPORT
zx_status_t DirWatcher::Create(fbl::unique_fd dir_fd,
                               std::unique_ptr<DirWatcher>* out_dir_watcher) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  fdio_cpp::FdioCaller caller(std::move(dir_fd));
  zx_status_t status2;
  status = fuchsia_io_DirectoryWatch(caller.borrow_channel(), fuchsia_io_WATCH_MASK_REMOVED, 0,
                                     server.release(), &status2);
  if (status == ZX_OK) {
    status = status2;
  }
  if (status != ZX_OK) {
    return status;
  }
  *out_dir_watcher = std::make_unique<DirWatcher>(std::move(client));
  return ZX_OK;
}

__EXPORT
zx_status_t DirWatcher::WaitForRemoval(const fbl::String& filename, zx::duration timeout) {
  auto deadline = zx::deadline_after(timeout);
  // Loop until we see the removal event, or wait_one fails due to timeout.
  for (;;) {
    zx_signals_t observed;
    zx_status_t status = client_.wait_one(ZX_CHANNEL_READABLE, deadline, &observed);
    if (status != ZX_OK) {
      return status;
    }
    if (!(observed & ZX_CHANNEL_READABLE)) {
      return ZX_ERR_IO;
    }

    // Messages are of the form:
    //  uint8_t event
    //  uint8_t len
    //  char* name
    uint8_t buf[fuchsia_io_MAX_BUF];
    uint32_t actual_len;
    status = client_.read(0, buf, nullptr, sizeof(buf), 0, &actual_len, nullptr);
    if (status != ZX_OK) {
      return status;
    }
    if (buf[0] != fuchsia_io_WATCH_EVENT_REMOVED) {
      continue;
    }
    if (filename.length() == 0) {
      // Waiting on any file.
      return ZX_OK;
    }
    if ((buf[1] == filename.length()) &&
        (memcmp(buf + 2, filename.c_str(), filename.length()) == 0)) {
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

}  // namespace devmgr_integration_test
