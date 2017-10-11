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

#include "hif.h"

#include <ddktl/protocol/pci.h>

namespace ath10k {

class PciBus : public Hif {
  public:
    explicit PciBus(pci_protocol_t* pci) : pci_(pci) {}

    // HIF methods
    virtual zx_status_t Bind() override;
    virtual zx_status_t Init(HwRev* rev) override;

    virtual zx_status_t TxSg(uint8_t pipe_id, HifSGItem* items, size_t n_items) override;
    virtual zx_status_t DiagRead(uint32_t address, void* buf, size_t buf_len) override;
    virtual zx_status_t DiagWrite(uint32_t address, const void* buf, size_t buf_len) override;
    virtual zx_status_t ExchangeBmiMsg(void* req, uint32_t req_len, void* resp,
                                       uint32_t* resp_len) override;
    virtual zx_status_t Start() override;
    virtual void Stop() override;
    virtual zx_status_t MapServiceToPipe(uint16_t service_id, uint8_t* ul_pipe,
                                         uint8_t* dl_pipe) override;
    virtual void GetDefaultPipe(uint8_t* ul_pipe, uint8_t* dl_pipe) override;
    virtual void SendCompleteCheck(uint8_t pipe_id, int force) override;
    virtual uint16_t GetFreeQueueNumber(uint8_t pipe_id) override;
    virtual uint32_t Read32(uint32_t address) override;
    virtual void Write32(uint32_t address, uint32_t value) override;
    virtual zx_status_t PowerUp() override;
    virtual void PowerDown() override;
    virtual zx_status_t Suspend() override;
    virtual zx_status_t Resume() override;
    virtual zx_status_t FetchCalEeprom(void** data, size_t* data_len) override;

  private:
    ddk::PciProtocolProxy pci_;

    // PCI power save. When disabled, avoids frequent locking on MMIO read/write.
    bool pci_ps_ = false;
};

}  // namespace ath10k
