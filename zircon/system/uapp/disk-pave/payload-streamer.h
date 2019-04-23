// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <fuchsia/paver/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

namespace disk_pave {

class PayloadStreamer {
public:
    PayloadStreamer(zx::channel chan, fbl::unique_fd payload);
    ~PayloadStreamer();

    PayloadStreamer(const PayloadStreamer&) = delete;
    PayloadStreamer& operator=(const PayloadStreamer&) = delete;
    PayloadStreamer(PayloadStreamer&&) = delete;
    PayloadStreamer& operator=(PayloadStreamer&&) = delete;

    zx_status_t RegisterVmo(zx_handle_t vmo_handle, fidl_txn_t* txn);

    zx_status_t ReadData(fidl_txn_t* txn);

private:
    using Binder = fidl::Binder<PayloadStreamer>;

    fbl::unique_fd payload_;
    static constexpr fuchsia_paver_PayloadStream_ops_t ops_ = {
        .RegisterVmo = Binder::BindMember<&PayloadStreamer::RegisterVmo>,
        .ReadData = Binder::BindMember<&PayloadStreamer::ReadData>,
    };
    zx::vmo vmo_;
    fzl::VmoMapper mapper_;
    bool eof_reached_ = false;
};

} // namespace disk_pave
