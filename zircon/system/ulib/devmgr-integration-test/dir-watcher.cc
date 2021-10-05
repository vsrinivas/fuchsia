// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/clock.h>

#include <fbl/unique_fd.h>

namespace devmgr_integration_test {

namespace fio = fuchsia_io;

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
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fio::Directory>(caller.borrow_channel()))
                    ->Watch(fio::wire::kWatchMaskRemoved, 0, zx::channel(server.release()));
  if (!result.ok()) {
    return result.status();
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
    uint8_t buf[fio::wire::kMaxBuf];
    uint32_t actual_len;
    status = client_.read(0, buf, nullptr, sizeof(buf), 0, &actual_len, nullptr);
    if (status != ZX_OK) {
      return status;
    }
    if (buf[0] != fio::wire::kWatchEventRemoved) {
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
