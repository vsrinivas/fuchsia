// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"
#include "logging.h"
#include "ralink.h"

#include <ddk/protocol/usb.h>
#include <ddk/protocol/wlan.h>
#include <zircon/assert.h>
#include <zircon/hw/usb.h>
#include <zx/vmo.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>

#include <endian.h>
#include <inttypes.h>

#include <algorithm>
#include <cstdio>

#define RALINK_DUMP_EEPROM 0
#define RALINK_DUMP_RX 0

#define CHECK_REG(reg, op, status) \
    do { \
        if (status != ZX_OK) { \
            errorf("" #op "Register error for " #reg ": %d\n", status); \
            return status; \
        } \
    } while (0)
#define CHECK_READ(reg, status) CHECK_REG(reg, Read, status)
#define CHECK_WRITE(reg, status) CHECK_REG(reg, Write, status)

namespace {

zx_status_t sleep_for(zx_duration_t t) {
    return zx::nanosleep(zx::deadline_after(t));
}

constexpr size_t kReadReqCount = 32;
constexpr size_t kReadBufSize = 4096;
constexpr size_t kWriteReqCount = 8;
constexpr size_t kWriteBufSize = 4096;  // todo: use endpt max size

constexpr char kFirmwareFile[] = "rt2870.bin";

constexpr int kMaxBusyReads = 20;

// The <cstdlib> overloads confuse the compiler for <cstdint> types.
template <typename T>
constexpr T abs(T t) {
    return t < 0 ? -t : t;
}

uint16_t extract_tx_power(int hw_index, uint16_t txpower) {
    uint16_t val = (hw_index % 2) ? (txpower >> 8) & 0xff : txpower & 0xff;
    return fbl::clamp(val, ralink::kMinTxPower, ralink::kMaxTxPower);
}
}  // namespace

namespace ralink {

constexpr zx_duration_t Device::kDefaultBusyWait;

Device::Device(zx_device_t* device, usb_protocol_t* usb, uint8_t bulk_in,
               std::vector<uint8_t>&& bulk_out)
  : ddk::Device<Device, ddk::Unbindable>(device),
    usb_(*usb),
    rx_endpt_(bulk_in),
    tx_endpts_(std::move(bulk_out)) {
    debugf("Device dev=%p bulk_in=%u\n", parent(), rx_endpt_);

    channels_.insert({
            {1, Channel(1, 0, 241, 2, 2)},
            {2, Channel(2, 1, 241, 2, 7)},
            {3, Channel(3, 2, 242, 2, 2)},
            {4, Channel(4, 3, 242, 2, 7)},
            {5, Channel(5, 4, 243, 2, 2)},
            {6, Channel(6, 5, 243, 2, 7)},
            {7, Channel(7, 6, 244, 2, 2)},
            {8, Channel(8, 7, 244, 2, 7)},
            {9, Channel(9, 8, 245, 2, 2)},
            {10, Channel(10, 9, 245, 2, 7)},
            {11, Channel(11, 10, 246, 2, 2)},
            {12, Channel(12, 11, 246, 2, 7)},
            {13, Channel(13, 12, 247, 2, 2)},
            {14, Channel(14, 13, 248, 2, 4)},
    });
}

Device::~Device() {
    debugfn();
    for (auto txn : free_write_reqs_) {
        iotxn_release(txn);
    }
}

zx_status_t Device::Bind() {
    debugfn();

    AsicVerId avi;
    auto status = ReadRegister(&avi);
    CHECK_READ(MAC_CSR0, status);

    rt_type_ = avi.ver_id();
    rt_rev_ = avi.rev_id();
    infof("RT chipset %#x, rev %#x\n", rt_type_, rt_rev_);

    bool autorun = false;
    status = DetectAutoRun(&autorun);
    if (status != ZX_OK) {
        return status;
    }

    EfuseCtrl ec;
    status = ReadRegister(&ec);
    CHECK_READ(EFUSE_CTRL, status);

    debugf("efuse ctrl reg: %#x\n", ec.val());
    bool efuse_present = ec.sel_efuse() > 0;
    debugf("efuse present: %s\n", efuse_present ? "Y" : "N");

    status = ReadEeprom();
    if (status != ZX_OK) {
        errorf("failed to read eeprom\n");
        return status;
    }

    status = ValidateEeprom();
    if (status != ZX_OK) {
        errorf("failed to validate eeprom\n");
        return status;
    }

    for (auto& entry : channels_) {
        // The eeprom is organized into uint16_ts, but the tx power elements are 8 bits. chan_offset
        // represents the eeprom entry for the channel, and extract_tx_power will select the correct
        // bits and clamp them between kMinTxPower and kMaxTxPower.
        auto chan_offset = entry.second.hw_index >> 1;
        auto bg1_offset = EEPROM_TXPOWER_BG1 + chan_offset;
        auto bg2_offset = EEPROM_TXPOWER_BG2 + chan_offset;
        uint16_t txpower_bg1, txpower_bg2;
        status = ReadEepromField(bg1_offset, &txpower_bg1);
        CHECK_READ(EEPROM_TXPOWER_BG1, status);
        status = ReadEepromField(bg2_offset, &txpower_bg2);
        CHECK_READ(EEPROM_TXPOWER_BG2, status);

        entry.second.default_power1 = extract_tx_power(entry.second.hw_index, txpower_bg1);
        entry.second.default_power2 = extract_tx_power(entry.second.hw_index, txpower_bg2);
    }

    if (rt_type_ == RT5390) {
        status = ReadEepromField(EEPROM_CHIP_ID, &rf_type_);
        if (status != ZX_OK) {
            errorf("could not read chip id err=%d\n", status);
            return status;
        }
        infof("RF chipset %#x\n", rf_type_);
    } else {
        // TODO(tkilbourn): support other RF chipsets
        errorf("RF chipset %#x not supported!\n", rf_type_);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // TODO(tkilbourn): default antenna configs

    EepromNicConf1 enc1;
    ReadEepromField(&enc1);
    debugf("NIC CONF1=%#x\n", enc1.val());
    debugf("has HW radio? %s\n", enc1.hw_radio() ? "Y" : "N");
    debugf("has BT coexist? %s\n", enc1.bt_coexist() ? "Y" : "N");

    EepromFreq ef;
    ReadEepromField(&ef);
    debugf("freq offset=%#x\n", ef.offset());

    EepromEirpMaxTxPower eemtp;
    ReadEepromField(&eemtp);
    if (eemtp.power_2g() < kEirpMaxPower) {
        warnf("has EIRP tx power limit\n");
        warnf("TODO: limit tx power (bug NET-86)\n");
    }

    // rfkill switch
    GpioCtrl gc;
    status = ReadRegister(&gc);
    CHECK_READ(GPIO_CTRL, status);
    gc.set_gpio2_dir(1);
    status = WriteRegister(gc);
    CHECK_WRITE(GPIO_CTRL, status);

    // Add the device. The radios are not active yet though; we wait until the wlanmac start method
    // is called.
    status = DdkAdd("ralink");
    if (status != ZX_OK) {
        errorf("could not add device err=%d\n", status);
    } else {
        infof("device added\n");
    }

    // TODO(tkilbourn): if status != ZX_OK, reset the hw
    return status;
}

zx_status_t Device::ReadRegister(uint16_t offset, uint32_t* value) {
    auto ret = usb_control(&usb_, (USB_DIR_IN | USB_TYPE_VENDOR), kMultiRead, 0,
            offset, value, sizeof(*value), ZX_TIME_INFINITE);
    return ret < 0 ? ret : ZX_OK;
}

template <uint16_t A> zx_status_t Device::ReadRegister(Register<A>* reg) {
    return ReadRegister(A, reg->mut_val());
}

zx_status_t Device::WriteRegister(uint16_t offset, uint32_t value) {
    auto ret = usb_control(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite, 0,
            offset, &value, sizeof(value), ZX_TIME_INFINITE);
    return ret < 0 ? ret : ZX_OK;
}

template <uint16_t A> zx_status_t Device::WriteRegister(const Register<A>& reg) {
    return WriteRegister(A, reg.val());
}

zx_status_t Device::ReadEeprom() {
    debugfn();
    // Read 4 entries at a time
    static_assert((kEepromSize % 8) == 0, "EEPROM size must be a multiple of 8.");
    for (unsigned int i = 0; i < eeprom_.size(); i += 8) {
        EfuseCtrl ec;
        auto status = ReadRegister(&ec);
        CHECK_READ(EFUSE_CTRL, status);

        // Set the address and tell it to load the next four words. Addresses
        // must be 16-byte aligned.
        ec.set_efsrom_ain(i << 1);
        ec.set_efsrom_mode(0);
        ec.set_efsrom_kick(1);
        status = WriteRegister(ec);
        CHECK_WRITE(EFUSE_CTRL, status);

        // Wait until the registers are ready for reading.
        status = BusyWait(&ec, [&ec]() { return !ec.efsrom_kick(); });
        if (status != ZX_OK) {
            if (status == ZX_ERR_TIMED_OUT) {
                errorf("ralink busy wait for EFUSE_CTRL failed\n");
            }
            return status;
        }

        // Read the registers into the eeprom. EEPROM is read in descending
        // order, and are always return in host order but to be interpreted as
        // little endian.
        RfuseData0 rd0;
        status = ReadRegister(&rd0);
        CHECK_READ(EFUSE_DATA0, status);
        eeprom_[i] = htole32(rd0.val()) & 0xffff;
        eeprom_[i+1] = htole32(rd0.val()) >> 16;

        RfuseData1 rd1;
        status = ReadRegister(&rd1);
        CHECK_READ(EFUSE_DATA1, status);
        eeprom_[i+2] = htole32(rd1.val()) & 0xffff;
        eeprom_[i+3] = htole32(rd1.val()) >> 16;

        RfuseData2 rd2;
        status = ReadRegister(&rd2);
        CHECK_READ(EFUSE_DATA2, status);
        eeprom_[i+4] = htole32(rd2.val()) & 0xffff;
        eeprom_[i+5] = htole32(rd2.val()) >> 16;

        RfuseData3 rd3;
        status = ReadRegister(&rd3);
        CHECK_READ(EFUSE_DATA3, status);
        eeprom_[i+6] = htole32(rd3.val()) & 0xffff;
        eeprom_[i+7] = htole32(rd3.val()) >> 16;
    }

#if RALINK_DUMP_EEPROM
    std::printf("ralink: eeprom dump");
    for (size_t i = 0; i < eeprom_.size(); i++) {
        if (i % 8 == 0) {
            std::printf("\n0x%04zx: ", i);
        }
        std::printf("%04x ", eeprom_[i]);
    }
    std::printf("\n");
#endif

    return ZX_OK;
}

zx_status_t Device::ReadEepromField(uint16_t addr, uint16_t* value) {
    if (addr >= eeprom_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    *value = letoh16(eeprom_[addr]);
    return ZX_OK;
}

template <uint16_t A> zx_status_t Device::ReadEepromField(EepromField<A>* field) {
    return ReadEepromField(field->addr(), field->mut_val());
}

template <uint16_t A> zx_status_t Device::WriteEepromField(const EepromField<A>& field) {
    if (field.addr() > kEepromSize) {
        return ZX_ERR_INVALID_ARGS;
    }
    eeprom_[field.addr()] = field.val();
    return ZX_OK;
}

zx_status_t Device::ValidateEeprom() {
    debugfn();
    memcpy(mac_addr_, eeprom_.data() + EEPROM_MAC_ADDR_0, sizeof(mac_addr_));
    // TODO(tkilbourn): validate mac address
    infof("MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac_addr_[0], mac_addr_[1], mac_addr_[2], mac_addr_[3], mac_addr_[4], mac_addr_[5]);

    EepromNicConf0 enc0;
    ReadEepromField(&enc0);
    if (enc0.val() == 0xffff || enc0.val() == 0x2860 || enc0.val() == 0x2872) {
        // These values need some eeprom patching; not supported yet.
        errorf("unsupported value for EEPROM_NIC_CONF0=%#x\n", enc0.val());
        return ZX_ERR_NOT_SUPPORTED;
    }

    EepromNicConf1 enc1;
    ReadEepromField(&enc1);
    if (enc1.val() == 0xffff) {
        errorf("unsupported value for EEPROM_NIC_CONF1=%#x\n", enc1.val());
        return ZX_ERR_NOT_SUPPORTED;
    }

    EepromFreq ef;
    ReadEepromField(&ef);
    if (ef.offset() == 0x00ff) {
        ef.set_offset(0);
        WriteEepromField(ef);
        debugf("Freq: %#x\n", ef.val());
    }
    // TODO(tkilbourn): check/set LED mode

    EepromLna el;
    ReadEepromField(&el);
    auto default_lna_gain = el.a0();

    EepromRssiBg erbg;
    ReadEepromField(&erbg);
    if (abs(erbg.offset0()) > 10) {
        erbg.set_offset0(0);
    }
    if (abs(erbg.offset1()) > 10) {
        erbg.set_offset1(0);
    }
    bg_rssi_offset_[0] = erbg.offset0();
    bg_rssi_offset_[1] = erbg.offset1();
    WriteEepromField(erbg);

    EepromRssiBg2 erbg2;
    ReadEepromField(&erbg2);
    if (abs(erbg2.offset2()) > 10) {
        erbg2.set_offset2(0);
    }
    if (erbg2.lna_a1() == 0x00 || erbg2.lna_a1() == 0xff) {
        erbg2.set_lna_a1(default_lna_gain);
    }
    bg_rssi_offset_[2] = erbg2.offset2();
    WriteEepromField(erbg2);

    // TODO(tkilbourn): check and set RSSI for A

    return ZX_OK;
}

zx_status_t Device::LoadFirmware() {
    debugfn();
    zx_handle_t fw_handle;
    size_t fw_size = 0;
    auto status = load_firmware(zxdev(), kFirmwareFile, &fw_handle, &fw_size);
    if (status != ZX_OK) {
        errorf("failed to load firmware '%s': err=%d\n", kFirmwareFile, status);
        return status;
    }
    if (fw_size < 4) {
        errorf("FW: bad length (%zu)\n", fw_size);
        return ZX_ERR_BAD_STATE;
    }
    infof("opened firmware '%s' (%zd bytes)\n", kFirmwareFile, fw_size);

    zx::vmo fw(fw_handle);
    uint8_t fwversion[2];
    size_t actual = 0;
    status = fw.read(fwversion, fw_size - 4, 2, &actual);
    if (status != ZX_OK || actual != sizeof(fwversion)) {
        errorf("error reading fw version\n");
        return ZX_ERR_BAD_STATE;
    }
    infof("FW version %u.%u\n", fwversion[0], fwversion[1]);
    // Linux rt2x00 driver has more intricate size checking for different
    // chipsets. We just care that it's 8kB for ralink.
    if (fw_size != 8192) {
        errorf("FW: bad length (%zu)\n", fw_size);
        return ZX_ERR_BAD_STATE;
    }

    // TODO(tkilbourn): check crc, 4kB at a time

    AutoWakeupCfg awc;
    debugf("writing auto wakeup\n");
    status = WriteRegister(awc);
    CHECK_WRITE(AUTO_WAKEUP_CFG, status);
    debugf("auto wakeup written\n");

    // Wait for hardware to stabilize
    status = WaitForMacCsr();
    if (status != ZX_OK) {
        errorf("unstable hardware\n");
        return status;
    }
    debugf("hardware stabilized\n");

    status = DisableWpdma();
    if (status != ZX_OK) {
        return status;
    }

    bool autorun = false;
    status = DetectAutoRun(&autorun);
    if (status != ZX_OK) {
        return status;
    }
    if (autorun) {
        infof("not loading firmware, NIC is in autorun mode\n");
        return ZX_OK;
    }
    debugf("autorun not enabled\n");

    // Send the firmware to the chip. Start at offset 4096 and send 4096 bytes
    size_t offset = 4096;
    size_t remaining = fw_size - offset;
    uint8_t buf[64];
    uint16_t addr = FW_IMAGE_BASE;

    while (remaining) {
        size_t to_send = std::min(remaining, sizeof(buf));
        status = fw.read(buf, offset, to_send, &actual);
        if (status != ZX_OK || actual != to_send) {
            errorf("error reading firmware\n");
            return ZX_ERR_BAD_STATE;
        }
        status = usb_control(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite,
                             0, addr, buf, to_send, ZX_TIME_INFINITE);
        if (status < (ssize_t)to_send) {
            errorf("failed to send firmware\n");
            return ZX_ERR_BAD_STATE;
        }
        remaining -= to_send;
        offset += to_send;
        addr += to_send;
    }
    debugf("sent firmware\n");

    H2mMailboxCid hmc;
    hmc.set_val(~0);
    status = WriteRegister(hmc);
    CHECK_WRITE(H2M_MAILBOX_CID, status);

    H2mMailboxStatus hms;
    hms.set_val(~0);
    status = WriteRegister(hms);
    CHECK_WRITE(H2M_MAILBOX_STATUS, status);

    // Tell the device to load the firmware
    status = usb_control(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kDeviceMode,
                         kFirmware, 0, NULL, 0, ZX_TIME_INFINITE);
    if (status != ZX_OK) {
        errorf("failed to send load firmware command\n");
        return status;
    }
    sleep_for(ZX_MSEC(10));

    H2mMailboxCsr hmcsr;
    status = WriteRegister(hmcsr);
    CHECK_WRITE(H2M_MAILBOX_CSR, status);

    SysCtrl sc;
    status = BusyWait(&sc, [&sc]() { return sc.mcu_ready(); }, ZX_MSEC(1));
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("system MCU not ready\n");
        }
        return status;
    }

    // Disable WPDMA again
    status = DisableWpdma();
    if (status != ZX_OK) {
        return status;
    }

    // Initialize firmware and boot the MCU
    H2mBbpAgent hba;
    status = WriteRegister(hba);
    CHECK_WRITE(H2M_BBP_AGENT, status);

    status = WriteRegister(hmcsr);
    CHECK_WRITE(H2M_MAILBOX_CSR, status);

    H2mIntSrc his;
    status = WriteRegister(his);
    CHECK_WRITE(H2M_INT_SRC, status);

    status = McuCommand(MCU_BOOT_SIGNAL, 0, 0, 0);
    if (status != ZX_OK) {
        errorf("error booting MCU err=%d\n", status);
        return status;
    }
    sleep_for(ZX_MSEC(1));

    return ZX_OK;
}

zx_status_t Device::EnableRadio() {
    debugfn();

    // Wakeup the MCU
    auto status = McuCommand(MCU_WAKEUP, 0xff, 0, 2);
    if (status != ZX_OK) {
        errorf("error waking MCU err=%d\n", status);
        return status;
    }
    sleep_for(ZX_MSEC(1));

    // Wait for WPDMA to be ready
    WpdmaGloCfg wgc;
    auto wpdma_pred = [&wgc]() { return !wgc.tx_dma_busy() && !wgc.rx_dma_busy(); };
    status = BusyWait(&wgc, wpdma_pred, ZX_MSEC(10));
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("WPDMA busy\n");
        }
        return status;
    }

    // Set up USB DMA
    UsbDmaCfg udc;
    status = ReadRegister(&udc);
    CHECK_READ(USB_DMA_CFG, status);
    udc.set_phy_wd_en(0);
    udc.set_rx_agg_en(0);
    udc.set_rx_agg_to(128);
    // There appears to be a bug in the Linux driver, where an overflow is
    // setting the rx aggregation limit too low. For now, I'm using the
    // (incorrect) low value that Linux uses, but we should look into increasing
    // this.
    udc.set_rx_agg_limit(45);
    udc.set_udma_rx_en(1);
    udc.set_udma_tx_en(1);
    status = WriteRegister(udc);
    CHECK_WRITE(USB_DMA_CFG, status);

    // Wait for WPDMA again
    status = BusyWait(&wgc, wpdma_pred, ZX_MSEC(10));
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("WPDMA busy\n");
        }
        return status;
    }

    status = InitRegisters();
    if (status != ZX_OK) {
        errorf("failed to initialize registers\n");
        return status;
    }

    // Wait for MAC status ready
    MacStatusReg msr;
    status = BusyWait(&msr, [&msr]() { return !msr.tx_status() && !msr.rx_status(); }, ZX_MSEC(10));
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("BBP busy\n");
        }
        return status;
    }

    // Initialize firmware
    H2mBbpAgent hba;
    status = WriteRegister(hba);
    CHECK_WRITE(H2M_BBP_AGENT, status);

    H2mMailboxCsr hmc;
    status = WriteRegister(hmc);
    CHECK_WRITE(H2M_MAILBOX_CSR, status);

    H2mIntSrc his;
    status = WriteRegister(his);
    CHECK_WRITE(H2M_INT_SRC, status);

    status = McuCommand(MCU_BOOT_SIGNAL, 0, 0, 0);
    if (status != ZX_OK) {
        errorf("error booting MCU err=%d\n", status);
        return status;
    }
    sleep_for(ZX_MSEC(1));

    status = WaitForBbp();
    if (status != ZX_OK) {
        errorf("error waiting for BBP=%d\n", status);
        return status;
    }

    status = InitBbp();
    if (status != ZX_OK) {
        errorf("error initializing BBP=%d\n", status);
        return status;
    }

    status = InitRfcsr();
    if (status != ZX_OK) {
        errorf("error initializing RF=%d\n", status);
        return status;
    }

    // enable rx
    MacSysCtrl msc;
    status = ReadRegister(&msc);
    CHECK_READ(MAC_SYS_CTRL, status);
    msc.set_mac_tx_en(1);
    msc.set_mac_rx_en(0);
    status = WriteRegister(msc);
    CHECK_WRITE(MAC_SYS_CTRL, status);

    sleep_for(ZX_USEC(50));

    status = ReadRegister(&wgc);
    CHECK_READ(WPDMA_GLO_CFG, status);
    wgc.set_tx_dma_en(1);
    wgc.set_rx_dma_en(1);
    wgc.set_wpdma_bt_size(2);
    wgc.set_tx_wb_ddone(1);
    status = WriteRegister(wgc);
    CHECK_WRITE(WPDMA_GLO_CFG, status);

    status = ReadRegister(&msc);
    CHECK_READ(MAC_SYS_CTRL, status);
    msc.set_mac_tx_en(1);
    msc.set_mac_rx_en(1);
    status = WriteRegister(msc);
    CHECK_WRITE(MAC_SYS_CTRL, status);

    // TODO(tkilbourn): LED control stuff

    return ZX_OK;
}

