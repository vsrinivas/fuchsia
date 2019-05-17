// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <fbl/unique_fd.h>
#include <fuchsia/paver/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

namespace netsvc {

// Reads the data into the vmo at offset, size. Can block.
using ReadCallback = fbl::Function<zx_status_t(void* /*buf*/, size_t /*offset*/, size_t /*size*/,
                                               size_t* /*actual*/)>;

class PayloadStreamer {
public:
    PayloadStreamer(zx::channel chan, ReadCallback callback);

    PayloadStreamer(const PayloadStreamer&) = delete;
    PayloadStreamer& operator=(const PayloadStreamer&) = delete;
    PayloadStreamer(PayloadStreamer&&) = delete;
    PayloadStreamer& operator=(PayloadStreamer&&) = delete;

    zx_status_t RegisterVmo(zx_handle_t vmo_handle, fidl_txn_t* txn);

    zx_status_t ReadData(fidl_txn_t* txn);

private:
    using Binder = fidl::Binder<PayloadStreamer>;

    static constexpr fuchsia_paver_PayloadStream_ops_t ops_ = {
        .RegisterVmo = Binder::BindMember<&PayloadStreamer::RegisterVmo>,
        .ReadData = Binder::BindMember<&PayloadStreamer::ReadData>,
    };

    ReadCallback read_;
    zx::vmo vmo_;
    fzl::VmoMapper mapper_;
    size_t read_offset_ = 0;
    bool eof_reached_ = false;
};

} // namespace netsvc
