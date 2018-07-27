// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>

#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include <chrono>

namespace wlan {

class Buffer;
class StartRequest;
template <typename T> class MlmeMsg;

// Power Saving configuration managing TIM and DTIM.
class PsCfg {
   public:
    void SetDtimPeriod(uint8_t dtim_period) {
        // DTIM period of 0 is reserved.
        ZX_DEBUG_ASSERT(dtim_period > 0);

        dtim_period_ = dtim_period;
        dtim_count_ = dtim_period - 1;
    }

    uint8_t dtim_period() const { return dtim_period_; }

    uint8_t dtim_count() const { return dtim_count_; }

    TrafficIndicationMap* GetTim() { return &tim_; }

    const TrafficIndicationMap* GetTim() const { return &tim_; }

    uint8_t NextDtimCount() {
        if (IsDtim()) {
            dtim_count_ = dtim_period_ - 1;
            return dtim_count_;
        }
        return --dtim_count_;
    }

    uint8_t LastDtimCount() {
        if (dtim_count_ == dtim_period_ - 1) { return 0; }
        return dtim_count_ + 1;
    }

    bool IsDtim() const { return dtim_count_ == 0; }

   private:
    TrafficIndicationMap tim_;
    uint8_t dtim_period_ = 1;
    uint8_t dtim_count_ = 0;
};

class BssInterface {
   public:
    virtual const common::MacAddr& bssid() const = 0;
    virtual uint64_t timestamp() = 0;

    // Assigns an AID to the given client. Returns an error if there is no AID
    // available anymore.
    virtual zx_status_t AssignAid(const common::MacAddr& client, aid_t* out_aid) = 0;
    // Releases the AID associated with the given client. The AID will be
    // available afterwards and can get assigned to other, newly associated
    // clients.
    virtual zx_status_t ReleaseAid(const common::MacAddr& client) = 0;

    virtual seq_t NextSeq(const MgmtFrameHeader& hdr) = 0;
    virtual seq_t NextSeq(const MgmtFrameHeader& hdr, uint8_t aci) = 0;
    virtual seq_t NextSeq(const DataFrameHeader& hdr) = 0;

    virtual zx_status_t EthToDataFrame(const EthFrame& frame,
                                       fbl::unique_ptr<Packet>* out_packet) = 0;

    virtual bool IsRsn() const = 0;
    virtual bool IsHTReady() const = 0;
    virtual bool IsCbw40RxReady() const = 0;
    virtual bool IsCbw40TxReady() const = 0;
    virtual HtCapabilities BuildHtCapabilities() const = 0;
    virtual HtOperation BuildHtOperation(const wlan_channel_t& chan) const = 0;

    virtual zx_status_t SendMgmtFrame(fbl::unique_ptr<Packet> packet) = 0;
    virtual zx_status_t SendDataFrame(fbl::unique_ptr<Packet> packet) = 0;
    virtual zx_status_t SendEthFrame(fbl::unique_ptr<Packet> packet) = 0;

    // Indications reported from lower MAC layer.
    virtual void OnPreTbtt() = 0;
    virtual void OnBcnTxComplete() = 0;

    virtual wlan_channel_t Chan() const = 0;
};

}  // namespace wlan