zx_status_t Device::InitRegisters() {
    debugfn();

    auto status = DisableWpdma();
    if (status != ZX_OK) {
        return status;
    }

    status = WaitForMacCsr();
    if (status != ZX_OK) {
        errorf("hardware unstable\n");
        return status;
    }

    SysCtrl sc;
    status = ReadRegister(&sc);
    CHECK_READ(SYS_CTRL, status);
    sc.set_pme_oen(0);
    status = WriteRegister(sc);
    CHECK_WRITE(SYS_CTRL, status);

    MacSysCtrl msc;
    msc.set_mac_srst(1);
    msc.set_bbp_hrst(1);
    status = WriteRegister(msc);
    CHECK_WRITE(MAC_SYS_CTRL, status);

    UsbDmaCfg udc;
    status = WriteRegister(udc);
    CHECK_WRITE(USB_DMA_CFG, status);

    status = usb_control(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kDeviceMode,
            kReset, 0, NULL, 0, ZX_TIME_INFINITE);
    if (status != ZX_OK) {
        errorf("failed reset\n");
        return status;
    }

    msc.clear();
    status = WriteRegister(msc);
    CHECK_WRITE(MAC_SYS_CTRL, status);

    LegacyBasicRate lbr;
    lbr.set_rate_1mbps(1);
    lbr.set_rate_2mbps(1);
    lbr.set_rate_5_5mbps(1);
    lbr.set_rate_11mbps(1);
    lbr.set_rate_6mbps(1);
    lbr.set_rate_9mbps(1);
    lbr.set_rate_24mbps(1);
    status = WriteRegister(lbr);
    CHECK_WRITE(LEGACY_BASIC_RATE, status);

    HtBasicRate hbr;
    hbr.set_val(0x8003);
    status = WriteRegister(hbr);
    CHECK_WRITE(HT_BASIC_RATE, status);

    msc.clear();
    status = WriteRegister(msc);
    CHECK_WRITE(MAC_SYS_CTRL, status);

    BcnTimeCfg btc;
    status = ReadRegister(&btc);
    CHECK_READ(BCN_TIME_CFG, status);
    btc.set_bcn_intval(1600);
    btc.set_tsf_timer_en(0);
    btc.set_tsf_sync_mode(0);
    btc.set_tbtt_timer_en(0);
    btc.set_bcn_tx_en(0);
    btc.set_tsf_ins_comp(0);
    status = WriteRegister(btc);
    CHECK_WRITE(BCN_TIME_CFG, status);

    status = SetRxFilter();
    if (status != ZX_OK) {
        return status;
    }

    BkoffSlotCfg bsc;
    status = ReadRegister(&bsc);
    CHECK_READ(BKOFF_SLOT_CFG, status);
    bsc.set_slot_time(9);
    bsc.set_cc_delay_time(2);
    status = WriteRegister(bsc);
    CHECK_WRITE(BKOFF_SLOT_CFG, status);

    TxSwCfg0 tswc0;
    // TX_SW_CFG register values come from Linux kernel driver
    tswc0.set_dly_txpe_en(0x04);
    tswc0.set_dly_pape_en(0x04);
    // All other TX_SW_CFG0 values are 0 (set by using 0 as starting value)
    status = WriteRegister(tswc0);
    CHECK_WRITE(TX_SW_CFG0, status);

    TxSwCfg1 tswc1;
    tswc1.set_dly_pape_dis(0x06);
    tswc1.set_dly_trsw_dis(0x06);
    tswc1.set_dly_rftr_dis(0x08);
    status = WriteRegister(tswc1);
    CHECK_WRITE(TX_SW_CFG1, status);

    TxSwCfg2 tswc2;
    // All bits set to zero.
    status = WriteRegister(tswc2);
    CHECK_WRITE(TX_SW_CFG2, status);

    TxLinkCfg tlc;
    status = ReadRegister(&tlc);
    CHECK_READ(TX_LINK_CFG, status);
    tlc.set_remote_mfb_lifetime(32);
    tlc.set_tx_mfb_en(0);
    tlc.set_remote_umfs_en(0);
    tlc.set_tx_mrq_en(0);
    tlc.set_tx_rdg_en(0);
    tlc.set_tx_cfack_en(1);
    tlc.set_remote_mfb(0);
    tlc.set_remote_mfs(0);
    status = WriteRegister(tlc);
    CHECK_WRITE(TX_LINK_CFG, status);

    TxTimeoutCfg ttc;
    status = ReadRegister(&ttc);
    CHECK_READ(TX_TIMEOUT_CFG, status);
    ttc.set_mpdu_life_time(9);
    ttc.set_rx_ack_timeout(32);
    ttc.set_txop_timeout(10);
    status = WriteRegister(ttc);
    CHECK_WRITE(TX_TIMEOUT_CFG, status);

    MaxLenCfg mlc;
    status = ReadRegister(&mlc);
    CHECK_READ(MAX_LEN_CFG, status);
    mlc.set_max_mpdu_len(3840);
    mlc.set_max_psdu_len(1);
    mlc.set_min_psdu_len(0);
    mlc.set_min_mpdu_len(0);
    status = WriteRegister(mlc);
    CHECK_WRITE(MAX_LEN_CFG, status);

    LedCfg lc;
    status = ReadRegister(&lc);
    CHECK_READ(LED_CFG, status);
    lc.set_led_on_time(70);
    lc.set_led_off_time(30);
    lc.set_slow_blk_time(3);
    lc.set_r_led_mode(3);
    lc.set_g_led_mode(3);
    lc.set_y_led_mode(3);
    lc.set_led_pol(1);
    status = WriteRegister(lc);
    CHECK_WRITE(LED_CFG, status);

    MaxPcnt mp;
    mp.set_max_rx0q_pcnt(0x9f);
    mp.set_max_tx2q_pcnt(0xbf);
    mp.set_max_tx1q_pcnt(0x3f);
    mp.set_max_tx0q_pcnt(0x1f);
    status = WriteRegister(mp);
    CHECK_WRITE(MAX_PCNT, status);

    TxRtyCfg trc;
    status = ReadRegister(&trc);
    CHECK_READ(TX_RTY_CFG, status);
    trc.set_short_rty_limit(15);
    trc.set_long_rty_limit(31);
    trc.set_long_rty_thres(2000);
    trc.set_nag_rty_mode(0);
    trc.set_agg_rty_mode(0);
    trc.set_tx_autofb_en(1);
    status = WriteRegister(trc);
    CHECK_WRITE(TX_RTY_CFG, status);

    AutoRspCfg arc;
    status = ReadRegister(&arc);
    CHECK_READ(AUTO_RSP_CFG, status);
    arc.set_auto_rsp_en(1);
    arc.set_bac_ackpolicy_en(1);
    arc.set_cts_40m_mode(0);
    arc.set_cts_40m_ref(0);
    arc.set_cck_short_en(1);
    arc.set_bac_ack_policy(0);
    arc.set_ctrl_pwr_bit(0);
    status = WriteRegister(arc);
    CHECK_WRITE(AUTO_RSP_CFG, status);

    CckProtCfg cpc;
    status = ReadRegister(&cpc);
    CHECK_READ(CCK_PROT_CFG, status);
    cpc.set_prot_rate(3);
    cpc.set_prot_ctrl(0);
    cpc.set_prot_nav(1);
    cpc.set_txop_allow_cck_tx(1);
    cpc.set_txop_allow_ofdm_tx(1);
    cpc.set_txop_allow_mm20_tx(1);
    cpc.set_txop_allow_mm40_tx(0);
    cpc.set_txop_allow_gf20_tx(1);
    cpc.set_txop_allow_gf40_tx(0);
    cpc.set_rtsth_en(1);
    status = WriteRegister(cpc);
    CHECK_WRITE(CCK_PROT_CFG, status);

    OfdmProtCfg opc;
    status = ReadRegister(&opc);
    CHECK_READ(OFDM_PROT_CFG, status);
    opc.set_prot_rate(3);
    opc.set_prot_ctrl(0);
    opc.set_prot_nav(1);
    opc.set_txop_allow_cck_tx(1);
    opc.set_txop_allow_ofdm_tx(1);
    opc.set_txop_allow_mm20_tx(1);
    opc.set_txop_allow_mm40_tx(0);
    opc.set_txop_allow_gf20_tx(1);
    opc.set_txop_allow_gf40_tx(0);
    opc.set_rtsth_en(1);
    status = WriteRegister(opc);
    CHECK_WRITE(OFDM_PROT_CFG, status);

    Mm20ProtCfg mm20pc;
    status = ReadRegister(&mm20pc);
    CHECK_READ(MM20_PROT_CFG, status);
    mm20pc.set_prot_rate(0x4004);
    mm20pc.set_prot_ctrl(0);
    mm20pc.set_prot_nav(1);
    mm20pc.set_txop_allow_cck_tx(1);
    mm20pc.set_txop_allow_ofdm_tx(1);
    mm20pc.set_txop_allow_mm20_tx(1);
    mm20pc.set_txop_allow_mm40_tx(0);
    mm20pc.set_txop_allow_gf20_tx(1);
    mm20pc.set_txop_allow_gf40_tx(0);
    mm20pc.set_rtsth_en(0);
    status = WriteRegister(mm20pc);
    CHECK_WRITE(MM20_PROT_CFG, status);

    Mm40ProtCfg mm40pc;
    status = ReadRegister(&mm40pc);
    CHECK_READ(MM40_PROT_CFG, status);
    mm40pc.set_prot_rate(0x4084);
    mm40pc.set_prot_ctrl(0);
    mm40pc.set_prot_nav(1);
    mm40pc.set_txop_allow_cck_tx(1);
    mm40pc.set_txop_allow_ofdm_tx(1);
    mm40pc.set_txop_allow_mm20_tx(1);
    mm40pc.set_txop_allow_mm40_tx(1);
    mm40pc.set_txop_allow_gf20_tx(1);
    mm40pc.set_txop_allow_gf40_tx(1);
    mm40pc.set_rtsth_en(0);
    status = WriteRegister(mm40pc);
    CHECK_WRITE(MM40_PROT_CFG, status);

    Gf20ProtCfg gf20pc;
    status = ReadRegister(&gf20pc);
    CHECK_READ(GF20_PROT_CFG, status);
    gf20pc.set_prot_rate(0x4004);
    gf20pc.set_prot_ctrl(0);
    gf20pc.set_prot_nav(1);
    gf20pc.set_txop_allow_cck_tx(1);
    gf20pc.set_txop_allow_ofdm_tx(1);
    gf20pc.set_txop_allow_mm20_tx(1);
    gf20pc.set_txop_allow_mm40_tx(0);
    gf20pc.set_txop_allow_gf20_tx(1);
    gf20pc.set_txop_allow_gf40_tx(0);
    gf20pc.set_rtsth_en(0);
    status = WriteRegister(gf20pc);
    CHECK_WRITE(GF20_PROT_CFG, status);

    Gf40ProtCfg gf40pc;
    status = ReadRegister(&gf40pc);
    CHECK_READ(GF40_PROT_CFG, status);
    gf40pc.set_prot_rate(0x4084);
    gf40pc.set_prot_ctrl(0);
    gf40pc.set_prot_nav(1);
    gf40pc.set_txop_allow_cck_tx(1);
    gf40pc.set_txop_allow_ofdm_tx(1);
    gf40pc.set_txop_allow_mm20_tx(1);
    gf40pc.set_txop_allow_mm40_tx(1);
    gf40pc.set_txop_allow_gf20_tx(1);
    gf40pc.set_txop_allow_gf40_tx(1);
    gf40pc.set_rtsth_en(0);
    status = WriteRegister(gf40pc);
    CHECK_WRITE(GF40_PROT_CFG, status);

    PbfCfg pc;
    pc.set_rx0q_en(1);
    pc.set_tx2q_en(1);
    pc.set_tx2q_num(20);
    pc.set_tx1q_num(7);
    status = WriteRegister(pc);
    CHECK_WRITE(PBF_CFG, status);

    WpdmaGloCfg wgc;
    status = ReadRegister(&wgc);
    CHECK_READ(WPDMA_GLO_CFG, status);
    wgc.set_tx_dma_en(0);
    wgc.set_tx_dma_busy(0);
    wgc.set_rx_dma_en(0);
    wgc.set_rx_dma_busy(0);
    wgc.set_wpdma_bt_size(3);
    wgc.set_tx_wb_ddone(0);
    wgc.set_big_endian(0);
    wgc.set_hdr_seg_len(0);
    status = WriteRegister(wgc);
    CHECK_WRITE(WPDMA_GLO_CFG, status);

    TxopCtrlCfg tcc;
    status = ReadRegister(&tcc);
    CHECK_READ(TXOP_CTRL_CFG, status);
    tcc.set_txop_trun_en(0x3f);
    tcc.set_lsig_txop_en(0);
    tcc.set_ext_cca_en(0);
    tcc.set_ext_cca_dly(88);
    tcc.set_ext_cw_min(0);
    status = WriteRegister(tcc);
    CHECK_WRITE(TXOP_CTRL_CFG, status);

    TxopHldrEt the;
    the.set_tx40m_blk_en(1);
    status = WriteRegister(the);
    CHECK_WRITE(TXOP_HLDR_ET, status);

    TxRtsCfg txrtscfg;
    status = ReadRegister(&txrtscfg);
    CHECK_READ(TX_RTS_CFG, status);
    txrtscfg.set_rts_rty_limit(32);
    txrtscfg.set_rts_thres(2353);  // IEEE80211_MAX_RTS_THRESHOLD in Linux
    txrtscfg.set_rts_fbk_en(0);
    status = WriteRegister(txrtscfg);
    CHECK_WRITE(TX_RTS_CFG, status);

    ExpAckTime eat;
    eat.set_exp_cck_ack_time(0x00ca);
    eat.set_exp_ofdm_ack_time(0x0024);
    status = WriteRegister(eat);
    CHECK_WRITE(EXP_ACK_TIME, status);

    XifsTimeCfg xtc;
    status = ReadRegister(&xtc);
    CHECK_READ(XIFS_TIME_CFG, status);
    xtc.set_cck_sifs_time(16);
    xtc.set_ofdm_sifs_time(16);
    xtc.set_ofdm_xifs_time(4);
    xtc.set_eifs_time(314);
    xtc.set_bb_rxend_en(1);
    status = WriteRegister(xtc);
    CHECK_WRITE(XIFS_TIME_CFG, status);

    PwrPinCfg ppc;
    ppc.set_io_rf_pe(1);
    ppc.set_io_ra_pe(1);
    status = WriteRegister(ppc);
    CHECK_WRITE(PWR_PIN_CFG, status);

    for (int i = 0; i < 4; i++) {
        status = WriteRegister(SHARED_KEY_MODE_BASE + i * sizeof(uint32_t), 0);
        CHECK_WRITE(SHARED_KEY_MODE, status);
    }

    RxWcidEntry rwe;
    memset(&rwe.mac, 0xff, sizeof(rwe.mac));
    memset(&rwe.ba_sess_mask, 0xff, sizeof(rwe.ba_sess_mask));
    for (int i = 0; i < 256; i++) {
        uint16_t addr = RX_WCID_BASE + i * sizeof(rwe);
        status = usb_control(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite,
                             0, addr, &rwe, sizeof(rwe), ZX_TIME_INFINITE);
        if (status < (ssize_t)sizeof(rwe)) {
            errorf("failed to set RX WCID search entry\n");
            return ZX_ERR_BAD_STATE;
        }

        status = WriteRegister(WCID_ATTR_BASE + i * sizeof(uint32_t), 0);
        CHECK_WRITE(WCID_ATTR, status);

        status = WriteRegister(IV_EIV_BASE + i * 8, 0);
        CHECK_WRITE(IV_EIV, status);
    }

    // TODO(tkilbourn): Clear beacons ?????? (probably not needed as long as we are only STA)

    UsCycCnt ucc;
    status = ReadRegister(&ucc);
    CHECK_READ(US_CYC_CNT, status);
    ucc.set_us_cyc_count(30);
    status = WriteRegister(ucc);
    CHECK_WRITE(US_CYC_CNT, status);

    HtFbkCfg0 hfc0;
    status = ReadRegister(&hfc0);
    CHECK_READ(HT_FBK_CFG0, status);
    hfc0.set_ht_mcs0_fbk(0);
    hfc0.set_ht_mcs1_fbk(0);
    hfc0.set_ht_mcs2_fbk(1);
    hfc0.set_ht_mcs3_fbk(2);
    hfc0.set_ht_mcs4_fbk(3);
    hfc0.set_ht_mcs5_fbk(4);
    hfc0.set_ht_mcs6_fbk(5);
    hfc0.set_ht_mcs7_fbk(6);
    status = WriteRegister(hfc0);
    CHECK_WRITE(HT_FBK_CFG0, status);

    HtFbkCfg1 hfc1;
    status = ReadRegister(&hfc1);
    CHECK_READ(HT_FBK_CFG1, status);
    hfc1.set_ht_mcs8_fbk(8);
    hfc1.set_ht_mcs9_fbk(8);
    hfc1.set_ht_mcs10_fbk(9);
    hfc1.set_ht_mcs11_fbk(10);
    hfc1.set_ht_mcs12_fbk(11);
    hfc1.set_ht_mcs13_fbk(12);
    hfc1.set_ht_mcs14_fbk(13);
    hfc1.set_ht_mcs15_fbk(14);
    status = WriteRegister(hfc1);
    CHECK_WRITE(HT_FBK_CFG1, status);

    LgFbkCfg0 lfc0;
    status = ReadRegister(&lfc0);
    CHECK_READ(LG_FBK_CFG0, status);
    lfc0.set_ofdm0_fbk(8);
    lfc0.set_ofdm1_fbk(8);
    lfc0.set_ofdm2_fbk(9);
    lfc0.set_ofdm3_fbk(10);
    lfc0.set_ofdm4_fbk(11);
    lfc0.set_ofdm5_fbk(12);
    lfc0.set_ofdm6_fbk(13);
    lfc0.set_ofdm7_fbk(14);
    status = WriteRegister(lfc0);
    CHECK_WRITE(LG_FBK_CFG0, status);

    LgFbkCfg1 lfc1;
    status = ReadRegister(&lfc1);
    CHECK_READ(LG_FBK_CFG1, status);
    lfc1.set_cck0_fbk(0);
    lfc1.set_cck1_fbk(0);
    lfc1.set_cck2_fbk(1);
    lfc1.set_cck3_fbk(2);
    status = WriteRegister(lfc1);
    CHECK_WRITE(LG_FBK_CFG1, status);

    // Linux does not force BA window sizes.
    ForceBaWinsize fbw;
    status = ReadRegister(&fbw);
    CHECK_READ(FORCE_BA_WINSIZE, status);
    fbw.set_force_ba_winsize(0);
    fbw.set_force_ba_winsize_en(0);
    status = WriteRegister(fbw);
    CHECK_WRITE(FORCE_BA_WINSIZE, status);

    // Reading the stats counters will clear them. We don't need to look at the
    // values.
    RxStaCnt0 rsc0;
    ReadRegister(&rsc0);
    RxStaCnt1 rsc1;
    ReadRegister(&rsc1);
    RxStaCnt2 rsc2;
    ReadRegister(&rsc2);
    TxStaCnt0 tsc0;
    ReadRegister(&tsc0);
    TxStaCnt1 tsc1;
    ReadRegister(&tsc1);
    TxStaCnt2 tsc2;
    ReadRegister(&tsc2);

    IntTimerCfg itc;
    status = ReadRegister(&itc);
    CHECK_READ(INT_TIMER_CFG, status);
    itc.set_pre_tbtt_timer(6 << 4);
    status = WriteRegister(itc);
    CHECK_WRITE(INT_TIMER_CFG, status);

    ChTimeCfg ctc;
    status = ReadRegister(&ctc);
    CHECK_READ(CH_TIME_CFG, status);
    ctc.set_ch_sta_timer_en(1);
    ctc.set_tx_as_ch_busy(1);
    ctc.set_rx_as_ch_busy(1);
    ctc.set_nav_as_ch_busy(1);
    ctc.set_eifs_as_ch_busy(1);
    status = WriteRegister(ctc);
    CHECK_WRITE(CH_TIME_CFG, status);

    return ZX_OK;
}

