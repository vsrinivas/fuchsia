// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/iotxn.h>
#include <ddktl/device.h>
#include <ddktl/protocol/wlan.h>
#include <driver/usb.h>
#include <zircon/compiler.h>
#include <zx/time.h>
#include <fbl/unique_ptr.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ralink {

template <uint16_t A> class Register;
template <uint8_t A> class BbpRegister;
template <uint8_t A> class RfcsrRegister;
template <uint16_t A> class EepromField;

class Device : public ddk::Device<Device, ddk::Unbindable>, public ddk::WlanmacProtocol<Device> {
  public:
    Device(zx_device_t* device, usb_protocol_t* usb, uint8_t bulk_in,
           std::vector<uint8_t>&& bulk_out);
    ~Device();

    zx_status_t Bind();

    // ddk::Device methods
    void DdkUnbind();
    void DdkRelease();

    // ddk::WlanmacProtocol methods
    zx_status_t WlanmacQuery(uint32_t options, ethmac_info_t* info);
    zx_status_t WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy);
    void WlanmacStop();
    void WlanmacTx(uint32_t options, const void* data, size_t len);
    zx_status_t WlanmacSetChannel(uint32_t options, wlan_channel_t* chan);

  private:
    // wireless channel information
    struct Channel {
        Channel(int channel, int hw_index, uint32_t N, uint32_t R, uint32_t K) :
            channel(channel), hw_index(hw_index), N(N), R(R), K(K) {}

        int channel;
        int hw_index;
        uint32_t N;
        uint32_t R;
        uint32_t K;

        uint16_t max_power = 0;
        uint16_t default_power1 = 0;
        uint16_t default_power2 = 0;
        uint16_t default_power3 = 0;
    };

    // read and write general registers
    zx_status_t ReadRegister(uint16_t offset, uint32_t* value);
    template <uint16_t A> zx_status_t ReadRegister(Register<A>* reg);
    zx_status_t WriteRegister(uint16_t offset, uint32_t value);
    template <uint16_t A> zx_status_t WriteRegister(const Register<A>& reg);

    // read and write the eeprom
    zx_status_t ReadEeprom();
    zx_status_t ReadEepromField(uint16_t addr, uint16_t* value);
    template <uint16_t A> zx_status_t ReadEepromField(EepromField<A>* field);
    template <uint16_t A> zx_status_t WriteEepromField(const EepromField<A>& field);
    zx_status_t ValidateEeprom();

    // read and write baseband processor registers
    zx_status_t ReadBbp(uint8_t addr, uint8_t* val);
    template <uint8_t A> zx_status_t ReadBbp(BbpRegister<A>* reg);
    zx_status_t WriteBbp(uint8_t addr, uint8_t val);
    template <uint8_t A> zx_status_t WriteBbp(const BbpRegister<A>& reg);
    zx_status_t WaitForBbp();

    // read and write rf registers
    zx_status_t ReadRfcsr(uint8_t addr, uint8_t* val);
    template <uint8_t A> zx_status_t ReadRfcsr(RfcsrRegister<A>* reg);
    zx_status_t WriteRfcsr(uint8_t addr, uint8_t val);
    template <uint8_t A> zx_status_t WriteRfcsr(const RfcsrRegister<A>& reg);

    // send a command to the MCU
    zx_status_t McuCommand(uint8_t command, uint8_t token, uint8_t arg0, uint8_t arg1);

    // initialization functions
    zx_status_t LoadFirmware();
    zx_status_t EnableRadio();
    zx_status_t InitRegisters();
    zx_status_t InitBbp();
    zx_status_t InitRfcsr();

    zx_status_t DetectAutoRun(bool* autorun);
    zx_status_t DisableWpdma();
    zx_status_t WaitForMacCsr();
    zx_status_t SetRxFilter();
    zx_status_t NormalModeSetup();
    zx_status_t StartQueues();
    zx_status_t StopRxQueue();
    zx_status_t SetupInterface();

    zx_status_t ConfigureChannel(const Channel& channel);
    zx_status_t ConfigureTxPower(const Channel& channel);

    template <typename R, typename Predicate>
    zx_status_t BusyWait(R* reg, Predicate pred,
            zx_duration_t delay = kDefaultBusyWait);

    void HandleRxComplete(iotxn_t* request);
    void HandleTxComplete(iotxn_t* request);

    static void ReadIotxnComplete(iotxn_t* request, void* cookie);
    static void WriteIotxnComplete(iotxn_t* request, void* cookie);

    usb_protocol_t usb_;
    fbl::unique_ptr<ddk::WlanmacIfcProxy> wlanmac_proxy_ __TA_GUARDED(lock_);

    uint8_t rx_endpt_ = 0;
    std::vector<uint8_t> tx_endpts_;

    constexpr static size_t kEepromSize = 0x0100;
    std::array<uint16_t, kEepromSize> eeprom_ = {};

    constexpr static zx_duration_t kDefaultBusyWait = ZX_USEC(100);

    // constants read out of the device
    uint16_t rt_type_ = 0;
    uint16_t rt_rev_ = 0;
    uint16_t rf_type_ = 0;

    uint8_t mac_addr_[ETH_MAC_SIZE];
    std::unordered_map<int, Channel> channels_;
    int current_channel_ = -1;
    uint16_t lna_gain_ = 0;
    uint8_t bg_rssi_offset_[3] = {};

    std::mutex lock_;
    std::vector<iotxn_t*> free_write_reqs_ __TA_GUARDED(lock_);
};

}  // namespace ralink
