// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "payload-streamer.h"

#include <lib/async/default.h>

namespace disk_pave {

PayloadStreamer::PayloadStreamer(zx::channel chan, fbl::unique_fd payload)
    : payload_(std::move(payload)) {
    fidl_bind(async_get_default_dispatcher(), chan.release(),
              reinterpret_cast<fidl_dispatch_t*>(fuchsia_paver_PayloadStream_dispatch),
              this, &ops_);
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

zx_status_t PayloadStreamer::RegisterVmo(zx_handle_t vmo_handle, fidl_txn_t* txn) {
    zx::vmo vmo(vmo_handle);

    if (vmo_) {
        vmo_.reset();
        mapper_.Unmap();
    }

    auto status = mapper_.Map(vmo, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
        return fuchsia_paver_PayloadStreamRegisterVmo_reply(txn, status);
    }

    vmo_ = std::move(vmo);
    return fuchsia_paver_PayloadStreamRegisterVmo_reply(txn, ZX_OK);
}

zx_status_t PayloadStreamer::ReadData(fidl_txn_t* txn) {
    fuchsia_paver_ReadResult result = {};
    if (!vmo_) {
        result.tag = fuchsia_paver_ReadResultTag_err;
        result.err = ZX_ERR_BAD_STATE;
        return fuchsia_paver_PayloadStreamReadData_reply(txn, &result);
    }
    if (eof_reached_) {
        result.tag = fuchsia_paver_ReadResultTag_eof;
        result.eof = true;
        return fuchsia_paver_PayloadStreamReadData_reply(txn, &result);
    }

    ssize_t n = read(payload_.get(), mapper_.start(), mapper_.size());
    if (n == 0) {
        eof_reached_ = true;
        result.tag = fuchsia_paver_ReadResultTag_eof;
        result.eof = true;
    } else if (n < 0) {
        result.tag = fuchsia_paver_ReadResultTag_err;
        result.err = ZX_ERR_IO;
    } else {
        result.tag = fuchsia_paver_ReadResultTag_info;
        result.info.offset = 0;
        result.info.size = n;
    }

    return fuchsia_paver_PayloadStreamReadData_reply(txn, &result);
}

} // namespace disk_pave