namespace {
const struct {
    uint8_t addr;
    uint8_t val;
} BBP_REGS[] = {
    { 31,  0x08 },
    { 65,  0x2c },
    { 66,  0x38 },
    { 68,  0x0b },
    { 69,  0x12 },
    { 73,  0x13 },
    { 75,  0x46 },
    { 76,  0x28 },
    { 77,  0x59 },
    { 70,  0x0a },
    { 79,  0x13 },
    { 80,  0x05 },
    { 81,  0x33 },
    { 82,  0x62 },
    { 83,  0x7a },
    { 84,  0x9a },
    { 86,  0x38 },
    { 91,  0x04 },
    { 92,  0x02 },
    { 103, 0xc0 },
    { 104, 0x92 },
    { 105, 0x3c },
    { 106, 0x03 },
    { 128, 0x12 },
};
}  // namespace

zx_status_t Device::InitBbp() {
    debugfn();

    Bbp4 reg;
    auto status = ReadBbp(&reg);
    CHECK_READ(BBP4, status);
    reg.set_mac_if_ctrl(1);
    status = WriteBbp(reg);
    CHECK_WRITE(BBP4, status);

    for (const auto& breg : BBP_REGS) {
        status = WriteBbp(breg.addr, breg.val);
        if (status != ZX_OK) {
            errorf("WriteRegister error for BBP reg %u: %d\n", breg.addr, status);
            return status;
        }
    }

    // disable unused dac/adc
    Bbp138 bbp138;
    status = ReadBbp(&bbp138);
    CHECK_READ(BBP138, status);
    EepromNicConf0 enc0;
    status = ReadEepromField(&enc0);
    CHECK_READ(EEPROM_NIC_CONF0, status);
    if (enc0.txpath() == 1) {
        bbp138.set_tx_dac1(1);
    }
    if (enc0.rxpath() == 1) {
        bbp138.set_rx_adc1(0);
    }
    status = WriteBbp(bbp138);
    CHECK_WRITE(BBP138, status);

    EepromNicConf1 enc1;
    ReadEepromField(&enc1);

    // TODO(tkilbourn): check for bt coexist (don't need this yet)

    // Use hardware antenna diversity for these chips
    if (rt_rev_ >= REV_RT5390R) {
        status = WriteBbp(BbpRegister<150>(0x00));
        CHECK_WRITE(BBP150, status);
        status = WriteBbp(BbpRegister<151>(0x00));
        CHECK_WRITE(BBP151, status);
        status = WriteBbp(BbpRegister<154>(0x00));
        CHECK_WRITE(BBP154, status);
    }

    Bbp152 bbp152;
    status = ReadBbp(&bbp152);
    CHECK_READ(BBP152, status);
    bbp152.set_rx_default_ant(enc1.ant_diversity() ? 0 : 1);
    status = WriteBbp(bbp152);
    CHECK_WRITE(BBP152, status);

    // frequency calibration
    status = WriteBbp(BbpRegister<142>(0x01));
    CHECK_WRITE(BBP142, status);
    status = WriteBbp(BbpRegister<143>(0x39));
    CHECK_WRITE(BBP143, status);

    for (size_t index = 0; index < EEPROM_BBP_SIZE; index++) {
        uint16_t val;
        status = ReadEepromField(EEPROM_BBP_START + index, &val);
        CHECK_READ(EEPROM_BBP, status);
        if (val != 0xffff && val != 0x0000) {
            status = WriteBbp(val >> 8, val & 0xff);
            if (status != ZX_OK) {
                errorf("WriteRegister error for BBP reg %u: %d\n", val >> 8, status);
                return status;
            }
        }
    }
    return ZX_OK;
}

