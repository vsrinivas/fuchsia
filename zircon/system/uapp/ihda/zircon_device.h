// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/device/intel-hda.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace audio {
namespace intel_hda {

class ZirconDevice {
public:
    zx_status_t Connect();
    void Disconnect();

    const char* dev_name() const { return dev_name_ ? dev_name_ : "<unknown>"; }

    using EnumerateCbk = zx_status_t (*)(void* ctx, uint32_t id, const char* const str);
    static zx_status_t Enumerate(void* ctx,
                                 const char* const dev_path,
                                 EnumerateCbk cbk);

protected:
    explicit ZirconDevice(const char* const dev_name)
        : dev_name_(::strdup(dev_name)) { }

    ~ZirconDevice() {
        Disconnect();
        if (dev_name_) ::free(dev_name_);
    }

    template <typename ReqType, typename RespType>
    zx_status_t CallDevice(const ReqType& req, RespType* resp, uint64_t timeout_msec = 100) {
        if (!resp)
            return ZX_ERR_INVALID_ARGS;

        zx_status_t res = Connect();
        if (res != ZX_OK)
            return res;

        zx_channel_call_args_t args;
        memset(&args, 0, sizeof(args));

        // TODO(johngro) : get rid of this const cast
        args.wr_bytes     = const_cast<ReqType*>(&req);
        args.wr_num_bytes = sizeof(req);
        args.rd_bytes     = resp;
        args.rd_num_bytes = sizeof(*resp);

        return CallDevice(args, timeout_msec);
    }

    template <typename ReqType>
    static void InitRequest(ReqType* req, ihda_cmd_t cmd) {
        ZX_DEBUG_ASSERT(req != nullptr);
        memset(req, 0, sizeof(*req));
        do {
            req->hdr.transaction_id = ++transaction_id_;
        } while (req->hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID);
        req->hdr.cmd = cmd;
    }

    char* dev_name_;
    zx_handle_t dev_channel_ = ZX_HANDLE_INVALID;

private:
    zx_status_t CallDevice(const zx_channel_call_args_t& args, uint64_t timeout_msec);
    static uint32_t transaction_id_;
};

}  // namespace audio
}  // namespace intel_hda
