// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "payload-streamer.h"

#include <lib/async/default.h>

namespace netsvc {

PayloadStreamer::PayloadStreamer(zx::channel chan, ReadCallback callback)
    : read_(std::move(callback)) {
    fidl_bind(async_get_default_dispatcher(), chan.release(),
              reinterpret_cast<fidl_dispatch_t*>(fuchsia_paver_PayloadStream_dispatch),
              this, &ops_);
}

zx_status_t PayloadStreamer::RegisterVmo(zx_handle_t vmo_handle, fidl_txn_t* txn) {
    zx::vmo vmo(vmo_handle);

    if (vmo_) {
        vmo_.reset();
        mapper_.Unmap();
    }

    vmo_ = std::move(vmo);
    auto status = mapper_.Map(vmo_, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    return fuchsia_paver_PayloadStreamRegisterVmo_reply(txn, status);
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

    size_t actual;
    auto status = read_(mapper_.start(), read_offset_, mapper_.size(), &actual);
    if (status != ZX_OK) {
        result.tag = fuchsia_paver_ReadResultTag_err;
        result.err = status;
    } else if (actual == 0) {
        eof_reached_ = true;
        result.tag = fuchsia_paver_ReadResultTag_eof;
        result.eof = true;
    } else {
        result.tag = fuchsia_paver_ReadResultTag_info;
        result.info.offset = 0;
        result.info.size = actual;
        read_offset_ += actual;
    }

    return fuchsia_paver_PayloadStreamReadData_reply(txn, &result);
}

} // namespace netsvc