namespace {
const struct {
    uint8_t addr;
    uint8_t val;
    bool    has_alt;  // should rt_rev_ be checked
    uint8_t alt_val;  // set if rt_rev_ >= REV_RT5390F
} RFCSR_REGS[] = {
#define RFCSR_REG(n,v)      { n, v, false, 0x00 }
#define RFCSR_ALTREG(n,v,a) { n, v, true,  a }
    RFCSR_REG(1, 0x0f),
    RFCSR_REG(2, 0x80),
    RFCSR_REG(3, 0x88),
    RFCSR_REG(5, 0x10),
    RFCSR_ALTREG(6, 0xa0, 0xe0),
    RFCSR_REG(7, 0x00),
    RFCSR_REG(10, 0x53),
    RFCSR_REG(11, 0x4a),
    RFCSR_REG(12, 0x46),
    RFCSR_REG(13, 0x9f),
    RFCSR_REG(14, 0x00),
    RFCSR_REG(15, 0x00),
    RFCSR_REG(16, 0x00),
    RFCSR_REG(18, 0x03),
    RFCSR_REG(19, 0x00),
    RFCSR_REG(20, 0x00),
    RFCSR_REG(21, 0x00),
    RFCSR_REG(22, 0x20),
    RFCSR_REG(23, 0x00),
    RFCSR_REG(24, 0x00),
    RFCSR_ALTREG(25, 0xc0, 0x80),
    RFCSR_REG(26, 0x00),
    RFCSR_REG(27, 0x09),
    RFCSR_REG(28, 0x00),
    RFCSR_REG(29, 0x10),
    RFCSR_REG(30, 0x10),
    RFCSR_REG(31, 0x80),
    RFCSR_REG(32, 0x80),
    RFCSR_REG(33, 0x00),
    RFCSR_REG(34, 0x07),
    RFCSR_REG(35, 0x12),
    RFCSR_REG(36, 0x00),
    RFCSR_REG(37, 0x08),
    RFCSR_REG(38, 0x85),
    RFCSR_REG(39, 0x1b),
    RFCSR_REG(40, 0x0b),
    RFCSR_REG(41, 0xbb),
    RFCSR_REG(42, 0xd2),
    RFCSR_REG(43, 0x9a),
    RFCSR_REG(44, 0x0e),
    RFCSR_REG(45, 0xa2),
    RFCSR_ALTREG(46, 0x7b, 0x73),
    RFCSR_REG(47, 0x00),
    RFCSR_REG(48, 0x10),
    RFCSR_REG(49, 0x94),
    RFCSR_REG(52, 0x38),
    RFCSR_ALTREG(53, 0x84, 0x00),
    RFCSR_REG(54, 0x78),
    RFCSR_REG(55, 0x44),
    RFCSR_ALTREG(56, 0x22, 0x42),
    RFCSR_REG(57, 0x80),
    RFCSR_REG(58, 0x7f),
    RFCSR_REG(59, 0x8f),
    RFCSR_REG(60, 0x45),
    RFCSR_ALTREG(61, 0xdd, 0xd1),
    RFCSR_REG(62, 0x00),
    RFCSR_REG(63, 0x00),
#undef RFCSR_REG
#undef RFCSR_ALTREG
};
}  // namespace

