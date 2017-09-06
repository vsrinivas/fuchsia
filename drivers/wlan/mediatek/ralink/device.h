// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/iotxn.h>
#include <ddktl/device.h>
#include <ddktl/protocol/wlan.h>
#include <driver/usb.h>
#include <magenta/compiler.h>
#include <mx/time.h>
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
    Device(mx_device_t* device, usb_protocol_t* usb, uint8_t bulk_in,
           std::vector<uint8_t>&& bulk_out);
    ~Device();

    mx_status_t Bind();

    // ddk::Device methods
    void DdkUnbind();
    void DdkRelease();

    // ddk::WlanmacProtocol methods
    mx_status_t WlanmacQuery(uint32_t options, ethmac_info_t* info);
    mx_status_t WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy);
    void WlanmacStop();
    void WlanmacTx(uint32_t options, const void* data, size_t len);
    mx_status_t WlanmacSetChannel(uint32_t options, wlan_channel_t* chan);

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
    mx_status_t ReadRegister(uint16_t offset, uint32_t* value);
    template <uint16_t A> mx_status_t ReadRegister(Register<A>* reg);
    mx_status_t WriteRegister(uint16_t offset, uint32_t value);
    template <uint16_t A> mx_status_t WriteRegister(const Register<A>& reg);

    // read and write the eeprom
    mx_status_t ReadEeprom();
    mx_status_t ReadEepromField(uint16_t addr, uint16_t* value);
    template <uint16_t A> mx_status_t ReadEepromField(EepromField<A>* field);
    template <uint16_t A> mx_status_t WriteEepromField(const EepromField<A>& field);
    mx_status_t ValidateEeprom();

    // read and write baseband processor registers
    mx_status_t ReadBbp(uint8_t addr, uint8_t* val);
    template <uint8_t A> mx_status_t ReadBbp(BbpRegister<A>* reg);
    mx_status_t WriteBbp(uint8_t addr, uint8_t val);
    template <uint8_t A> mx_status_t WriteBbp(const BbpRegister<A>& reg);
    mx_status_t WaitForBbp();

    // read and write rf registers
    mx_status_t ReadRfcsr(uint8_t addr, uint8_t* val);
    template <uint8_t A> mx_status_t ReadRfcsr(RfcsrRegister<A>* reg);
    mx_status_t WriteRfcsr(uint8_t addr, uint8_t val);
    template <uint8_t A> mx_status_t WriteRfcsr(const RfcsrRegister<A>& reg);

    // send a command to the MCU
    mx_status_t McuCommand(uint8_t command, uint8_t token, uint8_t arg0, uint8_t arg1);

    // initialization functions
    mx_status_t LoadFirmware();
    mx_status_t EnableRadio();
    mx_status_t InitRegisters();
    mx_status_t InitBbp();
    mx_status_t InitRfcsr();

    mx_status_t DetectAutoRun(bool* autorun);
    mx_status_t DisableWpdma();
    mx_status_t WaitForMacCsr();
    mx_status_t SetRxFilter();
    mx_status_t NormalModeSetup();
    mx_status_t StartQueues();
    mx_status_t StopRxQueue();
    mx_status_t SetupInterface();

    mx_status_t ConfigureChannel(const Channel& channel);
    mx_status_t ConfigureTxPower(const Channel& channel);

    template <typename R, typename Predicate>
    mx_status_t BusyWait(R* reg, Predicate pred,
            mx_duration_t delay = kDefaultBusyWait);

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

    constexpr static mx_duration_t kDefaultBusyWait = MX_USEC(100);

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
