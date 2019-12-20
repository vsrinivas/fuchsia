// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "payload-streamer.h"

#include <lib/async/default.h>
#include <lib/fidl-async/cpp/bind.h>

namespace disk_pave {

PayloadStreamer::PayloadStreamer(zx::channel chan, fbl::unique_fd payload)
    : payload_(std::move(payload)) {
  fidl::Bind(async_get_default_dispatcher(), std::move(chan), this);
}

PayloadStreamer::~PayloadStreamer() {
  if (!eof_reached_) {
    // Reads the entire file if it wasn't completely read by the channel.
    // This is necessary due to implementation of streaming protocol which
    // forces entire file to be transferred.
    char buf[8192];
    while (read(payload_.get(), &buf, sizeof(buf)) > 0)
      continue;
  }
}

void PayloadStreamer::RegisterVmo(zx::vmo vmo, RegisterVmoCompleter::Sync completer) {
  if (vmo_) {
    vmo_.reset();
    mapper_.Unmap();
  }

  auto status = mapper_.Map(vmo, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }

  vmo_ = std::move(vmo);
  completer.Reply(ZX_OK);
}

void PayloadStreamer::ReadData(ReadDataCompleter::Sync completer) {
  ::llcpp::fuchsia::paver::ReadResult result = {};
  if (!vmo_) {
    zx_status_t status = ZX_ERR_BAD_STATE;
    result.set_err(&status);
    completer.Reply(result);
    return;
  }
  if (eof_reached_) {
    result.set_eof(&eof_reached_);
    completer.Reply(result);
    return;
  }

  ssize_t n = read(payload_.get(), mapper_.start(), mapper_.size());
  if (n == 0) {
    eof_reached_ = true;
    result.set_eof(&eof_reached_);
    completer.Reply(result);
  } else if (n < 0) {
    zx_status_t status = ZX_ERR_IO;
    result.set_err(&status);
    completer.Reply(result);
  } else {
    // completer.Reply must be called from within this else block since otherwise
    // |info| will go out of scope
    ::llcpp::fuchsia::paver::ReadInfo info{.offset = 0, .size = static_cast<uint64_t>(n)};
    result.set_info(&info);
    completer.Reply(result);
  }
}

}  // namespace disk_pave