zx_status_t Device::InitRfcsr() {
    debugfn();

    // Init calibration
    Rfcsr2 r2;
    auto status = ReadRfcsr(&r2);
    CHECK_READ(RF2, status);

    r2.set_rescal_en(1);
    status = WriteRfcsr(r2);
    CHECK_WRITE(RF2, status);

    sleep_for(ZX_MSEC(1));
    r2.set_rescal_en(0);
    status = WriteRfcsr(r2);
    CHECK_WRITE(RF2, status);

    for (const auto& entry : RFCSR_REGS) {
        if (entry.has_alt) {
            if (rt_rev_ >= REV_RT5390F) {
                status = WriteRfcsr(entry.addr, entry.alt_val);
            } else {
                status = WriteRfcsr(entry.addr, entry.val);
            }
        } else {
            status = WriteRfcsr(entry.addr, entry.val);
        }
        if (status != ZX_OK) {
            errorf("WriteRegister error for RFCSR %u: %d\n", entry.addr, status);
            return status;
        }
    }

    status = NormalModeSetup();
    if (status != ZX_OK) {
        return status;
    }

    // TODO(tkilbourn): led open drain enable ??? (doesn't appear in vendor driver?)

    return ZX_OK;
}

zx_status_t Device::McuCommand(uint8_t command, uint8_t token, uint8_t arg0, uint8_t arg1) {
    debugf("McuCommand %u\n", command);
    H2mMailboxCsr hmc;
    auto status = BusyWait(&hmc, [&hmc]() { return !hmc.owner(); });
    if (status != ZX_OK) {
        return status;
    }

    hmc.set_owner(1);
    hmc.set_cmd_token(token);
    hmc.set_arg0(arg0);
    hmc.set_arg1(arg1);
    status = WriteRegister(hmc);
    CHECK_WRITE(H2M_MAILBOX_CSR, status);

    HostCmd hc;
    hc.set_command(command);
    status = WriteRegister(hc);
    CHECK_WRITE(HOST_CMD, status);
    sleep_for(ZX_MSEC(1));

    return status;
}

zx_status_t Device::ReadBbp(uint8_t addr, uint8_t* val) {
    BbpCsrCfg bcc;
    auto pred = [&bcc]() { return !bcc.bbp_csr_kick(); };

    auto status = BusyWait(&bcc, pred);
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("timed out waiting for BBP\n");
        }
        return status;
    }

    bcc.clear();
    bcc.set_bbp_addr(addr);
    bcc.set_bbp_csr_rw(1);
    bcc.set_bbp_csr_kick(1);
    bcc.set_bbp_rw_mode(1);
    status = WriteRegister(bcc);
    CHECK_WRITE(BBP_CSR_CFG, status);

    status = BusyWait(&bcc, pred);
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("timed out waiting for BBP\n");
            *val = 0xff;
        }
        return status;
    }

    *val = bcc.bbp_data();
    return ZX_OK;
}

template <uint8_t A> zx_status_t Device::ReadBbp(BbpRegister<A>* reg) {
    return ReadBbp(reg->addr(), reg->mut_val());
}

zx_status_t Device::WriteBbp(uint8_t addr, uint8_t val) {
    BbpCsrCfg bcc;
    auto status = BusyWait(&bcc, [&bcc]() { return !bcc.bbp_csr_kick(); });
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("timed out waiting for BBP\n");
        }
        return status;
    }

    bcc.clear();
    bcc.set_bbp_data(val);
    bcc.set_bbp_addr(addr);
    bcc.set_bbp_csr_rw(0);
    bcc.set_bbp_csr_kick(1);
    bcc.set_bbp_rw_mode(1);
    status = WriteRegister(bcc);
    CHECK_WRITE(BBP_CSR_CFG, status);
    return status;
}

template <uint8_t A> zx_status_t Device::WriteBbp(const BbpRegister<A>& reg) {
    return WriteBbp(reg.addr(), reg.val());
}

zx_status_t Device::WaitForBbp() {
    H2mBbpAgent hba;
    auto status = WriteRegister(hba);
    CHECK_WRITE(H2M_BBP_AGENT, status);

    H2mMailboxCsr hmc;
    status = WriteRegister(hmc);
    CHECK_WRITE(H2M_MAILBOX_CSR, status);
    sleep_for(ZX_MSEC(1));

    uint8_t val;
    for (unsigned int i = 0; i < kMaxBusyReads; i++) {
        status = ReadBbp(0, &val);
        CHECK_READ(BBP0, status);
        if ((val != 0xff) && (val != 0x00)) {
            return ZX_OK;
        }
        sleep_for(kDefaultBusyWait);
    }
    errorf("timed out waiting for BBP ready\n");
    return ZX_ERR_TIMED_OUT;
}

zx_status_t Device::ReadRfcsr(uint8_t addr, uint8_t* val) {
    RfCsrCfg rcc;
    auto pred = [&rcc]() { return !rcc.rf_csr_kick(); };

    auto status = BusyWait(&rcc, pred);
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("timed out waiting for RFCSR\n");
        }
        return status;
    }

    rcc.clear();
    rcc.set_rf_csr_addr(addr);
    rcc.set_rf_csr_rw(0);
    rcc.set_rf_csr_kick(1);
    status = WriteRegister(rcc);
    CHECK_WRITE(RF_CSR_CFG, status);

    status = BusyWait(&rcc, pred);
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("timed out waiting for RFCSR\n");
            *val = 0xff;
        }
        return status;
    }

    *val = rcc.rf_csr_data();
    return ZX_OK;
}

template <uint8_t A> zx_status_t Device::ReadRfcsr(RfcsrRegister<A>* reg) {
    return ReadRfcsr(reg->addr(), reg->mut_val());
}

zx_status_t Device::WriteRfcsr(uint8_t addr, uint8_t val) {
    RfCsrCfg rcc;
    auto status = BusyWait(&rcc, [&rcc]() { return !rcc.rf_csr_kick(); });
    if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
            errorf("timed out waiting for RFCSR\n");
        }
        return status;
    }

    rcc.clear();
    rcc.set_rf_csr_data(val);
    rcc.set_rf_csr_addr(addr);
    rcc.set_rf_csr_rw(1);
    rcc.set_rf_csr_kick(1);
    status = WriteRegister(rcc);
    CHECK_WRITE(RF_CSR_CFG, status);
    return status;
}

template <uint8_t A> zx_status_t Device::WriteRfcsr(const RfcsrRegister<A>& reg) {
    return WriteRfcsr(reg.addr(), reg.val());
}

zx_status_t Device::DisableWpdma() {
    WpdmaGloCfg wgc;
    auto status = ReadRegister(&wgc);
    CHECK_READ(WPDMA_GLO_CFG, status);
    wgc.set_tx_dma_en(0);
    wgc.set_tx_dma_busy(0);
    wgc.set_rx_dma_en(0);
    wgc.set_rx_dma_busy(0);
    wgc.set_tx_wb_ddone(1);
    status = WriteRegister(wgc);
    CHECK_WRITE(WPDMA_GLO_CFG, status);
    debugf("disabled WPDMA\n");
    return ZX_OK;
}

