// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/device/intel-hda.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace audio {
namespace intel_hda {

class MagentaDevice {
public:
    mx_status_t Connect();
    void Disconnect();

    const char* dev_name() const { return dev_name_ ? dev_name_ : "<unknown>"; }

    using EnumerateCbk = mx_status_t (*)(void* ctx, uint32_t id, const char* const str);
    static mx_status_t Enumerate(void* ctx,
                                 const char* const dev_path,
                                 const char* const dev_fmt,
                                 EnumerateCbk cbk);

protected:
    explicit MagentaDevice(const char* const dev_name)
        : dev_name_(::strdup(dev_name)) { }

    ~MagentaDevice() {
        Disconnect();
        if (dev_name_) ::free(dev_name_);
    }

    template <typename ReqType, typename RespType>
    mx_status_t CallDevice(const ReqType& req, RespType* resp, uint64_t timeout_msec = 100) {
        if (!resp)
            return MX_ERR_INVALID_ARGS;

        mx_status_t res = Connect();
        if (res != MX_OK)
            return res;

        mx_channel_call_args_t args;
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
        MX_DEBUG_ASSERT(req != nullptr);
        memset(req, 0, sizeof(*req));
        do {
            req->hdr.transaction_id = ++transaction_id_;
        } while (req->hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID);
        req->hdr.cmd = cmd;
    }

    char* dev_name_;
    mx_handle_t dev_channel_ = MX_HANDLE_INVALID;

private:
    mx_status_t CallDevice(const mx_channel_call_args_t& args, uint64_t timeout_msec);
    static uint32_t transaction_id_;
};

}  // namespace audio
}  // namespace intel_hda
