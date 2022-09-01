// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/payload-streamer.h"

#include <lib/async/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <zircon/errors.h>

namespace netsvc {

PayloadStreamer::PayloadStreamer(fidl::ServerEnd<fuchsia_paver::PayloadStream> server_end,
                                 ReadCallback callback)
    : read_(std::move(callback)) {
  fidl::BindSingleInFlightOnly(async_get_default_dispatcher(), std::move(server_end), this);
}

void PayloadStreamer::RegisterVmo(RegisterVmoRequestView request,
                                  RegisterVmoCompleter::Sync& completer) {
  if (vmo_) {
    completer.Reply(ZX_ERR_ALREADY_BOUND);
    return;
  }

  vmo_ = std::move(request->vmo);
  auto status = mapper_.Map(vmo_, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  completer.Reply(status);
}

void PayloadStreamer::ReadData(ReadDataCompleter::Sync& completer) {
  using fuchsia_paver::wire::ReadResult;
  if (!vmo_) {
    completer.Reply(ReadResult::WithErr(ZX_ERR_BAD_STATE));
    return;
  }
  if (eof_reached_) {
    completer.Reply(ReadResult::WithEof(eof_reached_));
    return;
  }

  size_t actual;
  auto status = read_(mapper_.start(), read_offset_, mapper_.size(), &actual);
  if (status != ZX_OK) {
    completer.Reply(ReadResult::WithErr(status));
  } else if (actual == 0) {
    eof_reached_ = true;
    completer.Reply(ReadResult::WithEof(eof_reached_));
  } else {
    // completer.Reply must be called from within this else block since otherwise
    // |info| will go out of scope
    fuchsia_paver::wire::ReadInfo info{.offset = 0, .size = actual};
    read_offset_ += actual;
    completer.Reply(
        ReadResult::WithInfo(fidl::ObjectView<fuchsia_paver::wire::ReadInfo>::FromExternal(&info)));
  }
}

}  // namespace netsvc
