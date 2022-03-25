// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "payload-streamer.h"

namespace fastboot {
namespace internal {

// Register a vmo for reading data from the payload
void PayloadStreamer::RegisterVmo(RegisterVmoRequestView request,
                                  RegisterVmoCompleter::Sync& completer) {
  if (vmo_) {
    completer.Reply(ZX_ERR_ALREADY_BOUND);
    return;
  }

  auto status = mapper_.Map(request->vmo, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }

  vmo_ = std::move(request->vmo);
  completer.Reply(ZX_OK);
}

// Read the payload data into the vmo registered via RegisterVmo(...)
void PayloadStreamer::ReadData(ReadDataRequestView request, ReadDataCompleter::Sync& completer) {
  fuchsia_paver::wire::ReadResult result = {};
  if (!vmo_) {
    result.set_err(ZX_ERR_BAD_STATE);
    completer.Reply(std::move(result));
    return;
  }

  size_t to_read = std::min(mapper_.size(), size_ - read_offset_);
  memcpy(mapper_.start(), data_ + read_offset_, to_read);
  read_offset_ += to_read;

  if (to_read == 0) {
    eof_reached_ = true;
    result.set_eof(eof_reached_);
    completer.Reply(std::move(result));
  } else {
    // completer.Reply must be called from within this else block since otherwise
    // |info| will go out of scope
    fuchsia_paver::wire::ReadInfo info{.offset = 0, .size = static_cast<uint64_t>(to_read)};
    result.set_info(fidl::ObjectView<fuchsia_paver::wire::ReadInfo>::FromExternal(&info));
    completer.Reply(std::move(result));
  }
}

}  // namespace internal
}  // namespace fastboot
