/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 * Copyright (c) 2017 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include "hw.h"

#include <zircon/types.h>

#include <stdint.h>

namespace ath10k {

struct HifSGItem {
    uint16_t transfer_id;
    void* transfer_context;  // TODO: template parameter?
    void* vaddr;             // for debugging mostly
    uint32_t paddr;
    uint16_t len;
};

/**
 * The Host interconnect Framework abstracts the bus type from the upper layers.
 */
class Hif {
  public:
    virtual ~Hif() = default;

    // Device setup
    virtual zx_status_t Bind() = 0;
    virtual zx_status_t Init(HwRev* rev) = 0;

    // Send a scatter-gather list to the target
    virtual zx_status_t TxSg(uint8_t pipe_id, HifSGItem* items, size_t n_items) = 0;

    // Read firmware memory through the diagnostic interface
    virtual zx_status_t DiagRead(uint32_t address, void* buf, size_t buf_len) = 0;

    // Write firmware memory through the diagnostic interface
    virtual zx_status_t DiagWrite(uint32_t address, const void* buf, size_t buf_len) = 0;

    // API to handle HIF-specific BMI message exchanges. This API is synchronous and only allowed to
    // be called from a context that can block (sleep).
    virtual zx_status_t ExchangeBmiMsg(void* req, uint32_t req_len, void* resp,
                                       uint32_t* resp_len) = 0;

    // Starts regular operation, post BMI phase, after firmware is loaded.
    virtual zx_status_t Start() = 0;

    // Stops regular operation. Does not revert to BMI phase; call PowerDown() and PowerUp() to do
    // that.
    virtual void Stop() = 0;

    virtual zx_status_t MapServiceToPipe(uint16_t service_id, uint8_t* ul_pipe,
                                         uint8_t* dl_pipe) = 0;

    virtual void GetDefaultPipe(uint8_t* ul_pipe, uint8_t* dl_pipe) = 0;

    // Check if prior sends for the pipe have completed. Only relevant for HIF pipes that are
    // configured to be polled rather than interrupt-driven.
    virtual void SendCompleteCheck(uint8_t pipe_id, int force) = 0;

    virtual uint16_t GetFreeQueueNumber(uint8_t pipe_id) = 0;

    virtual uint32_t Read32(uint32_t address) = 0;

    virtual void Write32(uint32_t address, uint32_t value) = 0;

    virtual zx_status_t PowerUp() = 0;

    virtual void PowerDown() = 0;

    virtual zx_status_t Suspend() = 0;

    virtual zx_status_t Resume() = 0;

    virtual zx_status_t FetchCalEeprom(void** data, size_t* data_len) = 0;
};

}  // namespace ath10k