zx_status_t Device::DetectAutoRun(bool* autorun) {
    uint32_t fw_mode = 0;
    zx_status_t status = usb_control(&usb_, (USB_DIR_IN | USB_TYPE_VENDOR),
            kDeviceMode, kAutorun, 0, &fw_mode, sizeof(fw_mode), ZX_TIME_INFINITE);
    if (status < 0) {
        errorf("DeviceMode error: %d\n", status);
        return status;
    }

    fw_mode = letoh32(fw_mode);
    if ((fw_mode & 0x03) == 2) {
        debugf("AUTORUN\n");
        *autorun = true;
    } else {
        *autorun = false;
    }
    return ZX_OK;
}

zx_status_t Device::WaitForMacCsr() {
    AsicVerId avi;
    return BusyWait(&avi, [&avi]() { return avi.val() && avi.val() != ~0u; }, ZX_MSEC(1));
}

zx_status_t Device::SetRxFilter() {
    RxFiltrCfg rfc;
    auto status = ReadRegister(&rfc);
    CHECK_READ(RX_FILTR_CFG, status);
    rfc.set_drop_crc_err(1);
    rfc.set_drop_phy_err(1);
    rfc.set_drop_uc_nome(1);
    rfc.set_drop_not_mybss(0);
    rfc.set_drop_ver_err(1);
    rfc.set_drop_mc(0);
    rfc.set_drop_bc(0);
    rfc.set_drop_dupl(1);
    rfc.set_drop_cfack(1);
    rfc.set_drop_cfend(1);
    rfc.set_drop_ack(1);
    rfc.set_drop_cts(1);
    rfc.set_drop_rts(1);
    rfc.set_drop_pspoll(1);
    rfc.set_drop_ba(0);
    rfc.set_drop_bar(1);
    rfc.set_drop_ctrl_rsv(1);
    status = WriteRegister(rfc);
    CHECK_WRITE(RX_FILTR_CFG, status);

    return ZX_OK;
}

zx_status_t Device::NormalModeSetup() {
    debugfn();

    Bbp138 bbp138;
    auto status = ReadBbp(&bbp138);
    CHECK_READ(BBP138, status);

    EepromNicConf0 enc0;
    status = ReadEepromField(&enc0);
    CHECK_READ(EEPROM_NIC_CONF0, status);

    if (enc0.rxpath()) {
        bbp138.set_rx_adc1(0);
    }
    if (enc0.txpath()) {
        bbp138.set_tx_dac1(1);
    }
    status = WriteBbp(bbp138);
    CHECK_WRITE(BBP138, status);

    Rfcsr38 r38;
    status = ReadRfcsr(&r38);
    CHECK_READ(RF38, status);
    r38.set_rx_lo1_en(0);
    status = WriteRfcsr(r38);
    CHECK_WRITE(RF38, status);

    Rfcsr39 r39;
    status = ReadRfcsr(&r39);
    CHECK_READ(RF39, status);
    r39.set_rx_lo2_en(0);
    status = WriteRfcsr(r39);
    CHECK_WRITE(RF39, status);

    Bbp4 bbp4;
    status = ReadBbp(&bbp4);
    CHECK_READ(BBP4, status);
    bbp4.set_mac_if_ctrl(1);
    status = WriteBbp(bbp4);
    CHECK_WRITE(BBP4, status);

    Rfcsr30 r30;
    status = ReadRfcsr(&r30);
    CHECK_READ(RF30, status);
    r30.set_rx_vcm(2);
    status = WriteRfcsr(r30);
    CHECK_WRITE(RF30, status);

    return ZX_OK;
}

zx_status_t Device::StartQueues() {
    debugfn();

    // RX queue
    MacSysCtrl msc;
    auto status = ReadRegister(&msc);
    CHECK_READ(MAC_SYS_CTRL, status);
    msc.set_mac_rx_en(1);
    status = WriteRegister(msc);
    CHECK_WRITE(MAC_SYS_CTRL, status);

    // Beacon queue  --  maybe this isn't started here
    //BcnTimeCfg btc;
    //status = ReadRegister(&btc);
    //CHECK_READ(BCN_TIME_CFG, status);
    //btc.set_tsf_timer_en(1);
    //btc.set_tbtt_timer_en(1);
    //btc.set_bcn_tx_en(1);
    //status = WriteRegister(btc);
    //CHECK_WRITE(BCN_TIME_CFG, status);

    // kick the rx queue???

    return ZX_OK;
}

zx_status_t Device::StopRxQueue() {
    MacSysCtrl msc;
    auto status = ReadRegister(&msc);
    CHECK_READ(MAC_SYS_CTRL, status);
    msc.set_mac_rx_en(0);
    status = WriteRegister(msc);
    CHECK_WRITE(MAC_SYS_CTRL, status);

    return ZX_OK;
}

zx_status_t Device::SetupInterface() {
    BcnTimeCfg btc;
    auto status = ReadRegister(&btc);
    CHECK_READ(BCN_TIME_CFG, status);
    btc.set_tsf_sync_mode(1);
    status = WriteRegister(btc);
    CHECK_WRITE(BCN_TIME_CFG, status);

    TbttSyncCfg tsc;
    status = ReadRegister(&tsc);
    CHECK_READ(TBTT_SYNC_CFG, status);
    tsc.set_tbtt_adjust(16);
    tsc.set_bcn_exp_win(32);
    tsc.set_bcn_aifsn(2);
    tsc.set_bcn_cwmin(4);
    status = WriteRegister(tsc);
    CHECK_WRITE(TBTT_SYNC_CFG, status);

    MacAddrDw0 mac0;
    MacAddrDw1 mac1;
    mac0.set_mac_addr_0(mac_addr_[0]);
    mac0.set_mac_addr_1(mac_addr_[1]);
    mac0.set_mac_addr_2(mac_addr_[2]);
    mac0.set_mac_addr_3(mac_addr_[3]);
    mac1.set_mac_addr_4(mac_addr_[4]);
    mac1.set_mac_addr_5(mac_addr_[5]);
    mac1.set_unicast_to_me_mask(0xff);
    status = WriteRegister(mac0);
    CHECK_WRITE(MAC_ADDR_DW0, status);
    status = WriteRegister(mac1);
    CHECK_WRITE(MAC_ADDR_DW1, status);

    return ZX_OK;
}

constexpr uint8_t kRfPowerBound = 0x27;
constexpr uint8_t kFreqOffsetBound = 0x5f;

zx_status_t Device::ConfigureChannel(const Channel& channel) {
    EepromLna lna;
    auto status = ReadEepromField(&lna);
    CHECK_READ(EEPROM_LNA, status);
    lna_gain_ = lna.bg();

    WriteRfcsr(RfcsrRegister<8>(channel.N));
    WriteRfcsr(RfcsrRegister<9>(channel.K & 0x0f));
    Rfcsr11 r11;
    status = ReadRfcsr(&r11);
    CHECK_READ(RF11, status);
    r11.set_r(channel.R);
    status = WriteRfcsr(r11);
    CHECK_WRITE(RF11, status);

    Rfcsr49 r49;
    status = ReadRfcsr(&r49);
    CHECK_READ(RF49, status);
    if (channel.default_power1 > kRfPowerBound) {
        r49.set_tx(kRfPowerBound);
    } else {
        r49.set_tx(channel.default_power1);
    }
    status = WriteRfcsr(r49);
    CHECK_WRITE(RF49, status);

    Rfcsr1 r1;
    status = ReadRfcsr(&r1);
    CHECK_READ(RF1, status);
    r1.set_rf_block_en(1);
    r1.set_pll_pd(1);
    r1.set_rx0_pd(1);
    r1.set_tx0_pd(1);
    status = WriteRfcsr(r1);
    CHECK_WRITE(RF1, status);

    // adjust freq offset
    EepromFreq ef;
    ReadEepromField(&ef);
    uint8_t freq_offset = std::min<uint8_t>(ef.offset(), kFreqOffsetBound);

    Rfcsr17 r17;
    status = ReadRfcsr(&r17);
    CHECK_READ(RF17, status);
    uint8_t prev_freq_off = r17.val();

    if (r17.freq_offset() != ef.offset()) {
        status = McuCommand(MCU_FREQ_OFFSET, 0xff, freq_offset, prev_freq_off);
        if (status != ZX_OK) {
            errorf("could not set frequency offset\n");
            return status;
        }
    }

    if (channel.channel <= 14) {
        if (rt_rev_ >= REV_RT5390F) {
            static const uint8_t r55[] = {
                0x23, 0x23, 0x23, 0x23, 0x13, 0x13, 0x03, 0x03,
                0x03, 0x03, 0x03, 0x03, 0x03, 0x03, };
            static const uint8_t r59[] = {
                0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
                0x07, 0x07, 0x06, 0x05, 0x04, 0x04, };
            static_assert(sizeof(r55) == sizeof(r59),
                    "r55 and r59 should have the same number of entries.");
            ZX_DEBUG_ASSERT(channel.hw_index < (ssize_t)sizeof(r55));
            WriteRfcsr(RfcsrRegister<55>(r55[channel.hw_index]));
            WriteRfcsr(RfcsrRegister<59>(r59[channel.hw_index]));
        } else {
            static const uint8_t r59[] = {
                0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8d,
                0x8a, 0x88, 0x88, 0x87, 0x87, 0x86, };
            ZX_DEBUG_ASSERT(channel.hw_index < (ssize_t)sizeof(r59));
            WriteRfcsr(RfcsrRegister<59>(r59[channel.hw_index]));
        }
    }

    Rfcsr30 r30;
    status = ReadRfcsr(&r30);
    CHECK_READ(RF30, status);
    r30.set_tx_h20m(0);
    r30.set_rx_h20m(0);
    status = WriteRfcsr(r30);
    CHECK_WRITE(RF30, status);

    Rfcsr3 r3;
    status = ReadRfcsr(&r3);
    CHECK_READ(RF3, status);
    r3.set_vcocal_en(1);
    status = WriteRfcsr(r3);
    CHECK_WRITE(RF3, status);

    WriteBbp(BbpRegister<62>(0x37 - lna_gain_));
    WriteBbp(BbpRegister<63>(0x37 - lna_gain_));
    WriteBbp(BbpRegister<64>(0x37 - lna_gain_));
    WriteBbp(BbpRegister<86>(0x00));

    TxBandCfg tbc;
    status = ReadRegister(&tbc);
    CHECK_READ(TX_BAND_CFG, status);
    tbc.set_tx_band_sel(0);
    tbc.set_a(0);
    tbc.set_bg(1);
    status = WriteRegister(tbc);
    CHECK_WRITE(TX_BAND_CFG, status);

    TxPinCfg tpc;
    // TODO(tkilbourn): see if we have more than 1 tx or rx chain
    // TODO(tkilbourn): set A values for channels > 14
    tpc.set_pa_pe_g0_en(1);
    tpc.set_lna_pe_a0_en(1);
    tpc.set_lna_pe_g0_en(1);
    tpc.set_rftr_en(1);
    tpc.set_trsw_en(1);
    status = WriteRegister(tpc);
    CHECK_WRITE(TX_PIN_CFG, status);

    Bbp4 b4;
    status = ReadBbp(&b4);
    CHECK_READ(BBP4, status);
    b4.set_bandwidth(0);
    status = WriteBbp(b4);
    CHECK_WRITE(BBP4, status);

    Bbp3 b3;
    status = ReadBbp(&b3);
    CHECK_READ(BBP3, status);
    b3.set_ht40_minus(0);
    status = WriteBbp(b3);
    CHECK_WRITE(BBP3, status);

    sleep_for(ZX_MSEC(1));

    // Clear channel stats by reading the registers
    ChIdleSta cis;
    ChBusySta cbs;
    ExtChBusySta ecbs;
    status = ReadRegister(&cis);
    CHECK_READ(CH_IDLE_STA, status);
    status = ReadRegister(&cbs);
    CHECK_READ(CH_BUSY_STA, status);
    status = ReadRegister(&ecbs);
    CHECK_READ(EXT_CH_BUSY_STA, status);

    return ZX_OK;
}

