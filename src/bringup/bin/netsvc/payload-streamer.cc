// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "payload-streamer.h"

#include <lib/async/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include "zircon/errors.h"

namespace netsvc {

PayloadStreamer::PayloadStreamer(zx::channel chan, ReadCallback callback)
    : read_(std::move(callback)) {
  fidl::BindSingleInFlightOnly(async_get_default_dispatcher(), std::move(chan), this);
}

void PayloadStreamer::RegisterVmo(zx::vmo vmo, RegisterVmoCompleter::Sync completer) {
  if (vmo_) {
    completer.Reply(ZX_ERR_ALREADY_BOUND);
    return;
  }

  vmo_ = std::move(vmo);
  auto status = mapper_.Map(vmo_, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  completer.Reply(status);
}

void PayloadStreamer::ReadData(ReadDataCompleter::Sync completer) {
  using ::llcpp::fuchsia::paver::ReadResult;
  ReadResult result;
  if (!vmo_) {
    zx_status_t status = ZX_ERR_BAD_STATE;
    result.set_err(fidl::unowned_ptr(&status));
    completer.Reply(std::move(result));
    return;
  }
  if (eof_reached_) {
    result.set_eof(fidl::unowned_ptr(&eof_reached_));
    completer.Reply(std::move(result));
    return;
  }

  size_t actual;
  auto status = read_(mapper_.start(), read_offset_, mapper_.size(), &actual);
  if (status != ZX_OK) {
    result.set_err(fidl::unowned_ptr(&status));
    completer.Reply(std::move(result));
  } else if (actual == 0) {
    eof_reached_ = true;
    result.set_eof(fidl::unowned_ptr(&eof_reached_));
    completer.Reply(std::move(result));
  } else {
    // completer.Reply must be called from within this else block since otherwise
    // |info| will go out of scope
    ::llcpp::fuchsia::paver::ReadInfo info{.offset = 0, .size = actual};
    result.set_info(fidl::unowned_ptr(&info));
    read_offset_ += actual;
    completer.Reply(std::move(result));
  }
}

}  // namespace netsvc