namespace {
uint8_t CompensateTx(uint8_t power) {
    // TODO(tkilbourn): implement proper tx compensation
    uint8_t high = (power & 0xf0) >> 4;
    uint8_t low = power & 0x0f;
    return (std::min<uint8_t>(high, 0x0c) << 4) | std::min<uint8_t>(low, 0x0c);
}
}  // namespace

zx_status_t Device::ConfigureTxPower(const Channel& channel) {
    // TODO(tkilbourn): calculate tx power control
    //       use 0 (normal) for now
    Bbp1 b1;
    auto status = ReadBbp(&b1);
    CHECK_READ(BBP1, status);
    b1.set_tx_power_ctrl(0);
    status = WriteBbp(b1);
    CHECK_WRITE(BBP1, status);

    uint16_t eeprom_val = 0;
    uint16_t offset = 0;

    // TX_PWR_CFG_0
    TxPwrCfg0 tpc0;
    status = ReadRegister(&tpc0);
    CHECK_READ(TX_PWR_CFG_0, status);

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc0.set_tx_pwr_cck_1(CompensateTx(eeprom_val & 0xff));
    tpc0.set_tx_pwr_cck_5(CompensateTx((eeprom_val >> 8) & 0xff));

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc0.set_tx_pwr_ofdm_6(CompensateTx(eeprom_val & 0xff));
    tpc0.set_tx_pwr_ofdm_12(CompensateTx((eeprom_val >> 8) & 0xff));

    status = WriteRegister(tpc0);
    CHECK_WRITE(TX_PWR_CFG_0, status);

    // TX_PWR_CFG_1
    TxPwrCfg1 tpc1;
    status = ReadRegister(&tpc1);
    CHECK_READ(TX_PWR_CFG_1, status);

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc1.set_tx_pwr_ofdm_24(CompensateTx(eeprom_val & 0xff));
    tpc1.set_tx_pwr_ofdm_48(CompensateTx((eeprom_val >> 8) & 0xff));

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc1.set_tx_pwr_mcs_0(CompensateTx(eeprom_val & 0xff));
    tpc1.set_tx_pwr_mcs_2(CompensateTx((eeprom_val >> 8) & 0xff));

    status = WriteRegister(tpc1);
    CHECK_WRITE(TX_PWR_CFG_1, status);

    // TX_PWR_CFG_2
    TxPwrCfg2 tpc2;
    status = ReadRegister(&tpc2);
    CHECK_READ(TX_PWR_CFG_2, status);

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc2.set_tx_pwr_mcs_4(CompensateTx(eeprom_val & 0xff));
    tpc2.set_tx_pwr_mcs_6(CompensateTx((eeprom_val >> 8) & 0xff));

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc2.set_tx_pwr_mcs_8(CompensateTx(eeprom_val & 0xff));
    tpc2.set_tx_pwr_mcs_10(CompensateTx((eeprom_val >> 8) & 0xff));

    status = WriteRegister(tpc2);
    CHECK_WRITE(TX_PWR_CFG_2, status);

    // TX_PWR_CFG_3
    TxPwrCfg3 tpc3;
    status = ReadRegister(&tpc3);
    CHECK_READ(TX_PWR_CFG_3, status);

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc3.set_tx_pwr_mcs_12(CompensateTx(eeprom_val & 0xff));
    tpc3.set_tx_pwr_mcs_14(CompensateTx((eeprom_val >> 8) & 0xff));

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc3.set_tx_pwr_stbc_0(CompensateTx(eeprom_val & 0xff));
    tpc3.set_tx_pwr_stbc_2(CompensateTx((eeprom_val >> 8) & 0xff));

    status = WriteRegister(tpc3);
    CHECK_WRITE(TX_PWR_CFG_3, status);

    // TX_PWR_CFG_4
    TxPwrCfg4 tpc4;

    status = ReadEepromField(EEPROM_TXPOWER_BYRATE + offset++, &eeprom_val);
    CHECK_READ(EEPROM_TXPOWER, status);

    tpc4.set_tx_pwr_stbc_4(CompensateTx(eeprom_val & 0xff));
    tpc4.set_tx_pwr_stbc_6(CompensateTx((eeprom_val >> 8) & 0xff));

    status = WriteRegister(tpc4);
    CHECK_WRITE(TX_PWR_CFG_4, status);

    return ZX_OK;
}

template <typename R, typename Predicate>
zx_status_t Device::BusyWait(R* reg, Predicate pred, zx_duration_t delay) {
    zx_status_t status;
    unsigned int busy;
    for (busy = 0; busy < kMaxBusyReads; busy++) {
        status = ReadRegister(reg);
        if (status != ZX_OK) {
            return status;
        }
        if (pred()) {
            break;
        }
        sleep_for(delay);
    }
    if (busy == kMaxBusyReads) {
        return ZX_ERR_TIMED_OUT;
    }
    return ZX_OK;
}

static void dump_rx(iotxn_t* request, RxInfo rx_info, RxDesc rx_desc,
        Rxwi0 rxwi0, Rxwi1 rxwi1, Rxwi2 rxwi2, Rxwi3 rxwi3) {
#if RALINK_DUMP_RX
    uint8_t* data;
    iotxn_mmap(request, reinterpret_cast<void**>(&data));
    debugf("dumping received packet\n");
    debugf("rx len=%" PRIu64 "\n", request->actual);
    debugf("rxinfo usb_dma_rx_pkt_len=%u\n", rx_info.usb_dma_rx_pkt_len());
    debugf("rxdesc ba=%u data=%u nulldata=%u frag=%u unicast_to_me=%u multicast=%u\n",
            rx_desc.ba(), rx_desc.data(), rx_desc.nulldata(), rx_desc.frag(),
            rx_desc.unicast_to_me(),rx_desc.multicast());
    debugf("broadcast=%u my_bss=%u crc_error=%u cipher_error=%u amsdu=%u htc=%u rssi=%u\n",
            rx_desc.broadcast(), rx_desc.my_bss(), rx_desc.crc_error(),
            rx_desc.cipher_error(), rx_desc.amsdu(), rx_desc.htc(), rx_desc.rssi());
    debugf("l2pad=%u ampdu=%u decrypted=%u plcp_rssi=%u cipher_alg=%u last_amsdu=%u\n",
            rx_desc.l2pad(), rx_desc.ampdu(), rx_desc.decrypted(), rx_desc.plcp_rssi(),
            rx_desc.cipher_alg(), rx_desc.last_amsdu());
    debugf("plcp_signal=0x%04x\n", rx_desc.plcp_signal());

    debugf("rxwi0 wcid=0x%02x key_idx=%u bss_idx=%u udf=0x%02x "
            "mpdu_total_byte_count=%u tid=0x%02x\n", rxwi0.wcid(), rxwi0.key_idx(), rxwi0.bss_idx(),
            rxwi0.udf(), rxwi0.mpdu_total_byte_count(), rxwi0.tid());
    debugf("rxwi1 frag=%u seq=%u mcs=0x%02x bw=%u sgi=%u stbc=%u phy_mode=%u\n",
            rxwi1.frag(), rxwi1.seq(), rxwi1.mcs(), rxwi1.bw(), rxwi1.sgi(), rxwi1.stbc(),
            rxwi1.phy_mode());
    debugf("rxwi2 rssi0=%u rssi1=%u rssi2=%u\n",
            rxwi2.rssi0(), rxwi2.rssi1(), rxwi2.rssi2());
    debugf("rxwi3 snr0=%u snr1=%u\n",
            rxwi3.snr0(), rxwi3.snr1());

    size_t i = 0;
    for (; i < request->actual; i++) {
        std::printf("0x%02x ", data[i]);
        if (i % 8 == 7) std::printf("\n");
    }
    if (i % 8) {
        std::printf("\n");
    }
#endif
}

static const uint8_t kDataRates[4][8] = {
    // Legacy CCK
    { 2, 4, 11, 22, 0, 0, 0, 0, },
    // Legacy OFDM
    { 12, 18, 24, 36, 48, 72, 96, 108, },
    // HT Mix mode
    { 13, 26, 39, 52, 78, 104, 117, 130, },
    // HT Greenfield
    { 13, 26, 39, 52, 78, 104, 117, 130, },
};

static void fill_rx_info(wlan_rx_info_t* info, Rxwi1 rxwi1, Rxwi2 rxwi2, Rxwi3 rxwi3,
                         uint8_t* rssi_offsets, uint8_t lna_gain) {
    info->flags |= WLAN_RX_INFO_PHY_PRESENT;
    ZX_DEBUG_ASSERT(rxwi1.phy_mode() < 4);
    switch (rxwi1.phy_mode()) {
    case PhyMode::kLegacyCck:
        info->phy = WLAN_PHY_CCK;
        break;
    case PhyMode::kLegacyOfdm:
        info->phy = WLAN_PHY_OFDM;
        break;
    case PhyMode::kHtMixMode:
        info->phy = WLAN_PHY_HT_MIXED;
        break;
    case PhyMode::kHtGreenfield:
        info->phy = WLAN_PHY_HT_GREENFIELD;
        break;
    default:
        // This should not happen!
        warnf("unknown PHY: %u\n", rxwi1.phy_mode());
        info->flags &= ~(WLAN_RX_INFO_PHY_PRESENT);
        break;
    }

    bool ht_phy = rxwi1.phy_mode() == PhyMode::kHtMixMode ||
        rxwi1.phy_mode() == PhyMode::kHtGreenfield;
    uint8_t mcs = rxwi1.mcs();
    if (rxwi1.phy_mode() == PhyMode::kLegacyCck && mcs > 8) {
        mcs -= 8;
    }
    if (rxwi1.phy_mode() < fbl::count_of(kDataRates) && mcs < fbl::count_of(kDataRates[0])) {
        uint8_t rate = kDataRates[rxwi1.phy_mode()][mcs];
        if (rate > 0) {
            info->flags |= WLAN_RX_INFO_DATA_RATE_PRESENT;
            info->data_rate = rate;
        }
    } else if (ht_phy && rxwi1.mcs() == 32) {
        info->flags |= WLAN_RX_INFO_DATA_RATE_PRESENT;
        info->data_rate = 12;
    }

    info->flags |= WLAN_RX_INFO_CHAN_WIDTH_PRESENT;
    info->chan_width = rxwi1.bw() ? WLAN_CHAN_WIDTH_40MHZ : WLAN_CHAN_WIDTH_20MHZ;

    if (ht_phy) {
        if (info->flags & WLAN_RX_INFO_DATA_RATE_PRESENT) {
            if (rxwi1.bw()) info->data_rate *= 2;
            if (rxwi1.sgi()) info->data_rate = (info->data_rate * 10) / 9;
        }
        if (rxwi1.mcs() < 8) {
            info->flags |= WLAN_RX_INFO_MOD_PRESENT;
            info->mod = rxwi1.mcs();
        }
    }

    // TODO(tkilbourn): check rssi1 and rssi2 and figure out what to do with them
    if (rxwi2.rssi0() > 0) {
        info->flags |= WLAN_RX_INFO_RSSI_PRESENT;
        // Use rssi offsets from the EEPROM to convert to RSSI
        info->rssi = static_cast<uint8_t>(-12 - rssi_offsets[0] - lna_gain - rxwi2.rssi0());
    }

    // TODO(tkilbourn): check snr1 and figure out what to do with it
    if (rxwi1.phy_mode() != PhyMode::kLegacyCck && rxwi3.snr0() > 0) {
        info->flags |= WLAN_RX_INFO_SNR_PRESENT;
        // Convert to SNR
        info->snr = ((rxwi3.snr0() * 3 / 16) + 10) * 2;
    }
}

void Device::HandleRxComplete(iotxn_t* request) {
    if (request->status == ZX_ERR_IO_REFUSED) {
        debugf("usb_reset_endpoint\n");
        usb_reset_endpoint(&usb_, rx_endpt_);
    }
    std::lock_guard<std::mutex> guard(lock_);
    auto ac = fbl::MakeAutoCall([&]() { iotxn_queue(parent(), request); });

    if (request->status == ZX_OK) {
        // Handle completed rx
        if (request->actual < 24) {
            errorf("short read\n");
            return;
        }
        uint8_t* data;
        iotxn_mmap(request, reinterpret_cast<void**>(&data));

        uint32_t* data32 = reinterpret_cast<uint32_t*>(data);
        RxInfo rx_info(letoh32(data32[RxInfo::addr()]));
        if (request->actual < 4 + rx_info.usb_dma_rx_pkt_len()) {
            errorf("short read\n");
            return;
        }

        RxDesc rx_desc(*(uint32_t*)(data + 4 + rx_info.usb_dma_rx_pkt_len()));

        Rxwi0 rxwi0(letoh32(data32[Rxwi0::addr()]));
        Rxwi1 rxwi1(letoh32(data32[Rxwi1::addr()]));
        Rxwi2 rxwi2(letoh32(data32[Rxwi2::addr()]));
        Rxwi3 rxwi3(letoh32(data32[Rxwi3::addr()]));

        if (wlanmac_proxy_ != nullptr) {
            wlan_rx_info_t wlan_rx_info = {};
            fill_rx_info(&wlan_rx_info, rxwi1, rxwi2, rxwi3, bg_rssi_offset_, lna_gain_);
            wlan_rx_info.chan.channel_num = current_channel_;
            wlanmac_proxy_->Recv(0u, data + 20, rxwi0.mpdu_total_byte_count(), &wlan_rx_info);
        }

        dump_rx(request, rx_info, rx_desc, rxwi0, rxwi1, rxwi2, rxwi3);
    } else {
        if (request->status != ZX_ERR_IO_REFUSED) {
            errorf("rx txn status %d\n", request->status);
        }
    }
}

void Device::HandleTxComplete(iotxn_t* request) {
    if (request->status == ZX_ERR_IO_REFUSED) {
        debugf("usb_reset_endpoint\n");
        usb_reset_endpoint(&usb_, tx_endpts_.front());
    }
    std::lock_guard<std::mutex> guard(lock_);

    free_write_reqs_.push_back(request);
}

void Device::DdkUnbind() {
    debugfn();
    device_remove(zxdev());
}

void Device::DdkRelease() {
    debugfn();
    delete this;
}

zx_status_t Device::WlanmacQuery(uint32_t options, ethmac_info_t* info) {
    info->mtu = 1500;
    std::memcpy(info->mac, mac_addr_, ETH_MAC_SIZE);
    info->features |= ETHMAC_FEATURE_WLAN;
    return ZX_OK;
}

zx_status_t Device::WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy) {
    debugfn();
    std::lock_guard<std::mutex> guard(lock_);

    if (wlanmac_proxy_ != nullptr) {
        return ZX_ERR_ALREADY_BOUND;
    }

    auto status = LoadFirmware();
    if (status != ZX_OK) {
        errorf("failed to load firmware\n");
        return status;
    }
    debugf("firmware loaded\n");

    // Initialize queues
    for (size_t i = 0; i < kReadReqCount; i++) {
        auto* req = usb_alloc_iotxn(rx_endpt_, kReadBufSize);
        if (req == nullptr) {
            errorf("failed to allocate rx iotxn\n");
            return ZX_ERR_NO_MEMORY;
        }
        req->length = kReadBufSize;
        req->complete_cb = &Device::ReadIotxnComplete;
        req->cookie = this;
        iotxn_queue(parent(), req);
    }
    // Only one TX queue for now
    auto tx_endpt = tx_endpts_.front();
    for (size_t i = 0; i < kWriteReqCount; i++) {
        auto* req = usb_alloc_iotxn(tx_endpt, kWriteBufSize);
        if (req == nullptr) {
            errorf("failed to allocate tx iotxn\n");
            return ZX_ERR_NO_MEMORY;
        }
        req->length = kWriteBufSize;
        req->complete_cb = &Device::WriteIotxnComplete;
        req->cookie = this;
        free_write_reqs_.push_back(req);
    }

    status = EnableRadio();
    if (status != ZX_OK) {
        errorf("could not enable radio\n");
        return status;
    }

    status = StartQueues();
    if (status != ZX_OK) {
        errorf("could not start queues\n");
        return status;
    }

    status = SetupInterface();
    if (status != ZX_OK) {
        errorf("could not setup interface\n");
        return status;
    }

    // TODO(tkilbourn): configure erp?
    // TODO(tkilbourn): configure tx

    // TODO(tkilbourn): configure retry limit (move this)
    TxRtyCfg trc;
    status = ReadRegister(&trc);
    CHECK_READ(TX_RTY_CFG, status);
    trc.set_short_rty_limit(0x07);
    trc.set_long_rty_limit(0x04);
    status = WriteRegister(trc);
    CHECK_WRITE(TX_RTY_CFG, status);

    // TODO(tkilbourn): configure power save (move these)
    AutoWakeupCfg awc;
    status = ReadRegister(&awc);
    CHECK_READ(AUTO_WAKEUP_CFG, status);
    awc.set_wakeup_lead_time(0);
    awc.set_sleep_tbtt_num(0);
    awc.set_auto_wakeup_en(0);
    status = WriteRegister(awc);
    CHECK_WRITE(AUTO_WAKEUP_CFG, status);

    status = McuCommand(MCU_WAKEUP, 0xff, 0, 2);
    if (status != ZX_OK) {
        errorf("error waking MCU err=%d\n", status);
        return status;
    }

    // TODO(tkilbourn): configure antenna
    // for now I'm hardcoding some antenna values
    Bbp1 bbp1;
    status = ReadBbp(&bbp1);
    CHECK_READ(BBP1, status);
    Bbp3 bbp3;
    status = ReadBbp(&bbp3);
    CHECK_READ(BBP3, status);
    bbp3.set_val(0x00);
    bbp1.set_val(0x40);
    status = WriteBbp(bbp3);
    CHECK_WRITE(BBP3, status);
    status = WriteBbp(bbp1);
    CHECK_WRITE(BBP1, status);
    status = WriteBbp(BbpRegister<66>(0x1c));
    CHECK_WRITE(BBP66, status);

    status = SetRxFilter();
    if (status != ZX_OK) {
        return status;
    }

    wlanmac_proxy_.swap(proxy);

    // For now, set the channel at startup just to get some packets flowing
    // TODO(tkilbourn): remove this
    wlan_channel_t chan;
    chan.channel_num = 1;
    status = WlanmacSetChannel(0, &chan);
    if (status != ZX_OK) {
        warnf("could not set channel err=%d\n", status);
    }

    infof("wlan started\n");
    return ZX_OK;
}

void Device::WlanmacStop() {
    debugfn();
    std::lock_guard<std::mutex> guard(lock_);
    wlanmac_proxy_.reset();

    // TODO(tkilbourn) disable radios, stop queues, etc.
}

void Device::WlanmacTx(uint32_t options, const void* data, size_t len) {
    // Our USB packet looks like:
    //   TxInfo (4 bytes)
    //   TXWI fields (16 bytes)
    //   packet (len bytes)
    //   alignment zero padding (round up to a 4-byte boundary)
    //   terminal zero padding (4 bytes)

    size_t align_pad_len = ((len + 3) & ~3) - len;
    size_t terminal_pad_len = 4;
    size_t iotxn_len = sizeof(TxInfo) + 16 + len + align_pad_len + terminal_pad_len;

    if (iotxn_len > kWriteBufSize) {
        errorf("iotxn buffer size insufficient for tx packet -- %d bytes needed\n",
               (int)iotxn_len);
        return;
    }

    iotxn_t* req = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (free_write_reqs_.empty()) {
            // No free write requests! Drop the packet.
            static int failed_writes = 0;
            if (failed_writes++ % 50 == 0) {
                warnf("dropping tx; no free iotxns\n");
            }
            return;
        }
        req = free_write_reqs_.back();
        free_write_reqs_.pop_back();
    }
    ZX_DEBUG_ASSERT(req != nullptr);

    TxPacket* packet;
    zx_status_t status = iotxn_mmap(req, reinterpret_cast<void**>(&packet));
    if (status != ZX_OK) {
        errorf("could not map iotxn: %d\n", status);
        std::lock_guard<std::mutex> guard(lock_);
        free_write_reqs_.push_back(req);
        return;
    }

    std::memset(packet, 0, sizeof(TxPacket));

    // The length field in TxInfo includes everything from the TXWI fields to the alignment pad
    size_t txinfo_len = (16 + len + align_pad_len);
    packet->tx_info.set_tx_pkt_length(txinfo_len);

    // TODO(tkilbourn): set these more appropriately
    packet->tx_info.set_wiv(1);
    packet->tx_info.set_qsel(2);

    packet->txwi0.set_ofdm(1);
    packet->txwi0.set_mcs(7);

    packet->txwi1.set_mpdu_total_byte_count(len);
    packet->txwi1.set_tx_packet_id(10);

    std::memcpy(packet->payload, data, len);
    std::memset(&packet->payload[len], 0, align_pad_len + terminal_pad_len);

    // Send the whole thing
    req->length = iotxn_len;
    iotxn_queue(parent(), req);
}

zx_status_t Device::WlanmacSetChannel(uint32_t options, wlan_channel_t* chan) {
    if (options != 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    auto channel = channels_.find(chan->channel_num);
    if (channel == channels_.end()) {
        return ZX_ERR_NOT_FOUND;
    }
    auto status = StopRxQueue();
    if (status != ZX_OK) {
        errorf("could not stop rx queue\n");
        return status;
    }
    status = ConfigureChannel(channel->second);
    if (status != ZX_OK) {
        return status;
    }
    status = ConfigureTxPower(channel->second);
    if (status != ZX_OK) {
        return status;
    }
    status = StartQueues();
    if (status != ZX_OK) {
        errorf("could not start queues\n");
        return status;
    }

    current_channel_ = chan->channel_num;
    return ZX_OK;
}

void Device::ReadIotxnComplete(iotxn_t* request, void* cookie) {
    if (request->status == ZX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    auto dev = static_cast<Device*>(cookie);
    dev->HandleRxComplete(request);
}

void Device::WriteIotxnComplete(iotxn_t* request, void* cookie) {
    if (request->status == ZX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    auto dev = static_cast<Device*>(cookie);
    dev->HandleTxComplete(request);
}

}  // namespace ralink
