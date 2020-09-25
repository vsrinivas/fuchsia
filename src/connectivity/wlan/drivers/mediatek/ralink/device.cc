// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <endian.h>
#include <inttypes.h>
#include <lib/fit/defer.h>
#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/hw/usb.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <memory>

#include <ddk/protocol/usb.h>
#include <usb/usb-request.h>
#include <wlan/common/channel.h>
#include <wlan/common/cipher.h>
#include <wlan/common/logging.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/phy.h>
#include <wlan/common/tx_vector.h>
#include <wlan/protocol/mac.h>

#include "ralink.h"

#define RALINK_DUMP_EEPROM 0
#define RALINK_DUMP_RX 0
#define RALINK_DUMP_RX_UCAST_ONLY 1
#define RALINK_DUMP_TX 0
#define RALINK_DUMP_TXPOWER 0

#define CHECK_REG(reg, op, status)                                \
  do {                                                            \
    if (status != ZX_OK) {                                        \
      errorf("" #op "Register error for " #reg ": %d\n", status); \
      return status;                                              \
    }                                                             \
  } while (0)
#define CHECK_READ(reg, status) CHECK_REG(reg, Read, status)
#define CHECK_WRITE(reg, status) CHECK_REG(reg, Write, status)

namespace {

zx_status_t sleep_for(zx::duration t) { return zx::nanosleep(zx::deadline_after(t)); }

constexpr size_t kReadReqCount = 128;
constexpr size_t kReadBufSize = 4096;  // Reflecting max A-MSDU length for Ralink: 3839 bytes
constexpr size_t kWriteReqCount = 128;
constexpr size_t kWriteBufSize = 4096;
constexpr size_t kMacHdrAddr1Offset = 4;

constexpr char kFirmwareFile[] = "rt2870.bin";

constexpr int kMaxBusyReads = 20;

// The polling interval and slack for asynchronous transmission reports, when the transmit hardware
// is believed to be idle.
constexpr zx::duration kAsyncTxInterruptIdlePollInterval = zx::msec(100);
constexpr zx::duration kAsyncTxInterruptIdlePollSlack = zx::msec(10);

// The polling interval and slack for asynchronous transmission reports, when the transmit hardware
// is believed to be busy. This interval must be small enough to ensure that packet transmission
// does not overflow the 16-entry hardware TX stats FIFO.
constexpr zx::duration kAsyncTxInterruptBusyPollInterval = zx::msec(1);
constexpr zx::duration kAsyncTxInterruptBusyPollSlack = zx::usec(100);

// The duration of lead time before a beacon frame used to send the pre-beacon notification.
constexpr zx::duration kPreTbttLeadTime = zx::msec(6);

// The polling interval for beacon frame interrupts, for when the interrupt is close but has not yet
// elapsed.
constexpr zx::duration kTbttInterruptPollInterval = zx::msec(1);

// Key which will shut down the currently running interrupt thread.
constexpr uint64_t kInterruptShutdownKey = 1;

// Key indicating an asynchronous transmission report interrupt.
constexpr uint64_t kAsyncTxInterruptKey = 2;

// Key indicating a beacon frame interrupt.
constexpr uint64_t kTbttInterruptKey = 3;

// TODO(hahnr): Use bcast_mac from MacAddr once it was moved to common/.
const uint8_t kBcastAddr[ETH_MAC_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// TX packet ID used to signal an invalid or untracked (e.g. beacon frame) packet.
constexpr int kInvalidTxPacketId = 0;

// The <cstdlib> overloads confuse the compiler for <cstdint> types.
template <typename T>
constexpr T abs(T t) {
  return t < 0 ? -t : t;
}

constexpr size_t RoundUp(size_t size, size_t align) { return (size + align - 1) / align * align; }

int8_t extract_tx_power(int byte_offset, bool is_5ghz, uint16_t eeprom_word) {
  uint8_t val = (byte_offset % 2) ? (eeprom_word >> 8) : eeprom_word;
  int8_t power = *reinterpret_cast<int8_t*>(&val);
  int8_t min_power = is_5ghz ? ralink::kMinTxPower_A : ralink::kMinTxPower_BG;
  int8_t max_power = is_5ghz ? ralink::kMaxTxPower_A : ralink::kMaxTxPower_BG;
  return std::clamp(power, min_power, max_power);
}
}  // namespace

namespace ralink {

namespace wlan_device = ::fuchsia::wlan::device;

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t wlanphy_impl_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->PhyUnbind(); },
    .release = [](void* ctx) { DEV(ctx)->PhyRelease(); },
};

static zx_protocol_device_t wlanmac_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->MacUnbind(); },
    .release = [](void* ctx) { DEV(ctx)->MacRelease(); },
};

static wlanphy_impl_protocol_ops_t wlanphy_impl_ops = {
    .query = [](void* ctx, wlanphy_impl_info_t* info) -> zx_status_t {
      return DEV(ctx)->Query(info);
    },
    .create_iface = [](void* ctx, const wlanphy_impl_create_iface_req_t* req,
                       uint16_t* out_iface_id) -> zx_status_t {
      return DEV(ctx)->CreateIface(req, out_iface_id);
    },
    .destroy_iface = [](void* ctx, uint16_t id) -> zx_status_t {
      return DEV(ctx)->DestroyIface(id);
    },

    .set_country = [](void* ctx, const wlanphy_country_t* country) -> zx_status_t {
      return DEV(ctx)->SetCountry(country);
    },

    .get_country = [](void* ctx, wlanphy_country_t* out_country) -> zx_status_t {
      return DEV(ctx)->GetCountry(out_country);
    },
};

static wlanmac_protocol_ops_t wlanmac_ops = {
    .query = [](void* ctx, uint32_t options, wlanmac_info_t* info) -> zx_status_t {
      return DEV(ctx)->WlanmacQuery(options, info);
    },
    .start = [](void* ctx, const wlanmac_ifc_protocol_t* ifc, zx_handle_t* out_sme_channel)
        -> zx_status_t { return DEV(ctx)->WlanmacStart(ifc, out_sme_channel); },
    .stop = [](void* ctx) { DEV(ctx)->WlanmacStop(); },
    .queue_tx = [](void* ctx, uint32_t options, wlan_tx_packet_t* pkt) -> zx_status_t {
      return DEV(ctx)->WlanmacQueueTx(options, pkt);
    },
    .set_channel = [](void* ctx, uint32_t options, const wlan_channel_t* chan) -> zx_status_t {
      return DEV(ctx)->WlanmacSetChannel(options, chan);
    },
    .configure_bss = [](void* ctx, uint32_t options, const wlan_bss_config_t* config)
        -> zx_status_t { return DEV(ctx)->WlanmacConfigureBss(options, config); },
    .enable_beaconing = [](void* ctx, uint32_t options, const wlan_bcn_config_t* bcn_cfg)
        -> zx_status_t { return DEV(ctx)->WlanmacEnableBeaconing(options, bcn_cfg != nullptr); },
    .configure_beacon = [](void* ctx, uint32_t options, const wlan_tx_packet_t* pkt)
        -> zx_status_t { return DEV(ctx)->WlanmacConfigureBeacon(options, pkt); },
    .set_key = [](void* ctx, uint32_t options, const wlan_key_config_t* key_config) -> zx_status_t {
      return DEV(ctx)->WlanmacSetKey(options, key_config);
    },
    .configure_assoc = [](void* ctx, uint32_t options,
                          const wlan_assoc_ctx* assoc_ctx) -> zx_status_t {
      // TODO(fxbug.dev/28925): Configure the chipset for this association
      return ZX_OK;
    },
    .clear_assoc = [](void* ctx, uint32_t options, const uint8_t* mac, size_t mac_len)
        -> zx_status_t { return DEV(ctx)->WlanmacClearAssoc(options, mac, mac_len); },
};
#undef DEV

constexpr zx::duration Device::kDefaultBusyWait;

Device::Device(zx_device_t* device, usb_protocol_t usb, uint8_t bulk_in,
               std::vector<uint8_t>&& bulk_out, size_t parent_req_size)
    : parent_(device),
      usb_(usb),
      parent_req_size_(parent_req_size),
      rx_endpt_(bulk_in),
      tx_endpts_(std::move(bulk_out)) {
  debugf("Device dev=%p bulk_in=%u\n", parent_, rx_endpt_);
  used_wcid_bitmap_.Reset(kMaxValidWcid);
}

Device::~Device() {
  debugfn();
  for (auto req : free_write_reqs_) {
    usb_request_release(req);
  }
}

zx_status_t Device::Bind() {
  debugfn();

  AsicVerId avi;
  zx_status_t status = ReadRegister(&avi);
  CHECK_READ(ASIC_VER_ID, status);

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

  status = InitializeRfVal();
  if (status != ZX_OK) {
    return status;
  }

  int count = 0;
  for (auto& entry : rf_vals_) {
    bool is_5ghz = entry.second.channel > 14;

    // The eeprom is organized into uint16_ts, but the tx power elements are 8 bits.
    // eeprom_offset represents the eeprom entry for the channel, and extract_tx_power will
    // select the correct bits and clamp them between kMinTxPower and kMaxTxPower.
    ZX_DEBUG_ASSERT(!is_5ghz || count >= 14);
    auto byte_offset = is_5ghz ? (count - 14) : count;
    auto eeprom_offset = byte_offset >> 1;

    // Determine where to find the tx power elements
    auto power1_offset = (is_5ghz ? EEPROM_TXPOWER_A1 : EEPROM_TXPOWER_BG1) + eeprom_offset;
    auto power2_offset = (is_5ghz ? EEPROM_TXPOWER_A2 : EEPROM_TXPOWER_BG2) + eeprom_offset;

    int16_t txpower1, txpower2;
    status = ReadEepromField(power1_offset, reinterpret_cast<uint16_t*>(&txpower1));
    CHECK_READ(EEPROM_TXPOWER_1, status);
    status = ReadEepromField(power2_offset, reinterpret_cast<uint16_t*>(&txpower2));
    CHECK_READ(EEPROM_TXPOWER_2, status);

    // Note: It reads [19, 24] for 2GHz channels,
    // [6, 12] for 5GHz UNII-1,2 channels,
    // [-1, 0] for 5GHz UNII-3 channels. The last appears to be invalid.
    entry.second.default_power1 = extract_tx_power(byte_offset, is_5ghz, txpower1);
    entry.second.default_power2 = extract_tx_power(byte_offset, is_5ghz, txpower2);

    count++;

#if RALINK_DUMP_TXPOWER
    auto rf_val = entry.second;
    auto cal = entry.second.cal_values;
    debugf(
        "[ralink] RF Vals: chan:%3u [eeprom_tx_power_upperbound] 1:%3d 2:%3d 3:%3d "
        "[calibration] "
        "tx0 gain:%3u phase:%3u tx1 gain:%3u phase:%3u\n",
        rf_val.channel, rf_val.default_power1, rf_val.default_power2, rf_val.default_power3,
        cal.gain_cal_tx0, cal.phase_cal_tx0, cal.gain_cal_tx1, cal.phase_cal_tx1);
#endif  // RALINK_DUMP_TXPOWER
  }

  if (rt_type_ == RT5390 || rt_type_ == RT5592) {
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

  EepromFreq ef;
  ReadEepromField(&ef);
  debugf("freq offset=%#x\n", ef.offset());

  EepromEirpMaxTxPower eemtp;
  ReadEepromField(&eemtp);
  if (eemtp.power_2g() < kEirpMaxPower) {
    warnf("has EIRP tx power limit\n");
    warnf("TODO: limit tx power (bug fxbug.dev/20569)\n");
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
  status = AddPhyDevice();
  if (status != ZX_OK) {
    errorf("could not add device err=%d\n", status);
  } else {
    infof("device added\n");
  }

  // TODO(tkilbourn): if status != ZX_OK, reset the hw
  return status;
}

zx_status_t Device::ReadRegister(uint16_t offset, uint32_t* value) {
  auto ret = usb_control_in(&usb_, (USB_DIR_IN | USB_TYPE_VENDOR), kMultiRead, 0, offset,
                            ZX_TIME_INFINITE, value, sizeof(*value), nullptr);
  return ret;
}

template <uint16_t A>
zx_status_t Device::ReadRegister(Register<A>* reg) {
  return ReadRegister(A, reg->mut_val());
}

zx_status_t Device::WriteRegister(uint16_t offset, uint32_t value) {
  auto ret = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite, 0, offset,
                             ZX_TIME_INFINITE, &value, sizeof(value));
  return ret;
}

template <uint16_t A>
zx_status_t Device::WriteRegister(const Register<A>& reg) {
  return WriteRegister(A, reg.val());
}

zx_status_t Device::ReadEeprom() {
  debugfn();
  // Read 4 entries at a time
  static_assert((kEepromSize % 8) == 0, "EEPROM size must be a multiple of 8.");
  for (unsigned int i = 0; i < eeprom_.size(); i += 8) {
    EfuseCtrl ec;
    zx_status_t status = ReadRegister(&ec);
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
    eeprom_[i + 1] = htole32(rd0.val()) >> 16;

    RfuseData1 rd1;
    status = ReadRegister(&rd1);
    CHECK_READ(EFUSE_DATA1, status);
    eeprom_[i + 2] = htole32(rd1.val()) & 0xffff;
    eeprom_[i + 3] = htole32(rd1.val()) >> 16;

    RfuseData2 rd2;
    status = ReadRegister(&rd2);
    CHECK_READ(EFUSE_DATA2, status);
    eeprom_[i + 4] = htole32(rd2.val()) & 0xffff;
    eeprom_[i + 5] = htole32(rd2.val()) >> 16;

    RfuseData3 rd3;
    status = ReadRegister(&rd3);
    CHECK_READ(EFUSE_DATA3, status);
    eeprom_[i + 6] = htole32(rd3.val()) & 0xffff;
    eeprom_[i + 7] = htole32(rd3.val()) >> 16;
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

zx_status_t Device::ReadEepromByte(uint16_t addr, uint8_t* value) {
  uint16_t word_addr = addr >> 1;
  uint16_t word_val;
  zx_status_t result = ReadEepromField(word_addr, &word_val);
  if (result != ZX_OK) {
    return result;
  }
  if (addr & 0x1) {
    *value = (word_val >> 8) & 0xff;
  } else {
    *value = word_val & 0xff;
  }
  return ZX_OK;
}

template <uint16_t A>
zx_status_t Device::ReadEepromField(EepromField<A>* field) {
  return ReadEepromField(field->addr(), field->mut_val());
}

template <uint16_t A>
zx_status_t Device::WriteEepromField(const EepromField<A>& field) {
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
  infof("MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n", mac_addr_[0], mac_addr_[1], mac_addr_[2],
        mac_addr_[3], mac_addr_[4], mac_addr_[5]);

  EepromNicConf0 enc0;
  ReadEepromField(&enc0);
  if (enc0.val() == 0xffff || enc0.val() == 0x2860 || enc0.val() == 0x2872) {
    // These values need some eeprom patching; not supported yet.
    errorf("unsupported value for EEPROM_NIC_CONF0=%#x\n", enc0.val());
    return ZX_ERR_NOT_SUPPORTED;
  }
  tx_path_ = enc0.txpath();
  rx_path_ = enc0.rxpath();

  EepromNicConf1 enc1;
  ReadEepromField(&enc1);
  if (enc1.val() == 0xffff) {
    errorf("unsupported value for EEPROM_NIC_CONF1=%#x\n", enc1.val());
    return ZX_ERR_NOT_SUPPORTED;
  }

  debugf("EEPROM NIC Conf0: val %u rxpath %x txpath %x rf_type %x\n", enc0.val(), enc0.rxpath(),
         enc0.txpath(), enc0.rf_type());
  debugf(
      "EEPROM NIC Conf1: val %u hw_radio %u ext_tx_alc %u ext_lna_2g %u ext_lna_5g %u "
      "cardbus_accel %u bw40m_sb_2g %u bw40m_sb_5g %u wps_pbc %u bw40m_2g %u bw40m_5g %u "
      "broadband_ext_lna %u ant_diversity %u int_tx_alc %u bt_coexist %u dac_test %u\n",
      enc1.val(), enc1.hw_radio(), enc1.external_tx_alc(), enc1.external_lna_2g(),
      enc1.external_lna_5g(), enc1.cardbus_accel(), enc1.bw40m_sb_2g(), enc1.bw40m_sb_5g(),
      enc1.wps_pbc(), enc1.bw40m_2g(), enc1.bw40m_5g(), enc1.broadband_ext_lna(),
      enc1.ant_diversity(), enc1.internal_tx_alc(), enc1.bt_coexist(), enc1.dac_test());

  has_external_lna_2g_ = enc1.external_lna_2g();
  has_external_lna_5g_ = enc1.external_lna_5g();
  antenna_diversity_ = enc1.ant_diversity();

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
  zx_status_t status = load_firmware(zxdev_, kFirmwareFile, &fw_handle, &fw_size);
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
  status = fw.read(fwversion, fw_size - 4, 2);
  if (status != ZX_OK) {
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
    status = fw.read(buf, offset, to_send);
    if (status != ZX_OK) {
      errorf("error reading firmware\n");
      return ZX_ERR_BAD_STATE;
    }
    status = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite, 0, addr,
                             ZX_TIME_INFINITE, buf, to_send);
    if (status != ZX_OK) {
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
  status = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kDeviceMode, kFirmware, 0,
                           ZX_TIME_INFINITE, nullptr, 0);
  if (status != ZX_OK) {
    errorf("failed to send load firmware command\n");
    return status;
  }
  sleep_for(zx::msec(10));

  H2mMailboxCsr hmcsr;
  status = WriteRegister(hmcsr);
  CHECK_WRITE(H2M_MAILBOX_CSR, status);

  SysCtrl sc;
  status = BusyWait(
      &sc, [&sc]() { return sc.mcu_ready(); }, zx::msec(1));
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
  sleep_for(zx::msec(1));

  return ZX_OK;
}

zx_status_t Device::EnableRadio() {
  debugfn();

  // Wakeup the MCU
  zx_status_t status = McuCommand(MCU_WAKEUP, 0xff, 0, 2);
  if (status != ZX_OK) {
    errorf("error waking MCU err=%d\n", status);
    return status;
  }
  sleep_for(zx::msec(1));

  // Wait for WPDMA to be ready
  WpdmaGloCfg wgc;
  auto wpdma_pred = [&wgc]() { return !wgc.tx_dma_busy() && !wgc.rx_dma_busy(); };
  status = BusyWait(&wgc, wpdma_pred, zx::msec(10));
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
  status = BusyWait(&wgc, wpdma_pred, zx::msec(10));
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
  status = BusyWait(
      &msr, [&msr]() { return !msr.tx_status() && !msr.rx_status(); }, zx::msec(10));
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
  sleep_for(zx::msec(1));

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

  sleep_for(zx::usec(50));

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

  zx_status_t status = DisableWpdma();
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

  status = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kDeviceMode, kReset, 0,
                           ZX_TIME_INFINITE, nullptr, 0);
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
  if (rt_type_ == RT5390) {
    tswc1.set_dly_pape_dis(0x06);
    tswc1.set_dly_trsw_dis(0x06);
    tswc1.set_dly_rftr_dis(0x08);
  }  // else value will be set to zero
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
  mlc.set_max_psdu_len(3);
  mlc.set_min_psdu_len(10);
  mlc.set_min_mpdu_len(10);
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
  trc.set_short_rty_limit(2);
  trc.set_long_rty_limit(2);
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
  arc.set_cts_40m_mode(1);
  arc.set_cts_40m_ref(0);
  arc.set_cck_short_en(0);
  arc.set_ctrl_wrap_en(0);
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
  mm20pc.set_prot_ctrl(1);
  mm20pc.set_prot_nav(1);
  mm20pc.set_txop_allow_cck_tx(0);
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
  mm40pc.set_prot_ctrl(1);
  mm40pc.set_prot_nav(1);
  mm40pc.set_txop_allow_cck_tx(0);
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
  gf20pc.set_prot_ctrl(1);
  gf20pc.set_prot_nav(1);
  gf20pc.set_txop_allow_cck_tx(0);
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
  gf40pc.set_prot_ctrl(1);
  gf40pc.set_prot_nav(1);
  gf40pc.set_txop_allow_cck_tx(0);
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
  if (rt_type_ == RT5592) {
    the.set_reserved_unk(4);
  }
  status = WriteRegister(the);
  CHECK_WRITE(TXOP_HLDR_ET, status);

  TxRtsCfg txrtscfg;
  status = ReadRegister(&txrtscfg);
  CHECK_READ(TX_RTS_CFG, status);
  txrtscfg.set_rts_rty_limit(7);
  txrtscfg.set_rts_thres(2353);  // IEEE80211_MAX_RTS_THRESHOLD in Linux
  txrtscfg.set_rts_fbk_en(1);
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

  // TODO(porce): Factor out encryption key clearing
  for (int i = 0; i < 4; i++) {
    status = WriteRegister(SHARED_KEY_MODE_BASE + i * sizeof(uint32_t), 0);
    CHECK_WRITE(SHARED_KEY_MODE, status);
  }

  RxWcidEntry rwe;
  memset(&rwe.mac, 0xff, sizeof(rwe.mac));
  memset(&rwe.ba_sess_mask, 0xff, sizeof(rwe.ba_sess_mask));
  for (int i = 0; i < 256; i++) {
    uint16_t addr = RX_WCID_BASE + i * sizeof(rwe);
    status = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite, 0, addr,
                             ZX_TIME_INFINITE, &rwe, sizeof(rwe));
    if (status != ZX_OK) {
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
  itc.set_pre_tbtt_timer(6 << 4);  // 6.144 msec
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

zx_status_t Device::InitBbp() {
  debugfn();

  switch (rt_type_) {
    case RT5390:
      return InitBbp5390();
    case RT5592:
      return InitBbp5592();
    default:
      errorf("Invalid device type in InitBbp\n");
      return ZX_ERR_NOT_FOUND;
  }
}

zx_status_t Device::InitBbp5390() {
  debugfn();

  Bbp4 reg;
  zx_status_t status = ReadBbp(&reg);
  CHECK_READ(BBP4, status);
  reg.set_mac_if_ctrl(1);
  status = WriteBbp(reg);
  CHECK_WRITE(BBP4, status);

  std::vector<RegInitValue> reg_init_values{
      // clang-format off
        RegInitValue(31,  0x08),
        RegInitValue(65,  0x2c),
        RegInitValue(66,  0x38),
        RegInitValue(68,  0x0b),
        RegInitValue(69,  0x12),
        RegInitValue(73,  0x13),
        RegInitValue(75,  0x46),
        RegInitValue(76,  0x28),
        RegInitValue(77,  0x59),
        RegInitValue(70,  0x0a),
        RegInitValue(79,  0x13),
        RegInitValue(80,  0x05),
        RegInitValue(81,  0x33),
        RegInitValue(82,  0x62),
        RegInitValue(83,  0x7a),
        RegInitValue(84,  0x9a),
        RegInitValue(86,  0x38),
        RegInitValue(91,  0x04),
        RegInitValue(92,  0x02),
        RegInitValue(103, 0xc0),
        RegInitValue(104, 0x92),
        RegInitValue(105, 0x3c),
        RegInitValue(106, 0x03),
        RegInitValue(128, 0x12),
// clang-format off
    };
    status = WriteBbpGroup(reg_init_values);
    if (status != ZX_OK) {
        return status;
    }

    // disable unused dac/adc
    Bbp138 bbp138;
    status = ReadBbp(&bbp138);
    CHECK_READ(BBP138, status);
    if (tx_path_ == 1) {
        bbp138.set_tx_dac1(1);
    }
    if (rx_path_ == 1) {
        bbp138.set_rx_adc1(0);
    }
    status = WriteBbp(bbp138);
    CHECK_WRITE(BBP138, status);

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
    bbp152.set_rx_default_ant(antenna_diversity_ == 3 ? 0 : 1);
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

zx_status_t Device::InitBbp5592() {
    // Initialize first group of BBP registers
    std::vector<RegInitValue> reg_init_values {
// clang-format off
        RegInitValue(65, 0x2c),
        RegInitValue(66, 0x38),
        RegInitValue(68, 0x0b),
        RegInitValue(69, 0x12),
        RegInitValue(70, 0x0a),
        RegInitValue(73, 0x10),
        RegInitValue(81, 0x37),
        RegInitValue(82, 0x62),
        RegInitValue(83, 0x6a),
        RegInitValue(84, 0x99),
        RegInitValue(86, 0x00),
        RegInitValue(91, 0x04),
        RegInitValue(92, 0x00),
        RegInitValue(103, 0x00),
        RegInitValue(105, 0x05),
        RegInitValue(106, 0x35),
      // clang-format on
  };
  zx_status_t status = WriteBbpGroup(reg_init_values);
  if (status != ZX_OK) {
    return status;
  }

  // Set MLD (Maximum Likelihood Detection) in BBP location 105
  Bbp105 bbp105;
  status = ReadBbp(&bbp105);
  CHECK_READ(BBP105, status);
  bbp105.set_mld(rx_path_ == 2 ? 1 : 0);
  status = WriteBbp(bbp105);
  CHECK_WRITE(BBP105, status);

  // Set MAC_IF_CTRL in BBP location 4
  Bbp4 bbp4;
  status = ReadBbp(&bbp4);
  CHECK_READ(BBP4, status);
  bbp4.set_mac_if_ctrl(1);
  status = WriteBbp(bbp4);
  CHECK_WRITE(BBP4, status);

  // Initialize second group of BBP registers
  std::vector<RegInitValue> reg_init_values2{
      // clang-format off
        RegInitValue(20,  0x06),
        RegInitValue(31,  0x08),
        RegInitValue(65,  0x2c),
        RegInitValue(68,  0xdd),
        RegInitValue(69,  0x1a),
        RegInitValue(70,  0x05),
        RegInitValue(73,  0x13),
        RegInitValue(74,  0x0f),
        RegInitValue(75,  0x4f),
        RegInitValue(76,  0x28),
        RegInitValue(77,  0x59),
        RegInitValue(84,  0x9a),
        RegInitValue(86,  0x38),
        RegInitValue(88,  0x90),
        RegInitValue(91,  0x04),
        RegInitValue(92,  0x02),
        RegInitValue(95,  0x9a),
        RegInitValue(98,  0x12),
        RegInitValue(103, 0xc0),
        RegInitValue(104, 0x92),
        RegInitValue(105, 0x3c),
        RegInitValue(106, 0x35),
        RegInitValue(128, 0x12),
        RegInitValue(134, 0xd0),
        RegInitValue(135, 0xf6),
        RegInitValue(137, 0x0f),
      // clang-format on
  };
  status = WriteBbpGroup(reg_init_values2);
  if (status != ZX_OK) {
    return status;
  }

  // Set GLRT values (Generalized likelihood ratio tests?)
  // clang-format off
    uint8_t glrt_values[] = { 0xe0, 0x1f, 0x38, 0x32, 0x08, 0x28, 0x19, 0x0a,
                              0xff, 0x00, 0x16, 0x10, 0x10, 0x0b, 0x36, 0x2c,
                              0x26, 0x24, 0x42, 0x36, 0x30, 0x2d, 0x4c, 0x46,
                              0x3d, 0x40, 0x3e, 0x42, 0x3d, 0x40, 0x3c, 0x34,
                              0x2c, 0x2f, 0x3c, 0x35, 0x2e, 0x2a, 0x49, 0x41,
                              0x36, 0x31, 0x30, 0x30, 0x0e, 0x0d, 0x28, 0x21,
                              0x1c, 0x16, 0x50, 0x4a, 0x43, 0x40, 0x10, 0x10,
                              0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x7d, 0x14, 0x32, 0x2c, 0x36, 0x4c, 0x43, 0x2c,
                              0x2e, 0x36, 0x30, 0x6e };
  // clang-format on
  status = WriteGlrtBlock(glrt_values, sizeof(glrt_values), 0x80);
  if (status != ZX_OK) {
    return status;
  }

  // Set MAC_IF_CTRL in BBP location 4
  status = ReadBbp(&bbp4);
  CHECK_READ(BBP4, status);
  bbp4.set_mac_if_ctrl(1);
  status = WriteBbp(bbp4);
  CHECK_WRITE(BBP4, status);

  // Set default rx antenna in BBP location 152
  Bbp152 bbp152;
  status = ReadBbp(&bbp152);
  CHECK_READ(BBP152, status);
  bbp152.set_rx_default_ant(antenna_diversity_ == 3 ? 0 : 1);
  status = WriteBbp(bbp152);
  CHECK_WRITE(BBP152, status);

  // Set bit 7 in BBP location 254 (as per Linux)
  if (rt_rev_ >= REV_RT5592C) {
    Bbp254 bbp254;
    status = ReadBbp(&bbp254);
    CHECK_READ(BBP254, status);
    bbp254.set_unk_bit7(1);
    status = WriteBbp(bbp254);
    CHECK_WRITE(BBP254, status);
  }

  // Frequency calibration
  status = WriteBbp(BbpRegister<142>(0x01));
  CHECK_WRITE(BBP142, status);
  status = WriteBbp(BbpRegister<143>(0x39));
  CHECK_WRITE(BBP143, status);

  status = WriteBbp(BbpRegister<84>(0x19));
  CHECK_WRITE(BBP84, status);

  if (rt_rev_ >= REV_RT5592C) {
    status = WriteBbp(BbpRegister<103>(0xc0));
    CHECK_WRITE(BBP103, status);
  }

  return ZX_OK;
}

zx_status_t Device::InitRfcsr() {
  debugfn();

  std::vector<RegInitValue> rfcsr_init_table;
  switch (rt_type_) {
    case RT5390:
      if (rt_rev_ >= REV_RT5390F) {
        rfcsr_init_table = {
            // clang-format off
                RegInitValue(1,  0x0f),
                RegInitValue(2,  0x80),
                RegInitValue(3,  0x88),
                RegInitValue(5,  0x10),
                RegInitValue(6,  0xe0),
                RegInitValue(7,  0x00),
                RegInitValue(10, 0x53),
                RegInitValue(11, 0x4a),
                RegInitValue(12, 0x46),
                RegInitValue(13, 0x9f),
                RegInitValue(14, 0x00),
                RegInitValue(15, 0x00),
                RegInitValue(16, 0x00),
                RegInitValue(18, 0x03),
                RegInitValue(19, 0x00),
                RegInitValue(20, 0x00),
                RegInitValue(21, 0x00),
                RegInitValue(22, 0x20),
                RegInitValue(23, 0x00),
                RegInitValue(24, 0x00),
                RegInitValue(25, 0x80),
                RegInitValue(26, 0x00),
                RegInitValue(27, 0x09),
                RegInitValue(28, 0x00),
                RegInitValue(29, 0x10),
                RegInitValue(30, 0x10),
                RegInitValue(31, 0x80),
                RegInitValue(32, 0x80),
                RegInitValue(33, 0x00),
                RegInitValue(34, 0x07),
                RegInitValue(35, 0x12),
                RegInitValue(36, 0x00),
                RegInitValue(37, 0x08),
                RegInitValue(38, 0x85),
                RegInitValue(39, 0x1b),
                RegInitValue(40, 0x0b),
                RegInitValue(41, 0xbb),
                RegInitValue(42, 0xd2),
                RegInitValue(43, 0x9a),
                RegInitValue(44, 0x0e),
                RegInitValue(45, 0xa2),
                RegInitValue(46, 0x73),
                RegInitValue(47, 0x00),
                RegInitValue(48, 0x10),
                RegInitValue(49, 0x94),
                RegInitValue(52, 0x38),
                RegInitValue(53, 0x00),
                RegInitValue(54, 0x78),
                RegInitValue(55, 0x44),
                RegInitValue(56, 0x42),
                RegInitValue(57, 0x80),
                RegInitValue(58, 0x7f),
                RegInitValue(59, 0x8f),
                RegInitValue(60, 0x45),
                RegInitValue(61, 0xd1), // 0xd5 for non-USB
                RegInitValue(62, 0x00),
                RegInitValue(63, 0x00),
            // clang-format on
        };
      } else {
        // RT5390 before rev. F
        rfcsr_init_table = {
            // clang-format off
                RegInitValue(1,  0x0f),
                RegInitValue(2,  0x80),
                RegInitValue(3,  0x88),
                RegInitValue(5,  0x10),
                RegInitValue(6,  0xa0),
                RegInitValue(7,  0x00),
                RegInitValue(10, 0x53),
                RegInitValue(11, 0x4a),
                RegInitValue(12, 0x46),
                RegInitValue(13, 0x9f),
                RegInitValue(14, 0x00),
                RegInitValue(15, 0x00),
                RegInitValue(16, 0x00),
                RegInitValue(18, 0x03),
                RegInitValue(19, 0x00),
                RegInitValue(20, 0x00),
                RegInitValue(21, 0x00),
                RegInitValue(22, 0x20),
                RegInitValue(23, 0x00),
                RegInitValue(24, 0x00),
                RegInitValue(25, 0xc0),
                RegInitValue(26, 0x00),
                RegInitValue(27, 0x09),
                RegInitValue(28, 0x00),
                RegInitValue(29, 0x10),
                RegInitValue(30, 0x10),
                RegInitValue(31, 0x80),
                RegInitValue(32, 0x80),
                RegInitValue(33, 0x00),
                RegInitValue(34, 0x07),
                RegInitValue(35, 0x12),
                RegInitValue(36, 0x00),
                RegInitValue(37, 0x08),
                RegInitValue(38, 0x85),
                RegInitValue(39, 0x1b),
                RegInitValue(40, 0x0b),
                RegInitValue(41, 0xbb),
                RegInitValue(42, 0xd2),
                RegInitValue(43, 0x9a),
                RegInitValue(44, 0x0e),
                RegInitValue(45, 0xa2),
                RegInitValue(46, 0x7b),
                RegInitValue(47, 0x00),
                RegInitValue(48, 0x10),
                RegInitValue(49, 0x94),
                RegInitValue(52, 0x38),
                RegInitValue(53, 0x84),
                RegInitValue(54, 0x78),
                RegInitValue(55, 0x44),
                RegInitValue(56, 0x22),
                RegInitValue(57, 0x80),
                RegInitValue(58, 0x7f),
                RegInitValue(59, 0x8f),
                RegInitValue(60, 0x45),
                RegInitValue(61, 0xdd), // 0xb5 for non-USB
                RegInitValue(62, 0x00),
                RegInitValue(63, 0x00),
            // clang-format on
        };
      }
      break;
    case RT5592:
      rfcsr_init_table = {
          // clang-format off
            RegInitValue(1,  0x3f),
            RegInitValue(3,  0x08),
            RegInitValue(5,  0x10),
            RegInitValue(6,  0xe4),
            RegInitValue(7,  0x00),
            RegInitValue(14, 0x00),
            RegInitValue(15, 0x00),
            RegInitValue(16, 0x00),
            RegInitValue(18, 0x03),
            RegInitValue(19, 0x4d),
            RegInitValue(20, 0x10),
            RegInitValue(21, 0x8d),
            RegInitValue(26, 0x82),
            RegInitValue(28, 0x00),
            RegInitValue(29, 0x10),
            RegInitValue(33, 0xc0),
            RegInitValue(34, 0x07),
            RegInitValue(35, 0x12),
            RegInitValue(47, 0x0c),
            RegInitValue(53, 0x22),
            RegInitValue(63, 0x07),
            RegInitValue(2,  0x80),
          // clang-format on
      };
      break;
    default:
      errorf("Invalid device type in %s\n", __FUNCTION__);
      return ZX_ERR_NOT_FOUND;
  }

  // Init calibration
  Rfcsr2 r2;
  zx_status_t status = ReadRfcsr(&r2);
  CHECK_READ(RF2, status);

  r2.set_rescal_en(1);
  status = WriteRfcsr(r2);
  CHECK_WRITE(RF2, status);

  sleep_for(zx::msec(1));
  r2.set_rescal_en(0);
  status = WriteRfcsr(r2);
  CHECK_WRITE(RF2, status);

  // Configure rfcsr registers
  for (const auto& entry : rfcsr_init_table) {
    status = WriteRfcsr(entry.addr, entry.val);
    if (status != ZX_OK) {
      errorf("WriteRegister error for RFCSR %u: %d\n", entry.addr, status);
      return status;
    }
  }

  if (rt_type_ == RT5592) {
    sleep_for(zx::msec(1));
    AdjustFreqOffset();
    if (rt_rev_ >= REV_RT5592C) {
      status = WriteBbp(BbpRegister<103>(0xc0));
      CHECK_WRITE(BBP103, status);
    }
  }

  status = NormalModeSetup();
  if (status != ZX_OK) {
    return status;
  }

  if (rt_type_ == RT5592 && rt_rev_ >= REV_RT5592C) {
    status = WriteBbp(BbpRegister<27>(0x03));
    CHECK_WRITE(BBP27, status);
  }
  // TODO(tkilbourn): led open drain enable ??? (doesn't appear in vendor driver?)

  return ZX_OK;
}

zx_status_t Device::McuCommand(uint8_t command, uint8_t token, uint8_t arg0, uint8_t arg1) {
  debugf("McuCommand %u\n", command);
  H2mMailboxCsr hmc;
  zx_status_t status = BusyWait(&hmc, [&hmc]() { return !hmc.owner(); });
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
  sleep_for(zx::msec(1));

  return status;
}

zx_status_t Device::ReadBbp(uint8_t addr, uint8_t* val) {
  BbpCsrCfg bcc;
  auto pred = [&bcc]() { return !bcc.bbp_csr_kick(); };

  zx_status_t status = BusyWait(&bcc, pred);
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

template <uint8_t A>
zx_status_t Device::ReadBbp(BbpRegister<A>* reg) {
  return ReadBbp(reg->addr(), reg->mut_val());
}

zx_status_t Device::WriteBbp(uint8_t addr, uint8_t val) {
  BbpCsrCfg bcc;
  zx_status_t status = BusyWait(&bcc, [&bcc]() { return !bcc.bbp_csr_kick(); });
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

template <uint8_t A>
zx_status_t Device::WriteBbp(const BbpRegister<A>& reg) {
  return WriteBbp(reg.addr(), reg.val());
}

zx_status_t Device::WriteBbpGroup(const std::vector<RegInitValue>& regs) {
  for (auto reg : regs) {
    zx_status_t status = WriteBbp(reg.addr, reg.val);
    if (status != ZX_OK) {
      errorf("WriteRegister error for BBP reg %u: %d\n", reg.addr, status);
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Device::WaitForBbp() {
  H2mBbpAgent hba;
  zx_status_t status = WriteRegister(hba);
  CHECK_WRITE(H2M_BBP_AGENT, status);

  H2mMailboxCsr hmc;
  status = WriteRegister(hmc);
  CHECK_WRITE(H2M_MAILBOX_CSR, status);
  sleep_for(zx::msec(1));

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

zx_status_t Device::WriteGlrt(uint8_t addr, uint8_t val) {
  zx_status_t status;
  status = WriteBbp(195, addr);
  CHECK_WRITE(BBP_GLRT_ADDR, status);
  status = WriteBbp(196, val);
  CHECK_WRITE(BBP_GLRT_VAL, status);
  return ZX_OK;
}

zx_status_t Device::WriteGlrtGroup(const std::vector<RegInitValue>& regs) {
  for (auto reg : regs) {
    zx_status_t status = WriteGlrt(reg.addr, reg.val);
    if (status != ZX_OK) {
      errorf("WriteRegister error for GLRT reg %u: %d\n", reg.addr, status);
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Device::WriteGlrtBlock(uint8_t values[], size_t size, size_t offset) {
  zx_status_t status = ZX_OK;
  size_t ndx;
  for (ndx = 0; ndx < size && status == ZX_OK; ndx++) {
    status = WriteGlrt(offset + ndx, values[ndx]);
  }
  return status;
}

zx_status_t Device::ReadRfcsr(uint8_t addr, uint8_t* val) {
  RfCsrCfg rcc;
  auto pred = [&rcc]() { return !rcc.rf_csr_kick(); };

  zx_status_t status = BusyWait(&rcc, pred);
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

template <uint8_t A>
zx_status_t Device::ReadRfcsr(RfcsrRegister<A>* reg) {
  return ReadRfcsr(reg->addr(), reg->mut_val());
}

zx_status_t Device::WriteRfcsr(uint8_t addr, uint8_t val) {
  RfCsrCfg rcc;
  zx_status_t status = BusyWait(&rcc, [&rcc]() { return !rcc.rf_csr_kick(); });
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

template <uint8_t A>
zx_status_t Device::WriteRfcsr(const RfcsrRegister<A>& reg) {
  return WriteRfcsr(reg.addr(), reg.val());
}

zx_status_t Device::WriteRfcsrGroup(const std::vector<RegInitValue>& regs) {
  for (auto reg : regs) {
    zx_status_t status = WriteRfcsr(reg.addr, reg.val);
    if (status != ZX_OK) {
      errorf("WriteRegister error for RFCSR reg %u: %d\n", reg.addr, status);
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Device::DisableWpdma() {
  WpdmaGloCfg wgc;
  zx_status_t status = ReadRegister(&wgc);
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
  zx_status_t status = usb_control_in(&usb_, (USB_DIR_IN | USB_TYPE_VENDOR), kDeviceMode, kAutorun,
                                      0, ZX_TIME_INFINITE, &fw_mode, sizeof(fw_mode), nullptr);
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
  return BusyWait(
      &avi, [&avi]() { return avi.val() && avi.val() != ~0u; }, zx::msec(1));
}

zx_status_t Device::SetRxFilter() {
  // TODO(porce): Support dynamic filter configuration
  RxFiltrCfg rfc;
  zx_status_t status = ReadRegister(&rfc);
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
  rfc.set_drop_ba(1);  // TODO(porce): Revisit for AMPDU
  rfc.set_drop_bar(1);
  rfc.set_drop_ctrl_rsv(1);
  status = WriteRegister(rfc);
  CHECK_WRITE(RX_FILTR_CFG, status);

  return ZX_OK;
}

constexpr uint8_t kFreqOffsetBound = 0x5f;

zx_status_t Device::AdjustFreqOffset() {
  EepromFreq ef;
  ReadEepromField(&ef);
  uint8_t freq_offset = std::min<uint8_t>(ef.offset(), kFreqOffsetBound);

  Rfcsr17 r17;
  zx_status_t status = ReadRfcsr(&r17);
  CHECK_READ(RF17, status);
  uint8_t prev_freq_off = r17.freq_offset();

  if (prev_freq_off != freq_offset) {
    status = McuCommand(MCU_FREQ_OFFSET, 0xff, freq_offset, prev_freq_off);
    if (status != ZX_OK) {
      errorf("could not set frequency offset\n");
    }
  }

  return status;
}

zx_status_t Device::NormalModeSetup() {
  debugfn();

  Bbp138 bbp138;
  zx_status_t status = ReadBbp(&bbp138);
  CHECK_READ(BBP138, status);
  if (rx_path_) {
    bbp138.set_rx_adc1(0);
  }
  if (tx_path_) {
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
  // RX queue
  MacSysCtrl msc;
  zx_status_t status = ReadRegister(&msc);
  CHECK_READ(MAC_SYS_CTRL, status);
  msc.set_mac_rx_en(1);
  status = WriteRegister(msc);
  CHECK_WRITE(MAC_SYS_CTRL, status);

  // Beacon queue  --  maybe this isn't started here
  // BcnTimeCfg btc;
  // status = ReadRegister(&btc);
  // CHECK_READ(BCN_TIME_CFG, status);
  // btc.set_tsf_timer_en(1);
  // btc.set_tbtt_timer_en(1);
  // btc.set_bcn_tx_en(1);
  // status = WriteRegister(btc);
  // CHECK_WRITE(BCN_TIME_CFG, status);

  // kick the rx queue???

  return ZX_OK;
}

zx_status_t Device::StopRxQueue() {
  MacSysCtrl msc;
  zx_status_t status = ReadRegister(&msc);
  CHECK_READ(MAC_SYS_CTRL, status);
  msc.set_mac_rx_en(0);
  status = WriteRegister(msc);
  CHECK_WRITE(MAC_SYS_CTRL, status);

  return ZX_OK;
}

zx_status_t Device::SetupInterface() {
  BcnTimeCfg btc;
  zx_status_t status = ReadRegister(&btc);
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

zx_status_t Device::InitializeRfVal() {
  if (rt_type_ == RT5390) {
    rf_vals_.insert({
        // clang-format off
                // RfVal(channel, N, R, K)
                {1, RfVal(1, 241, 2, 2)},
                {2, RfVal(2, 241, 2, 7)},
                {3, RfVal(3, 242, 2, 2)},
                {4, RfVal(4, 242, 2, 7)},
                {5, RfVal(5, 243, 2, 2)},
                {6, RfVal(6, 243, 2, 7)},
                {7, RfVal(7, 244, 2, 2)},
                {8, RfVal(8, 244, 2, 7)},
                {9, RfVal(9, 245, 2, 2)},
                {10, RfVal(10, 245, 2, 7)},
                {11, RfVal(11, 246, 2, 2)},
                {12, RfVal(12, 246, 2, 7)},
                {13, RfVal(13, 247, 2, 2)},
                {14, RfVal(14, 248, 2, 4)},
        // clang-format on
    });
  } else if (rt_type_ == RT5592) {
    DebugIndex debug_index;
    zx_status_t status = ReadRegister(&debug_index);
    CHECK_READ(DEBUG_INDEX, status);
    if (debug_index.reserved_xtal()) {
      // 40 MHz xtal
      rf_vals_.insert({
          // clang-format off
                    // RfVal(channel,  N, R, K,  mod)
                    {1,   RfVal(1,   241, 3, 2,  10)},
                    {2,   RfVal(2,   241, 3, 7,  10)},
                    {3,   RfVal(3,   242, 3, 2,  10)},
                    {4,   RfVal(4,   242, 3, 7,  10)},
                    {5,   RfVal(5,   243, 3, 2,  10)},
                    {6,   RfVal(6,   243, 3, 7,  10)},
                    {7,   RfVal(7,   244, 3, 2,  10)},
                    {8,   RfVal(8,   244, 3, 7,  10)},
                    {9,   RfVal(9,   245, 3, 2,  10)},
                    {10,  RfVal(10,  245, 3, 7,  10)},
                    {11,  RfVal(11,  246, 3, 2,  10)},
                    {12,  RfVal(12,  246, 3, 7,  10)},
                    {13,  RfVal(13,  247, 3, 2,  10)},
                    {14,  RfVal(14,  248, 3, 4,  10)},
                    {36,  RfVal(36,  86,  1, 4,  12)},
                    {38,  RfVal(38,  86,  1, 6,  12)},
                    {40,  RfVal(40,  86,  1, 8,  12)},
                    {42,  RfVal(42,  86,  1, 10, 12)},
                    {44,  RfVal(44,  87,  1, 0,  12)},
                    {46,  RfVal(46,  87,  1, 2,  12)},
                    {48,  RfVal(48,  87,  1, 4,  12)},
                    {50,  RfVal(50,  87,  1, 6,  12)},
                    {52,  RfVal(52,  87,  1, 8,  12)},
                    {54,  RfVal(54,  87,  1, 10, 12)},
                    {56,  RfVal(56,  88,  1, 0,  12)},
                    {58,  RfVal(58,  88,  1, 2,  12)},
                    {60,  RfVal(60,  88,  1, 4,  12)},
                    {62,  RfVal(62,  88,  1, 6,  12)},
                    {64,  RfVal(64,  88,  1, 8,  12)},
                    {100, RfVal(100, 91,  1, 8,  12)},
                    {102, RfVal(102, 91,  1, 10, 12)},
                    {104, RfVal(104, 92,  1, 0,  12)},
                    {106, RfVal(106, 92,  1, 2,  12)},
                    {108, RfVal(108, 92,  1, 4,  12)},
                    {110, RfVal(110, 92,  1, 6,  12)},
                    {112, RfVal(112, 92,  1, 8,  12)},
                    {114, RfVal(114, 92,  1, 10, 12)},
                    {116, RfVal(116, 93,  1, 0,  12)},
                    {118, RfVal(118, 93,  1, 2,  12)},
                    {120, RfVal(120, 93,  1, 4,  12)},
                    {122, RfVal(122, 93,  1, 6,  12)},
                    {124, RfVal(124, 93,  1, 8,  12)},
                    {126, RfVal(126, 93,  1, 10, 12)},
                    {128, RfVal(128, 94,  1, 0,  12)},
                    {130, RfVal(130, 94,  1, 2,  12)},
                    {132, RfVal(132, 94,  1, 4,  12)},
                    {134, RfVal(134, 94,  1, 6,  12)},
                    {136, RfVal(136, 94,  1, 8,  12)},
                    {138, RfVal(138, 94,  1, 10, 12)},
                    {140, RfVal(140, 95,  1, 0,  12)},
                    {149, RfVal(149, 95,  1, 9,  12)},
                    {151, RfVal(151, 95,  1, 11, 12)},
                    {153, RfVal(153, 96,  1, 1,  12)},
                    {155, RfVal(155, 96,  1, 3,  12)},
                    {157, RfVal(157, 96,  1, 5,  12)},
                    {159, RfVal(159, 96,  1, 7,  12)},
                    {161, RfVal(161, 96,  1, 9,  12)},
                    {165, RfVal(165, 97,  1, 1,  12)},
                    {184, RfVal(184, 82,  1, 0,  12)},
                    {188, RfVal(188, 82,  1, 4,  12)},
                    {192, RfVal(192, 82,  1, 8,  12)},
                    {196, RfVal(196, 83,  1, 0,  12)},
          // clang-format on
      });
    } else {
      // 20 MHz xtal
      rf_vals_.insert({
          // clang-format off
                    // RfVal(channel,  N, R, K, mod)
                    {1,   RfVal(1,   482, 3, 4,  10)},
                    {2,   RfVal(2,   483, 3, 4,  10)},
                    {3,   RfVal(3,   484, 3, 4,  10)},
                    {4,   RfVal(4,   485, 3, 4,  10)},
                    {5,   RfVal(5,   486, 3, 4,  10)},
                    {6,   RfVal(6,   487, 3, 4,  10)},
                    {7,   RfVal(7,   488, 3, 4,  10)},
                    {8,   RfVal(8,   489, 3, 4,  10)},
                    {9,   RfVal(9,   490, 3, 4,  10)},
                    {10,  RfVal(10,  491, 3, 4,  10)},
                    {11,  RfVal(11,  492, 3, 4,  10)},
                    {12,  RfVal(12,  493, 3, 4,  10)},
                    {13,  RfVal(13,  494, 3, 4,  10)},
                    {14,  RfVal(14,  496, 3, 8,  10)},
                    {36,  RfVal(36,  172, 1, 8,  12)},
                    {38,  RfVal(38,  173, 1, 0,  12)},
                    {40,  RfVal(40,  173, 1, 4,  12)},
                    {42,  RfVal(42,  173, 1, 8,  12)},
                    {44,  RfVal(44,  174, 1, 0,  12)},
                    {46,  RfVal(46,  174, 1, 4,  12)},
                    {48,  RfVal(48,  174, 1, 8,  12)},
                    {50,  RfVal(50,  175, 1, 0,  12)},
                    {52,  RfVal(52,  175, 1, 4,  12)},
                    {54,  RfVal(54,  175, 1, 8,  12)},
                    {56,  RfVal(56,  176, 1, 0,  12)},
                    {58,  RfVal(58,  176, 1, 4,  12)},
                    {60,  RfVal(60,  176, 1, 8,  12)},
                    {62,  RfVal(62,  177, 1, 0,  12)},
                    {64,  RfVal(64,  177, 1, 4,  12)},
                    {100, RfVal(100, 183, 1, 4,  12)},
                    {102, RfVal(102, 183, 1, 8,  12)},
                    {104, RfVal(104, 184, 1, 0,  12)},
                    {106, RfVal(106, 184, 1, 4,  12)},
                    {108, RfVal(108, 184, 1, 8,  12)},
                    {110, RfVal(110, 185, 1, 0,  12)},
                    {112, RfVal(112, 185, 1, 4,  12)},
                    {114, RfVal(114, 185, 1, 8,  12)},
                    {116, RfVal(116, 186, 1, 0,  12)},
                    {118, RfVal(118, 186, 1, 4,  12)},
                    {120, RfVal(120, 186, 1, 8,  12)},
                    {122, RfVal(122, 187, 1, 0,  12)},
                    {124, RfVal(124, 187, 1, 4,  12)},
                    {126, RfVal(126, 187, 1, 8,  12)},
                    {128, RfVal(128, 188, 1, 0,  12)},
                    {130, RfVal(130, 188, 1, 4,  12)},
                    {132, RfVal(132, 188, 1, 8,  12)},
                    {134, RfVal(134, 189, 1, 0,  12)},
                    {136, RfVal(136, 189, 1, 4,  12)},
                    {138, RfVal(138, 189, 1, 8,  12)},
                    {140, RfVal(140, 190, 1, 0,  12)},
                    {149, RfVal(149, 191, 1, 6,  12)},
                    {151, RfVal(151, 191, 1, 10, 12)},
                    {153, RfVal(153, 192, 1, 2,  12)},
                    {155, RfVal(155, 192, 1, 6,  12)},
                    {157, RfVal(157, 192, 1, 10, 12)},
                    {159, RfVal(159, 193, 1, 2,  12)},
                    {161, RfVal(161, 193, 1, 6,  12)},
                    {165, RfVal(165, 194, 1, 2,  12)},
                    {184, RfVal(184, 164, 1, 0,  12)},
                    {188, RfVal(188, 164, 1, 4,  12)},
                    {192, RfVal(192, 165, 1, 8,  12)},
                    {196, RfVal(196, 166, 1, 0,  12)},
          // clang-format on
      });
    }
    // Read all of our Tx calibration values
    TxCalibrationValues ch0_14, ch36_64, ch100_138, ch140_165;
    ReadEepromByte(EEPROM_GAIN_CAL_TX0_CH0_14, &ch0_14.gain_cal_tx0);
    ReadEepromByte(EEPROM_GAIN_CAL_TX0_CH36_64, &ch36_64.gain_cal_tx0);
    ReadEepromByte(EEPROM_GAIN_CAL_TX0_CH100_138, &ch100_138.gain_cal_tx0);
    ReadEepromByte(EEPROM_GAIN_CAL_TX0_CH140_165, &ch140_165.gain_cal_tx0);
    ReadEepromByte(EEPROM_PHASE_CAL_TX0_CH0_14, &ch0_14.phase_cal_tx0);
    ReadEepromByte(EEPROM_PHASE_CAL_TX0_CH36_64, &ch36_64.phase_cal_tx0);
    ReadEepromByte(EEPROM_PHASE_CAL_TX0_CH100_138, &ch100_138.phase_cal_tx0);
    ReadEepromByte(EEPROM_PHASE_CAL_TX0_CH140_165, &ch140_165.phase_cal_tx0);
    ReadEepromByte(EEPROM_GAIN_CAL_TX1_CH0_14, &ch0_14.gain_cal_tx1);
    ReadEepromByte(EEPROM_GAIN_CAL_TX1_CH36_64, &ch36_64.gain_cal_tx1);
    ReadEepromByte(EEPROM_GAIN_CAL_TX1_CH100_138, &ch100_138.gain_cal_tx1);
    ReadEepromByte(EEPROM_GAIN_CAL_TX1_CH140_165, &ch140_165.gain_cal_tx1);
    ReadEepromByte(EEPROM_PHASE_CAL_TX1_CH0_14, &ch0_14.phase_cal_tx1);
    ReadEepromByte(EEPROM_PHASE_CAL_TX1_CH36_64, &ch36_64.phase_cal_tx1);
    ReadEepromByte(EEPROM_PHASE_CAL_TX1_CH100_138, &ch100_138.phase_cal_tx1);
    ReadEepromByte(EEPROM_PHASE_CAL_TX1_CH140_165, &ch140_165.phase_cal_tx1);
    // Note: Regardless the channel, EEPROM reads 0xff for all
    // gain calibrations and phase calibrations, making them seemingly invalid table.
    for (auto& entry : rf_vals_) {
      if (entry.second.channel <= 14) {
        entry.second.cal_values = ch0_14;
      } else if (entry.second.channel <= 64) {
        entry.second.cal_values = ch36_64;
      } else if (entry.second.channel <= 138) {
        entry.second.cal_values = ch100_138;
      } else {
        entry.second.cal_values = ch140_165;
      }
    }
  } else {
    errorf("Unrecognized device family in %s\n", __FUNCTION__);
    return ZX_ERR_NOT_FOUND;
  }
  return ZX_OK;
}

constexpr uint8_t kHwTxPowerPerChainMax = 20;  // dBm
constexpr uint8_t kHwTxPowerPerChainMin = 0;   // dBm

zx_status_t Device::ConfigureChannel5390(const wlan_channel_t& chan) {
  zx_status_t status;
  RfVal rf_val;
  status = LookupRfVal(chan, &rf_val);
  if (status != ZX_OK) {
    return status;
  }

  WriteRfcsr(RfcsrRegister<8>(rf_val.N));
  WriteRfcsr(RfcsrRegister<9>(rf_val.K & 0x0f));
  Rfcsr11 r11;
  status = ReadRfcsr(&r11);
  CHECK_READ(RF11, status);
  r11.set_r(rf_val.R);
  status = WriteRfcsr(r11);
  CHECK_WRITE(RF11, status);

  // TODO(porce): Study why this configuration is outside ConfigureTxpower()
  Rfcsr49 r49;
  status = ReadRfcsr(&r49);
  CHECK_READ(RF49, status);

  // See for EIRP table
  // https://www.air802.com/fcc-rules-and-regulations.html
  constexpr uint8_t target_eirp = 30;
  uint8_t tx_power = GetPerChainTxPower(chan, target_eirp);
  r49.set_tx(tx_power);
  status = WriteRfcsr(r49);
  CHECK_WRITE(RF49, status);

#if RALINK_DUMP_TXPOWER
  debugf(
      "[ralink] TxPower for chan:%s [sw_bound] 2GHz:%u [hw_bound] 1:%d "
      "2:%d 3:%d rectified:%u [result] tx_power1:%u\n",
      wlan::common::ChanStr(chan).c_str(), kRfPowerBound2_4Ghz, rf_val.default_power1,
      rf_val.default_power2, rf_val.default_power3, rectified_hw_upperbound1, tx_power1);
#endif  // RALINK_DUMP_TXPOWER

  Rfcsr1 r1;
  status = ReadRfcsr(&r1);
  CHECK_READ(RF1, status);
  r1.set_rf_block_en(1);
  r1.set_pll_pd(1);
  r1.set_rx0_pd(1);
  r1.set_tx0_pd(1);
  status = WriteRfcsr(r1);
  CHECK_WRITE(RF1, status);

  status = AdjustFreqOffset();
  if (status != ZX_OK) {
    return status;
  }

  if (chan.primary <= 14) {
    int hw_index = chan.primary - 1;
    if (rt_rev_ >= REV_RT5390F) {
      static const uint8_t r55[] = {
          0x23, 0x23, 0x23, 0x23, 0x13, 0x13, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
      };
      static const uint8_t r59[] = {
          0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x05, 0x04, 0x04,
      };
      static_assert(sizeof(r55) == sizeof(r59),
                    "r55 and r59 should have the same number of entries.");
      ZX_DEBUG_ASSERT(hw_index < (ssize_t)sizeof(r55));
      WriteRfcsr(RfcsrRegister<55>(r55[hw_index]));
      WriteRfcsr(RfcsrRegister<59>(r59[hw_index]));
    } else {
      static const uint8_t r59[] = {
          0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8d, 0x8a, 0x88, 0x88, 0x87, 0x87, 0x86,
      };
      ZX_DEBUG_ASSERT(hw_index < (ssize_t)sizeof(r59));
      WriteRfcsr(RfcsrRegister<59>(r59[hw_index]));
    }
  }

  Rfcsr30 r30;
  status = ReadRfcsr(&r30);
  CHECK_READ(RF30, status);
  switch (chan.cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      r30.set_tx_h20m(0);
      r30.set_rx_h20m(0);
      break;
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      r30.set_tx_h20m(1);
      r30.set_rx_h20m(1);
      break;
    default:
      // Unreachable
      ZX_DEBUG_ASSERT(0);
      break;
  }
  status = WriteRfcsr(r30);
  CHECK_WRITE(RF30, status);

  Rfcsr3 r3;
  status = ReadRfcsr(&r3);
  CHECK_READ(RF3, status);
  r3.set_vcocal_en(1);
  status = WriteRfcsr(r3);
  CHECK_WRITE(RF3, status);

  return status;
}

zx_status_t Device::ConfigureChannel5592(const wlan_channel_t& chan) {
  zx_status_t status;
  RfVal rf_val;
  status = LookupRfVal(chan, &rf_val);
  if (status != ZX_OK) {
    return status;
  }

  // Set LDO_CORE_VLEVEL in LDO_CFG0
  LdoCfg0 lc0;
  status = ReadRegister(&lc0);
  CHECK_READ(LDO_CFG0, status);
  if (wlan::common::Is5Ghz(chan) || chan.cbw == WLAN_CHANNEL_BANDWIDTH__40ABOVE ||
      chan.cbw == WLAN_CHANNEL_BANDWIDTH__40BELOW) {
    lc0.set_ldo_core_vlevel(5);
  } else {
    // TODO(porce): Investigate if extra CBW40 in 2GHz support is necessary
    lc0.set_ldo_core_vlevel(0);
  }
  status = WriteRegister(lc0);
  CHECK_WRITE(LDO_CFG0, status);

  // Set N, R, K, mod values
  Rfcsr8 r8;
  r8.set_n(rf_val.N & 0xff);
  status = WriteRfcsr(r8);
  CHECK_WRITE(RF8, status);

  Rfcsr9 r9;
  status = ReadRfcsr(&r9);
  CHECK_READ(RF9, status);
  r9.set_k(rf_val.K & 0xf);
  r9.set_n((rf_val.N & 0x100) >> 8);
  r9.set_mod(((rf_val.mod - 8) & 0x4) >> 2);
  status = WriteRfcsr(r9);
  CHECK_WRITE(RF9, status);

  Rfcsr11 r11;
  status = ReadRfcsr(&r11);
  CHECK_READ(RF11, status);
  r11.set_r(rf_val.R - 1);
  r11.set_mod(rf_val.mod - 8);
  status = WriteRfcsr(r11);
  CHECK_WRITE(RF11, status);

  if (chan.primary <= 14) {
    std::vector<RegInitValue> reg_init_values{
        // clang-format off
            RegInitValue(10, 0x90),
            RegInitValue(11, 0x4a),
            RegInitValue(12, 0x52),
            RegInitValue(13, 0x42),
            RegInitValue(22, 0x40),
            RegInitValue(24, 0x4a),
            RegInitValue(25, 0x80),
            RegInitValue(27, 0x42),
            RegInitValue(36, 0x80),
            RegInitValue(37, 0x08),
            RegInitValue(38, 0x89),
            RegInitValue(39, 0x1b),
            RegInitValue(40, 0x0d),
            RegInitValue(41, 0x9b),
            RegInitValue(42, 0xd5),
            RegInitValue(43, 0x72),
            RegInitValue(44, 0x0e),
            RegInitValue(45, 0xa2),
            RegInitValue(46, 0x6b),
            RegInitValue(48, 0x10),
            RegInitValue(51, 0x3e),
            RegInitValue(52, 0x48),
            RegInitValue(54, 0x38),
            RegInitValue(56, 0xa1),
            RegInitValue(57, 0x00),
            RegInitValue(58, 0x39),
            RegInitValue(60, 0x45),
            RegInitValue(61, 0x91),
            RegInitValue(62, 0x39),
        // clang-format on
    };
    status = WriteRfcsrGroup(reg_init_values);
    if (status != ZX_OK) {
      return status;
    }

    uint8_t val = (chan.primary <= 10) ? 0x07 : 0x06;
    status = WriteRfcsr(23, val);
    CHECK_WRITE(RF23, status);
    status = WriteRfcsr(59, val);
    CHECK_WRITE(RF59, status);

    status = WriteRfcsr(55, 0x43);
    CHECK_WRITE(RF55, status);
  } else {
    std::vector<RegInitValue> reg_init_values{
        // clang-format off
            RegInitValue(10, 0x97),
            RegInitValue(11, 0x40),
            RegInitValue(25, 0xbf),
            RegInitValue(27, 0x42),
            RegInitValue(36, 0x00),
            RegInitValue(37, 0x04),
            RegInitValue(38, 0x85),
            RegInitValue(40, 0x42),
            RegInitValue(41, 0xbb),
            RegInitValue(42, 0xd7),
            RegInitValue(45, 0x41),
            RegInitValue(48, 0x00),
            RegInitValue(57, 0x77),
            RegInitValue(60, 0x05),
            RegInitValue(61, 0x01),
        // clang-format on
    };
    status = WriteRfcsrGroup(reg_init_values);
    if (status != ZX_OK) {
      return status;
    }

    if (chan.primary <= 64) {
      std::vector<RegInitValue> reg_init_values{
          // clang-format off
                RegInitValue(12, 0x2e),
                RegInitValue(13, 0x22),
                RegInitValue(22, 0x60),
                RegInitValue(23, 0x7f),
                RegInitValue(24, chan.primary <= 50 ? 0x09 : 0x07),
                RegInitValue(39, 0x1c),
                RegInitValue(43, 0x5b),
                RegInitValue(44, 0x40),
                RegInitValue(46, 0x00),
                RegInitValue(51, 0xfe),
                RegInitValue(52, 0x0c),
                RegInitValue(54, 0xf8),
                RegInitValue(55, chan.primary <= 50 ? 0x06 : 0x04),
                RegInitValue(56, chan.primary <= 50 ? 0xd3 : 0xbb),
                RegInitValue(58, 0x15),
                RegInitValue(59, 0x7f),
                RegInitValue(62, 0x15),
          // clang-format on
      };
      status = WriteRfcsrGroup(reg_init_values);
      if (status != ZX_OK) {
        return status;
      }
    } else if (chan.primary <= 165) {
      std::vector<RegInitValue> reg_init_values{
          // clang-format off
                RegInitValue(12, 0x0e),
                RegInitValue(13, 0x42),
                RegInitValue(22, 0x40),
                RegInitValue(23, chan.primary <= 153 ? 0x3c : 0x38),
                RegInitValue(24, chan.primary <= 153 ? 0x06 : 0x05),
                RegInitValue(39, chan.primary <= 138 ? 0x1a : 0x18),
                RegInitValue(43, chan.primary <= 138 ? 0x3b : 0x1b),
                RegInitValue(44, chan.primary <= 138 ? 0x20 : 0x10),
                RegInitValue(46, chan.primary <= 138 ? 0x18 : 0x08),
                RegInitValue(51, chan.primary <= 124 ? 0xfc : 0xec),
                RegInitValue(52, 0x06),
                RegInitValue(54, 0xeb),
                RegInitValue(55, chan.primary <= 138 ? 0x01 : 0x00),
                RegInitValue(56, chan.primary <= 128 ? 0xbb : 0xab),
                RegInitValue(58, chan.primary <= 116 ? 0x1d : 0x15),
                RegInitValue(59, chan.primary <= 138 ? 0x3f : 0x7c),
                RegInitValue(62, chan.primary <= 116 ? 0x1d : 0x15),
          // clang-format on
      };
      status = WriteRfcsrGroup(reg_init_values);
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  // TODO(porce): Study why this configuration is outside ConfigureTxpower()
  Rfcsr49 r49;
  status = ReadRfcsr(&r49);
  CHECK_READ(RF49, status);
  constexpr uint8_t target_eirp = 30;
  uint8_t tx_power1 = GetPerChainTxPower(chan, target_eirp);
  r49.set_tx(tx_power1);
  status = WriteRfcsr(r49);
  CHECK_WRITE(RF49, status);
  Rfcsr50 r50;
  status = ReadRfcsr(&r50);
  CHECK_READ(RF50, status);
  uint8_t tx_power2 = GetPerChainTxPower(chan, target_eirp);
  r50.set_tx(tx_power2);
  status = WriteRfcsr(r50);
  CHECK_WRITE(RF50, status);

  Rfcsr1 r1;
  status = ReadRfcsr(&r1);
  CHECK_READ(RF1, status);
  r1.set_rf_block_en(1);
  r1.set_pll_pd(1);
  r1.set_rx0_pd(rx_path_ >= 1);
  r1.set_tx0_pd(tx_path_ >= 1);
  r1.set_rx1_pd(rx_path_ == 2);
  r1.set_tx1_pd(tx_path_ == 2);
  r1.set_rx2_pd(0);
  r1.set_tx2_pd(0);
  status = WriteRfcsr(r1);
  CHECK_WRITE(RF1, status);

  status = WriteRfcsr(6, 0xe4);
  CHECK_WRITE(RF6, status);

  switch (chan.cbw) {  // RFCSR30
    case WLAN_CHANNEL_BANDWIDTH__20:
      status = WriteRfcsr(30, 0x10);
      break;
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      status = WriteRfcsr(30, 0x16);
      break;
    default:
      ZX_DEBUG_ASSERT(0);
      break;
  }
  CHECK_WRITE(RF30, status);

  status = WriteRfcsr(31, 0x80);
  CHECK_WRITE(RF31, status);
  status = WriteRfcsr(32, 0x80);
  CHECK_WRITE(RF32, status);

  status = AdjustFreqOffset();
  if (status != ZX_OK) {
    return status;
  }

  Rfcsr3 r3;
  status = ReadRfcsr(&r3);
  CHECK_READ(RF3, status);
  r3.set_vcocal_en(1);
  status = WriteRfcsr(r3);
  CHECK_WRITE(RF3, status);

  std::vector<RegInitValue> bbp_init_values{
      // clang-format off
        RegInitValue(62, 0x37 - lna_gain_),
        RegInitValue(63, 0x37 - lna_gain_),
        RegInitValue(64, 0x37 - lna_gain_),
        RegInitValue(79, wlan::common::Is2Ghz(chan)? 0x1c : 0x18),
        RegInitValue(80, wlan::common::Is2Ghz(chan)? 0x0e : 0x08),
        RegInitValue(81, wlan::common::Is2Ghz(chan)? 0x3a : 0x38),
        RegInitValue(82, wlan::common::Is2Ghz(chan)? 0x62 : 0x92),
      // clang-format on
  };
  status = WriteBbpGroup(bbp_init_values);
  if (status != ZX_OK) {
    return status;
  }

  std::vector<RegInitValue> glrt_init_values{
      // clang-format off
        RegInitValue(128, wlan::common::Is2Ghz(chan)? 0xe0 : 0xf0),
        RegInitValue(129, wlan::common::Is2Ghz(chan)? 0x1f : 0x1e),
        RegInitValue(130, wlan::common::Is2Ghz(chan)? 0x38 : 0x28),
        RegInitValue(131, wlan::common::Is2Ghz(chan)? 0x32 : 0x20),
        RegInitValue(133, wlan::common::Is2Ghz(chan)? 0x28 : 0x7f),
        RegInitValue(124, wlan::common::Is2Ghz(chan)? 0x19 : 0x7f),
      // clang-format on
  };
  status = WriteGlrtGroup(glrt_init_values);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t Device::LookupRfVal(const wlan_channel_t& chan, RfVal* rf_val) {
  auto center_chan_idx = wlan::common::GetCenterChanIdx(chan);
  auto entry = rf_vals_.find(center_chan_idx);
  if (entry == rf_vals_.end()) {
    errorf("Radio hardware does not support the requested channel %s\n",
           wlan::common::ChanStr(chan).c_str());
    return ZX_ERR_NOT_FOUND;
  }

  *rf_val = entry->second;
  return ZX_OK;
}

zx_status_t Device::ConfigureChannel(const wlan_channel_t& chan) {
  // TODO(porce): Factor out antenna calibration
  EepromLna lna;
  zx_status_t status = ReadEepromField(&lna);
  CHECK_READ(EEPROM_LNA, status);
  lna_gain_ = (chan.primary <= 14) ? lna.bg() : lna.a0();

  switch (rt_type_) {
    case RT5390:
      status = ConfigureChannel5390(chan);
      break;
    case RT5592:
      status = ConfigureChannel5592(chan);
      break;
    default:
      errorf("Ralink device type %u not supported\n", rt_type_);
      return ZX_ERR_NOT_FOUND;
  }

  if (status != ZX_OK) {
    return status;
  }

  WriteBbp(BbpRegister<62>(0x37 - lna_gain_));
  WriteBbp(BbpRegister<63>(0x37 - lna_gain_));
  WriteBbp(BbpRegister<64>(0x37 - lna_gain_));
  WriteBbp(BbpRegister<86>(0x00));

  if (rt_type_ == RT5592) {
    if (chan.primary <= 14) {
      WriteBbp(BbpRegister<82>(has_external_lna_2g_ ? 0x62 : 0x84));
      WriteBbp(BbpRegister<75>(has_external_lna_2g_ ? 0x46 : 0x50));
    } else {
      WriteBbp(BbpRegister<82>(0xf2));
      WriteBbp(BbpRegister<75>(has_external_lna_5g_ ? 0x46 : 0x50));
    }
  }

  TxBandCfg tbc;
  status = ReadRegister(&tbc);
  CHECK_READ(TX_BAND_CFG, status);

  switch (chan.cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      tbc.set_tx_band_sel(0);
      break;
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      tbc.set_tx_band_sel(0);
      break;
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      tbc.set_tx_band_sel(1);
      break;
    default:
      // Unreachable
      ZX_DEBUG_ASSERT(0);
      break;
  }

  if (chan.primary <= 14) {
    tbc.set_a(0);
    tbc.set_bg(1);
  } else {
    tbc.set_a(1);
    tbc.set_bg(0);
  }
  status = WriteRegister(tbc);
  CHECK_WRITE(TX_BAND_CFG, status);

  // TODO(porce): Support tx_path_ >= 3
  TxPinCfg tpc;
  status = ReadRegister(&tpc);
  CHECK_READ(TX_PIN_CFG, status);
  tpc.set_pa_pe_g0_en(chan.primary <= 14);
  tpc.set_pa_pe_g1_en((chan.primary <= 14) && (tx_path_ > 1));
  tpc.set_pa_pe_a0_en(chan.primary > 14);
  tpc.set_pa_pe_a1_en((chan.primary > 14) && (tx_path_ > 1));
  tpc.set_lna_pe_a0_en(1);
  tpc.set_lna_pe_g0_en(1);
  tpc.set_lna_pe_a1_en(tx_path_ > 1);
  tpc.set_lna_pe_g1_en(tx_path_ > 1);
  tpc.set_rftr_en(1);
  tpc.set_trsw_en(1);
  tpc.set_rfrx_en(1);
  status = WriteRegister(tpc);
  CHECK_WRITE(TX_PIN_CFG, status);

  if (rt_type_ == RT5592) {
    switch (chan.cbw) {  // BBP 141
      case WLAN_CHANNEL_BANDWIDTH__20:
        WriteGlrt(141, 0x1a);
        break;
      case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      case WLAN_CHANNEL_BANDWIDTH__40BELOW:
        WriteGlrt(141, 0x10);
        break;
      default:
        ZX_DEBUG_ASSERT(0);
        break;
    }

    // TODO(porce) Revisit the logic for multiple antennas
    uint8_t rx_ndx;
    for (rx_ndx = 0; rx_ndx < rx_path_; rx_ndx++) {
      Bbp27 b27;
      status = ReadBbp(&b27);
      CHECK_READ(BBP27, status);
      b27.set_rx_chain_sel(rx_ndx);
      status = WriteBbp(b27);
      CHECK_WRITE(BBP27, status);
      status = WriteBbp(66, (lna_gain_ * 2) + (chan.primary <= 14 ? 0x1c : 0x24));
      CHECK_WRITE(BBP66, status);
    }

    struct RfVal rf_val;
    status = LookupRfVal(chan, &rf_val);
    if (status != ZX_OK) {
      return status;
    }

    // TODO(porce): Refactor IQ calibration
    status = WriteBbp(158, 0x2c);
    CHECK_WRITE(BBP158, status);
    status = WriteBbp(159, rf_val.cal_values.gain_cal_tx0);
    CHECK_WRITE(BBP159, status);
    status = WriteBbp(158, 0x2d);
    CHECK_WRITE(BBP158, status);
    status = WriteBbp(159, rf_val.cal_values.phase_cal_tx0);
    CHECK_WRITE(BBP159, status);
    status = WriteBbp(158, 0x4a);
    CHECK_WRITE(BBP158, status);
    status = WriteBbp(159, rf_val.cal_values.gain_cal_tx1);
    CHECK_WRITE(BBP159, status);
    status = WriteBbp(158, 0x4b);
    CHECK_WRITE(BBP158, status);
    status = WriteBbp(159, rf_val.cal_values.phase_cal_tx1);
    CHECK_WRITE(BBP159, status);

    uint8_t comp_ctl, imbalance_comp_ctl;
    status = ReadEepromByte(EEPROM_COMP_CTL, &comp_ctl);
    CHECK_READ(EEPROM_COMP_CTL, status);
    status = WriteBbp(158, 0x04);
    CHECK_WRITE(BBP158, status);
    status = WriteBbp(159, comp_ctl == 0xff ? 0 : comp_ctl);
    CHECK_WRITE(BBP159, status);
    status = ReadEepromByte(EEPROM_IMB_COMP_CTL, &imbalance_comp_ctl);
    CHECK_READ(EEPROM_IMB_COMP_CTL, status);
    status = WriteBbp(158, 0x03);
    CHECK_WRITE(BBP158, status);
    status = WriteBbp(159, imbalance_comp_ctl == 0xff ? 0 : imbalance_comp_ctl);
    CHECK_WRITE(BBP159, status);
  }

  Bbp4 b4;
  status = ReadBbp(&b4);
  CHECK_READ(BBP4, status);
  switch (chan.cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      b4.set_bandwidth(0);
      break;
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      b4.set_bandwidth(0x2);
      break;
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      b4.set_bandwidth(0x2);
      break;
    default:
      // Unreachable
      ZX_DEBUG_ASSERT(0);
      break;
  }
  status = WriteBbp(b4);
  CHECK_WRITE(BBP4, status);

  Bbp3 b3;
  status = ReadBbp(&b3);
  CHECK_READ(BBP3, status);
  switch (chan.cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      b3.set_ht40_minus(0);
      break;
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      b3.set_ht40_minus(0);
      break;
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      b3.set_ht40_minus(1);
      break;
    default:
      // Unreachable
      ZX_DEBUG_ASSERT(0);
      break;
  }
  status = WriteBbp(b3);
  CHECK_WRITE(BBP3, status);

  sleep_for(zx::msec(1));

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

uint8_t Device::GetEirpRegUpperBound(const wlan_channel_t& chan) {
  if (wlan::common::Is2Ghz(chan)) {
    return 36;
  } else if (chan.primary <= 48) {
    return 30;
  } else if (chan.primary <= 144) {
    return 29;
  } else {
    return 36;
  }
}

uint8_t Device::GetPerChainTxPower(const wlan_channel_t& chan, uint8_t eirp_target) {
  uint8_t eirp_reg_upperbound = GetEirpRegUpperBound(chan);  // dBm
  uint8_t antenna_gain = 3;                                  // dBi
  uint8_t tx_chain_cnt_contribution = 3;                     // dB, for 2 tx chains

  uint8_t result = eirp_target - antenna_gain - tx_chain_cnt_contribution;
  result = std::min(result, eirp_reg_upperbound);
  result = std::clamp(result, kHwTxPowerPerChainMin, kHwTxPowerPerChainMax);

#if RALINK_DUMP_TXPOWER
  debugf(
      "[ralink] TxPower for chan:%s [eirp] target:%u reg_ub:%u ant_gain:%u tx_chain_cnt:%u [hw] "
      "ub:%u lb:%u [per-chain] result:%u\n",
      wlan::common::ChanStr(chan).c_str(), eirp_target, eirp_reg_upperbound, antenna_gain,
      tx_chain_cnt, hw_upperbound, hw_lowerbound, result);
#endif  // RALINK_DUMP_TXPOWER

  return result;
}

namespace {
uint8_t CompensateTx(uint8_t power) {
  // TODO(tkilbourn): implement proper tx compensation
  uint8_t high = (power & 0xf0) >> 4;
  uint8_t low = power & 0x0f;
  return (std::min<uint8_t>(high, 0x0c) << 4) | std::min<uint8_t>(low, 0x0c);
}
}  // namespace

zx_status_t Device::ConfigureTxPower(const wlan_channel_t& chan) {
  // TODO(porce): Refactor to support
  // (1) Target EIRP configured from a higher layer
  // (2) Calcualte compensation and truncation per rate/MCS, for 4 bit size

  Bbp1 b1;
  zx_status_t status = ReadBbp(&b1);
  CHECK_READ(BBP1, status);

  b1.set_tx_power_ctrl(0);  // TODO(fxbug.dev/28777): Investigate the register effect.

  status = WriteBbp(b1);
  CHECK_WRITE(BBP1, status);

  // Reading of EEPOM from EEPOM_TXPOWER_BYRATE + offset, where
  // offset is in [0, 8] is all 0x6666.
  // Instead of using the value from the EEPROM, use a constant value,
  // with kTxCompMaxPower.
  constexpr uint16_t eeprom_val = kTxCompMaxPower | kTxCompMaxPower << 4 | kTxCompMaxPower << 8 |
                                  kTxCompMaxPower << 12;  // 0xcccc

  // TX_PWR_CFG_0
  TxPwrCfg0 tpc0;
  status = ReadRegister(&tpc0);
  CHECK_READ(TX_PWR_CFG_0, status);

  tpc0.set_tx_pwr_cck_1(CompensateTx(eeprom_val & 0xff));
  tpc0.set_tx_pwr_cck_5(CompensateTx((eeprom_val >> 8) & 0xff));

  tpc0.set_tx_pwr_ofdm_6(CompensateTx(eeprom_val & 0xff));
  tpc0.set_tx_pwr_ofdm_12(CompensateTx((eeprom_val >> 8) & 0xff));

  status = WriteRegister(tpc0);
  CHECK_WRITE(TX_PWR_CFG_0, status);

  // TX_PWR_CFG_1
  TxPwrCfg1 tpc1;
  status = ReadRegister(&tpc1);
  CHECK_READ(TX_PWR_CFG_1, status);

  tpc1.set_tx_pwr_ofdm_24(CompensateTx(eeprom_val & 0xff));
  tpc1.set_tx_pwr_ofdm_48(CompensateTx((eeprom_val >> 8) & 0xff));

  tpc1.set_tx_pwr_mcs_0(CompensateTx(eeprom_val & 0xff));
  tpc1.set_tx_pwr_mcs_2(CompensateTx((eeprom_val >> 8) & 0xff));

  status = WriteRegister(tpc1);
  CHECK_WRITE(TX_PWR_CFG_1, status);

  // TX_PWR_CFG_2
  TxPwrCfg2 tpc2;
  status = ReadRegister(&tpc2);
  CHECK_READ(TX_PWR_CFG_2, status);

  tpc2.set_tx_pwr_mcs_4(CompensateTx(eeprom_val & 0xff));
  tpc2.set_tx_pwr_mcs_6(CompensateTx((eeprom_val >> 8) & 0xff));

  tpc2.set_tx_pwr_mcs_8(CompensateTx(eeprom_val & 0xff));
  tpc2.set_tx_pwr_mcs_10(CompensateTx((eeprom_val >> 8) & 0xff));

  status = WriteRegister(tpc2);
  CHECK_WRITE(TX_PWR_CFG_2, status);

  // TX_PWR_CFG_3
  TxPwrCfg3 tpc3;
  status = ReadRegister(&tpc3);
  CHECK_READ(TX_PWR_CFG_3, status);

  tpc3.set_tx_pwr_mcs_12(CompensateTx(eeprom_val & 0xff));
  tpc3.set_tx_pwr_mcs_14(CompensateTx((eeprom_val >> 8) & 0xff));

  tpc3.set_tx_pwr_stbc_0(CompensateTx(eeprom_val & 0xff));
  tpc3.set_tx_pwr_stbc_2(CompensateTx((eeprom_val >> 8) & 0xff));

  status = WriteRegister(tpc3);
  CHECK_WRITE(TX_PWR_CFG_3, status);

  // TX_PWR_CFG_4
  TxPwrCfg4 tpc4;

  tpc4.set_tx_pwr_stbc_4(CompensateTx(eeprom_val & 0xff));
  tpc4.set_tx_pwr_stbc_6(CompensateTx((eeprom_val >> 8) & 0xff));

  status = WriteRegister(tpc4);
  CHECK_WRITE(TX_PWR_CFG_4, status);

  return ZX_OK;
}

template <typename R, typename Predicate>
zx_status_t Device::BusyWait(R* reg, Predicate pred, zx::duration delay) {
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

static void dump_rx(usb_request_t* request, RxInfo rx_info, RxDesc rx_desc, Rxwi0 rxwi0,
                    Rxwi1 rxwi1, Rxwi2 rxwi2, Rxwi3 rxwi3) {
#if RALINK_DUMP_RX_UCAST_ONLY
  if (rx_desc.unicast_to_me() != 1) {
    return;
  }
#endif  // RALINK_DUMP_RX_UCAST_ONLY

#if RALINK_DUMP_RX

  {  // Length validation
    // TODO(porce): If a warning takes place, it means there is room
    // for improvement on the best understanding how the USB read chunk
    // structure, which is experimentally learned.
    auto len1 = request->response.actual;
    auto len2 = rx_info.usb_dma_rx_pkt_len();
    auto len3 = rxwi0.mpdu_total_byte_count();
    auto len4 = rx_desc.l2pad() == 1 ? 2 : 0;

    if (len1 != len2 + 8 || len1 % 4 != 0) {
      debugf("[ralink] USB read size incongruous)\n");
    }
    debugf(
        "[ralink] USB read size : response.actual %zu usb_dma_rx_pkt_len "
        "%u rx_hdr_size %zu mpdu_total_byte_count %u l2pad_len %u\n",
        len1, len2, rx_hdr_size, len3, len4);
  }

  uint8_t* data;
  usb_request_mmap(request, reinterpret_cast<void**>(&data));
  debugf("# Rxed packet: rx_len=%" PRIu64 "\n", request->response.actual);
  debugf("  rxinfo: usb_dma_rx_pkt_len=%u\n", rx_info.usb_dma_rx_pkt_len());
  debugf("  rxdesc: ba=%u data=%u nulldata=%u frag=%u unicast_to_me=%u multicast=%u\n",
         rx_desc.ba(), rx_desc.data(), rx_desc.nulldata(), rx_desc.frag(), rx_desc.unicast_to_me(),
         rx_desc.multicast());
  debugf(
      "          broadcast=%u my_bss=%u crc_error=%u cipher_error=%u amsdu=%u htc=%u "
      "rssi=%u\n",
      rx_desc.broadcast(), rx_desc.my_bss(), rx_desc.crc_error(), rx_desc.cipher_error(),
      rx_desc.amsdu(), rx_desc.htc(), rx_desc.rssi());
  debugf(
      "          l2pad=%u ampdu=%u decrypted=%u plcp_rssi=%u cipher_alg=%u last_amsdu=%u "
      "plcp_signal=0x%04x\n",
      rx_desc.l2pad(), rx_desc.ampdu(), rx_desc.decrypted(), rx_desc.plcp_rssi(),
      rx_desc.cipher_alg(), rx_desc.last_amsdu(), rx_desc.plcp_signal());
  debugf(
      "  rxwi0 : wcid=0x%02x key_idx=%u bss_idx=%u udf=0x%02x "
      "mpdu_total_byte_count=%u tid=0x%02x\n",
      rxwi0.wcid(), rxwi0.key_idx(), rxwi0.bss_idx(), rxwi0.udf(), rxwi0.mpdu_total_byte_count(),
      rxwi0.tid());
  debugf("  rxwi1 : frag=%u seq=%u mcs=0x%02x bw=%u sgi=%u stbc=%u phy_mode=%u\n", rxwi1.frag(),
         rxwi1.seq(), rxwi1.mcs(), rxwi1.bw(), rxwi1.sgi(), rxwi1.stbc(), rxwi1.phy_mode());
  debugf("  rxwi2 : rssi0=%u rssi1=%u rssi2=%u\n", rxwi2.rssi0(), rxwi2.rssi1(), rxwi2.rssi2());
  debugf("  rxwi3 : snr0=%u snr1=%u\n", rxwi3.snr0(), rxwi3.snr1());
#endif  // RALINK_DUMP_RX
}

static const uint8_t kDataRates[4][8] = {
    // clang-format off
    // Legacy CCK
    { 2, 4, 11, 22, 0, 0, 0, 0, },
    // Legacy OFDM
    { 12, 18, 24, 36, 48, 72, 96, 108, },
    // HT Mix mode
    { 13, 26, 39, 52, 78, 104, 117, 130, },
    // HT Greenfield
    { 13, 26, 39, 52, 78, 104, 117, 130, },
    // clang-format on
};

static uint8_t ralink_mcs_to_rate(uint8_t phy_mode, uint8_t mcs, bool is_40mhz, bool is_sgi) {
  uint8_t rate = 0;            // Mbps * 2
  uint8_t rate_tbl_idx = 255;  // Init with invalid idx.
  uint8_t nss = 1;             // Minimum NSS

  if (phy_mode >= std::size(kDataRates)) {
    return rate;
  }

  switch (phy_mode) {
    case PhyMode::kLegacyCck:
      if (mcs <= kLongPreamble11Mbps) {
        // Long preamble case
        rate_tbl_idx = mcs;
      } else if (kShortPreamble1Mbps <= mcs && mcs <= kShortPreamble11Mbps) {
        // Short preamble case
        rate_tbl_idx = mcs - kShortPreamble1Mbps;
      } else {
        warnf("ralink: illegal mcs for phy %u mcs %u is_40mhz %u is_sgi %u\n", phy_mode, mcs,
              is_40mhz, is_sgi);
        return rate;
      }
      break;
    case PhyMode::kLegacyOfdm:
      rate_tbl_idx = mcs;
      break;
    case PhyMode::kHtMixMode:
      // fallthrough
    case PhyMode::kHtGreenfield:
      if (mcs == kHtDuplicateMcs) {
        // 40MHz, ShortGuardInterval case: HT duplicate 6 Mbps.
        rate_tbl_idx = 0;
      } else if (mcs < kHtDuplicateMcs) {
        rate_tbl_idx = mcs % std::size(kDataRates[0]);
        nss = 1 + mcs / std::size(kDataRates[0]);
      } else {
        rate_tbl_idx = mcs;
      }
      break;
    default:
      warnf("ralink: unknown phy %u with mcs %u is_40mhz %u is_sgi %u\n", phy_mode, mcs, is_40mhz,
            is_sgi);
      return rate;
  }

  if (rate_tbl_idx >= std::size(kDataRates[0])) {
    warnf("ralink: illegal rate_tbl_idx %u for phy %u mcs %u is_40mhz %u is_sgi %u\n", rate_tbl_idx,
          phy_mode, mcs, is_40mhz, is_sgi);
    return rate;
  }

  rate = kDataRates[phy_mode][rate_tbl_idx] * nss;
  if (is_40mhz) {
    // 802.11n case
    // Set the multipler by the ratio of the subcarriers, not by the ratio of the bandwidth
    // rate *= 2.0769;          // Correct
    // rate *= (40MHz / 20MHz); // Incorrect

    constexpr uint8_t subcarriers_data_40 = 108;  // counts
    constexpr uint8_t subcarriers_data_20 = 52;   // counts
    rate = rate * subcarriers_data_40 / subcarriers_data_20;
  }
  if (is_sgi) {
    rate = static_cast<uint8_t>((static_cast<uint16_t>(rate) * 10) / 9);
  }

  return rate;
}

static uint16_t ralink_phy_to_ddk_phy(uint8_t ralink_phy) {
  switch (ralink_phy) {
    case PhyMode::kLegacyCck:
      return WLAN_INFO_PHY_TYPE_CCK;
    case PhyMode::kLegacyOfdm:
      return WLAN_INFO_PHY_TYPE_OFDM;
    case PhyMode::kHtMixMode:
    case PhyMode::kHtGreenfield:
      // TODO(tkilbourn): set a bit somewhere indicating greenfield format, if we ever support
      // it.
      return WLAN_INFO_PHY_TYPE_HT;
    default:
      warnf("received unknown PHY: %u\n", ralink_phy);
      ZX_DEBUG_ASSERT(0);  // TODO: Define Undefined Phy in DDK.
      return 0;            // Happy compiler
  }
}

static uint8_t ddk_phy_to_ralink_phy(uint16_t ddk_phy) {
  switch (ddk_phy) {
    case WLAN_INFO_PHY_TYPE_CCK:
      return PhyMode::kLegacyCck;
    case WLAN_INFO_PHY_TYPE_OFDM:
      return PhyMode::kLegacyOfdm;
    case WLAN_INFO_PHY_TYPE_HT:
      return PhyMode::kHtMixMode;
    default:
      warnf("invalid DDK phy: %u. Fallback to PHY_OFDM\n", ddk_phy);
      return PhyMode::kLegacyOfdm;
  }
}

static uint8_t mcs_to_ralink_mcs(uint8_t vendor_phy_mode, uint8_t mcs) {
  // TODO(porce): Translate Rate index in each phy to ralink MCS values
  // For LegacyOFDM:
  // Standard MCS index: 13, 16, 5, 7, 9, 11, 1, 3 map to 6, 9, 12, 18, 24, 36, 48, 54 Mbps
  // which in turns maps to Ralink MCS index: 0, 1, 2, 3, 4, 5, 6, 7.

  // For CCK,
  // Ralink supports 0 to 3, mapping to 1, 2, 5.5, 11 Mbps, for long preamble.
  // Add value 8 to mcs index for short preamble.
  return mcs;
}

static void fill_rx_info(wlan_rx_info_t* info, RxDesc rx_desc, Rxwi1 rxwi1, Rxwi2 rxwi2,
                         Rxwi3 rxwi3, uint8_t* rssi_offsets, uint8_t lna_gain) {
  if (rx_desc.l2pad()) {
    info->rx_flags |= WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4;
  }
  info->valid_fields |= WLAN_RX_INFO_VALID_PHY;
  info->phy = ralink_phy_to_ddk_phy(rxwi1.phy_mode());

  uint8_t rate = ralink_mcs_to_rate(rxwi1.phy_mode(), rxwi1.mcs(), rxwi1.bw(), rxwi1.sgi());
  if (rate != 0) {
    info->valid_fields |= WLAN_RX_INFO_VALID_DATA_RATE;
    info->data_rate = rate;
  }

  info->valid_fields |= WLAN_RX_INFO_VALID_CHAN_WIDTH;
  // TODO(porce): Study how to distinguish CBW40ABOVE from CBW40BELOW, from rxwi.
  info->chan.cbw = rxwi1.bw() ? WLAN_CHANNEL_BANDWIDTH__40 : WLAN_CHANNEL_BANDWIDTH__20;

  uint8_t phy_mode = rxwi1.phy_mode();
  bool is_ht = phy_mode == PhyMode::kHtMixMode || phy_mode == PhyMode::kHtGreenfield;
  if (is_ht && rxwi1.mcs() < kMaxHtMcs) {
    info->valid_fields |= WLAN_RX_INFO_VALID_MCS;
    info->mcs = rxwi1.mcs();
  }

  // TODO(tkilbourn): check rssi1 and rssi2 and figure out what to do with them
  info->rssi_dbm = WLAN_RSSI_DBM_INVALID;
  info->rcpi_dbmh = WLAN_RCPI_DBMH_INVALID;
  info->snr_dbh = WLAN_RSNI_DBH_INVALID;

  if (rxwi2.rssi0() > 0) {
    // Use rssi offsets from the EEPROM to convert to RSSI
    auto rssi_dbm = static_cast<int8_t>(-12 - rssi_offsets[0] - lna_gain - rxwi2.rssi0());
    if (WLAN_RSSI_DBM_MIN <= rssi_dbm && rssi_dbm <= WLAN_RSSI_DBM_MAX) {
      info->valid_fields |= WLAN_RX_INFO_VALID_RSSI;
      info->rssi_dbm = rssi_dbm;
    }
  }

  // TODO(tkilbourn): check snr1 and figure out what to do with it
  if (rxwi1.phy_mode() != PhyMode::kLegacyCck && rxwi3.snr0() > 0) {
    // Convert to SNR
    auto snr_dbh = ((rxwi3.snr0() * 3 / 16) + 10) * 2;
    if (WLAN_RSNI_DBH_MIN <= snr_dbh && snr_dbh <= WLAN_RSNI_DBH_MAX) {
      info->valid_fields |= WLAN_RX_INFO_VALID_SNR;
      info->snr_dbh = snr_dbh;
    }
  }
}

void Device::HandleRxComplete(usb_request_t* request) {
  if (request->response.status == ZX_ERR_IO_REFUSED) {
    debugf("usb_reset_endpoint\n");
    usb_reset_endpoint(&usb_, rx_endpt_);
  }
  std::lock_guard<std::mutex> guard(lock_);
  auto ac = fit::defer([&]() {
    usb_request_complete_t complete = {
        .callback = &Device::ReadRequestComplete,
        .ctx = this,
    };
    usb_request_queue(&usb_, request, &complete);
  });

  if (request->response.status == ZX_OK) {
    // Total bytes received is (request->response.actual) bytes
    // request->response.actual := (a) + (b) + (c) + (d) + (e) + (f) + (g) + (h)
    // rf.info.usb_dma_rx_pkt_len() := (b) + (c) + (d) + (e) + (f) + (g)

    // RxInfo      :   4 bytes // (a).
    // RxWI        :  16 bytes // (b).
    // RxWI-Extra  :   8 bytes // (c). Present only for RT5592
    // MAC header  : (d) bytes // (d). (d) + (f) is rxwi0.mpdu_total_byte_count()
    // L2PAD       :   2 bytes // (e). Present only if rx_desc.l2pad() is 1
    // MAC payload : (f) bytes // (f). Start of (f) is 4-byte aligned
    // Padding     : 0~3 bytes // (g). To align in 4 bytes
    // RxDesc      :   4 bytes // (h).

    size_t rx_hdr_size = (rt_type_ == RT5592) ? 28 : 20;

    // Handle completed rx
    if (request->response.actual < rx_hdr_size + 4) {
      errorf("short read: response.actual %ld rx_hdr_size %zu\n", request->response.actual,
             rx_hdr_size);
      return;
    }

    uint8_t* data;
    usb_request_mmap(request, reinterpret_cast<void**>(&data));
    uint32_t* data32 = reinterpret_cast<uint32_t*>(data);
    RxInfo rx_info(letoh32(data32[RxInfo::addr()]));

    if (request->response.actual < 4 + rx_info.usb_dma_rx_pkt_len()) {
      errorf("short read: response.actual %ld usb_dma_rx_pkt_len %d\n", request->response.actual,
             rx_info.usb_dma_rx_pkt_len());
      return;
    }

    Rxwi0 rxwi0(letoh32(data32[Rxwi0::addr()]));
    Rxwi1 rxwi1(letoh32(data32[Rxwi1::addr()]));
    Rxwi2 rxwi2(letoh32(data32[Rxwi2::addr()]));
    Rxwi3 rxwi3(letoh32(data32[Rxwi3::addr()]));
    RxDesc rx_desc(*(uint32_t*)(data + 4 + rx_info.usb_dma_rx_pkt_len()));

    dump_rx(request, rx_info, rx_desc, rxwi0, rxwi1, rxwi2, rxwi3);
    if (wlanmac_proxy_.is_valid()) {
      wlan_rx_info_t wlan_rx_info = {};
      fill_rx_info(&wlan_rx_info, rx_desc, rxwi1, rxwi2, rxwi3, bg_rssi_offset_, lna_gain_);

      // Be mindful in interpretation of wlan_rx_info.chan:
      // That reflects how the radio was configured in prior,
      // and does not reflect how the incoming frame is received, which
      // shall be referred by rxwi.
      wlan_rx_info.chan = cfg_chan_;

      // TODO(porce): Pass up the byte stream after stripping off the zero padding.
      // Keep MLME ignorant of Ralink-specific L2Padding
      uint16_t mpdu_len_ota = rxwi0.mpdu_total_byte_count();
      uint16_t l2pad_len = rx_desc.l2pad() ? 2 : 0;  // 2 bytes if padded, by Ralink spec.
      uint16_t mpdu_len = mpdu_len_ota + l2pad_len;
      wlanmac_proxy_.Recv(0u, data + rx_hdr_size, mpdu_len, &wlan_rx_info);
    }
  } else {
    if (request->response.status != ZX_ERR_IO_REFUSED) {
      errorf("rx req status %d\n", request->response.status);
    }
  }
}

void Device::HandleTxComplete(usb_request_t* request) {
  if (request->response.status == ZX_ERR_IO_REFUSED) {
    debugf("usb_reset_endpoint\n");
    usb_reset_endpoint(&usb_, tx_endpts_.front());
  }
  std::lock_guard<std::mutex> guard(lock_);

  free_write_reqs_.push_back(request);
}

void Device::PhyUnbind() {
  debugfn();

  {
    std::lock_guard<std::mutex> guard(lock_);
    phy_state_ = PHY_DESTROYING;
  }

  StopInterruptPolling();
  device_unbind_reply(zxdev_);
}

void Device::PhyRelease() {
  debugfn();
  delete this;
}

void Device::MacUnbind() {
  debugfn();
  zx_device_t* dev;
  {
    std::lock_guard<std::mutex> guard(lock_);
    iface_state_ = IFC_DESTROYING;
    dev = wlanmac_dev_;
  }
  device_unbind_reply(dev);
}

void Device::MacRelease() {
  debugfn();
  // Do not delete this right now, as the wlanmac device shares a context with the wlanphy-impl
  // device. When the wlanphy-impl is released, then the memory will be freed. We do forget that
  // this device existed though.
  std::lock_guard<std::mutex> guard(lock_);
  wlanmac_dev_ = nullptr;
  iface_role_ = 0;
  iface_state_ = IFC_NONE;
  // Bump the iface id in case the phy isn't being released and we want to create another
  // iface.
  iface_id_++;
  iface_sme_channel_.reset();
}

zx_status_t Device::AddPhyDevice() {
  debugfn();
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "ralink-wlanphy";
  args.ctx = this;
  args.ops = &wlanphy_impl_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANPHY_IMPL;
  args.proto_ops = &wlanphy_impl_ops;

  return device_add(parent_, &args, &zxdev_);
}

zx_status_t Device::AddMacDevice() {
  debugfn();
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "ralink-wlanmac";
  args.ctx = this;
  args.ops = &wlanmac_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANMAC;
  args.proto_ops = &wlanmac_ops;

  zx_device_t* dev;
  zx_status_t status;
  status = device_add(zxdev_, &args, &dev);
  if (status == ZX_OK) {
    std::lock_guard<std::mutex> guard(lock_);
    wlanmac_dev_ = dev;
  }
  return status;
}

zx_status_t Device::Query(wlan_info_t* info) {
  debugfn();

  memset(info, 0, sizeof(*info));
  std::memcpy(info->mac_addr, mac_addr_, ETH_MAC_SIZE);

  info->supported_phys = WLAN_INFO_PHY_TYPE_DSSS | WLAN_INFO_PHY_TYPE_CCK |
                         WLAN_INFO_PHY_TYPE_OFDM | WLAN_INFO_PHY_TYPE_HT;
  info->mac_role = WLAN_INFO_MAC_ROLE_CLIENT;
  info->caps =
      WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE | WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME;
  info->driver_features = WLAN_INFO_DRIVER_FEATURE_TX_STATUS_REPORT;
  info->bands_count = 1;
  info->bands[0] = {
      .band = WLAN_INFO_BAND_2GHZ,
      // These hard-coded values are experimentally proven to work,
      // but does not necessarily reflect the true capabilities of the chipset.
      .ht_supported = true,
      .ht_caps =
          {
              .ht_capability_info = 0x016e,
              .ampdu_params = 0x17,
              .supported_mcs_set =
                  {
                      .bytes =
                          {
                              // Rx MCS bitmask
                              // Supported MCS values: 0-7, 32
                              0xff,
                              0x00,
                              0x00,
                              0x00,
                              0x01,
                              0x00,
                              0x00,
                              0x00,
                              0x00,
                              0x00,
                              0x00,
                              0x00,
                              // Tx parameters
                              // - Tx MCS set defined
                              // - Tx and Rx MCS set equal
                              // - Other fields set to zero due to the first two
                              0x01,
                              0x00,
                              0x00,
                              0x00,
                          },
                  },
              // No ext capabilities (PCO, MCS feedback, HT control, RD responder)
              .ht_ext_capabilities = 0x0000,
              // No Tx beamforming
              .tx_beamforming_capabilities = 0x00000000,
              // No antenna selection
              .asel_capabilities = 0x00,
          },
      .vht_supported = false,
      .vht_caps = {},
      // TODO(fxbug.dev/28891):
      // Unmark the "BasicRate" bit for the first 4 rates.
      // See IEEE Std 802.11-2016, 9.4.2.3 for encoding
      .rates = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c},
      .supported_channels =
          {
              .base_freq = 2407,
              .channels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14},
          },
  };

  if (rt_type_ == RT5592) {
    info->bands_count = 2;
    // Add MCS 8-15 to band 0
    info->bands[0].ht_caps.supported_mcs_set.bytes[1] = 0xff;
    info->bands[1] = {
        .band = WLAN_INFO_BAND_5GHZ,
        // See above for descriptions of these capabilities
        .ht_supported = true,
        .ht_caps =
            {
                .ht_capability_info = 0x016e,
                .ampdu_params = 0x17,
                .supported_mcs_set =
                    {
                        .bytes =
                            {
                                // Rx MCS bitmask
                                // Supported MCS values: 0-15, 32
                                0xff,
                                0xff,
                                0x00,
                                0x00,
                                0x01,
                                0x00,
                                0x00,
                                0x00,
                                0x00,
                                0x00,
                                0x00,
                                0x00,
                                // Tx parameters
                                0x01,
                                0x00,
                                0x00,
                                0x00,
                            },
                    },
                .ht_ext_capabilities = 0x0000,
                .tx_beamforming_capabilities = 0x00000000,
                .asel_capabilities = 0x00,
            },
        .vht_supported = false,
        .vht_caps = {},
        // TODO(fxbug.dev/28891):
        // See IEEE Std 802.11-2016, 9.4.2.3 for encoding
        .rates = {0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c},
        .supported_channels =
            {
                .base_freq = 5000,
                .channels =
                    {
                        36,  40,  44,  48,                                      // UNII-1
                        52,  56,  60,  64,                                      // UNII-2A
                        100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,  // UNII-2C
                        149, 153, 157, 161, 165,                                // UNII-3
                    },
            },
    };
  }
  return ZX_OK;
}

zx_status_t Device::Query(wlanphy_impl_info_t* info) { return Query(&info->wlan_info); }

zx_status_t Device::CreateIface(const wlanphy_impl_create_iface_req_t* req,
                                uint16_t* out_iface_id) {
  debugfn();

  {
    std::lock_guard<std::mutex> guard(lock_);
    if (phy_state_ != PHY_RUNNING) {
      return ZX_ERR_BAD_STATE;
    }
    if (iface_state_ != IFC_NONE) {
      return ZX_ERR_ALREADY_BOUND;
    }
    iface_state_ = IFC_CREATING;
  }

  zx_status_t status = AddMacDevice();
  {
    std::lock_guard<std::mutex> guard(lock_);
    *out_iface_id = iface_id_;
    iface_role_ = req->role;
    iface_state_ = IFC_RUNNING;
    iface_sme_channel_ = zx::channel(req->sme_channel);
  }

  if (status != ZX_OK) {
    errorf("could not add iface device err: %s\n", zx_status_get_string(status));
    std::lock_guard<std::mutex> guard(lock_);
    iface_state_ = IFC_NONE;
    return status;
  }

  {
    std::lock_guard<std::mutex> guard(lock_);
    *out_iface_id = iface_id_;
    iface_role_ = req->role;
    iface_state_ = IFC_RUNNING;
  }

  infof("iface added\n");
  return status;
}

zx_status_t Device::DestroyIface(uint16_t id) {
  debugfn();

  zx_device_t* dev;

  {
    std::lock_guard<std::mutex> guard(lock_);
    if (phy_state_ != PHY_RUNNING || iface_state_ != IFC_RUNNING) {
      return ZX_ERR_BAD_STATE;
    }

    if (id != iface_id_) {
      errorf("unknown iface id in destroy request: %u (expected %u)\n", id, iface_id_);
      return ZX_ERR_INVALID_ARGS;
    }

    iface_state_ = IFC_DESTROYING;
    dev = wlanmac_dev_;
  }

  // device_async_remove() may invoke MacRelease(), so we can't hold the lock across the call
  device_async_remove(dev);

  return ZX_OK;
}

zx_status_t Device::SetCountry(const wlanphy_country_t* country) {
  if (country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Nothing can be done in Ralink device driver level.
  debugf("SetCountry to [%s] not implemented\n",
         wlan::common::Alpha2ToStr(country->alpha2).c_str());
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::GetCountry(wlanphy_country_t* out_country) {
  if (out_country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Nothing can be done in Ralink device driver level.
  debugf("GetCountry not implemented\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanmacQuery(uint32_t options, wlanmac_info_t* info) {
  wlan_info_mac_role_t role;
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (phy_state_ != PHY_RUNNING || iface_state_ != IFC_RUNNING) {
      return ZX_ERR_BAD_STATE;
    }
    role = iface_role_;
  }
  zx_status_t status = Query(&info->ifc_info);
  info->ifc_info.mac_role = role;
  return status;
}

zx_status_t Device::WlanmacStart(const wlanmac_ifc_protocol_t* ifc, zx_handle_t* out_sme_channel) {
  debugfn();
  std::lock_guard<std::mutex> guard(lock_);

  if (phy_state_ != PHY_RUNNING) {
    return ZX_ERR_PEER_CLOSED;
  }
  if (wlanmac_proxy_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  if (!iface_sme_channel_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  zx_status_t status = LoadFirmware();
  if (status != ZX_OK) {
    errorf("failed to load firmware\n");
    return status;
  }

  // Initialize queues
  for (size_t i = 0; i < kReadReqCount; i++) {
    usb_request_t* req;
    zx_status_t status = usb_request_alloc(&req, kReadBufSize, rx_endpt_, parent_req_size_);
    if (status != ZX_OK) {
      errorf("failed to allocate rx usb request\n");
      goto err;
    }
    usb_request_complete_t complete = {
        .callback = &Device::ReadRequestComplete,
        .ctx = this,
    };
    usb_request_queue(&usb_, req, &complete);
  }
  // Only one TX queue for now
  {
    auto tx_endpt = tx_endpts_.front();
    for (size_t i = 0; i < kWriteReqCount; i++) {
      usb_request_t* req;
      zx_status_t status = usb_request_alloc(&req, kWriteBufSize, tx_endpt, parent_req_size_);
      if (status != ZX_OK) {
        errorf("failed to allocate tx usb request\n");
        goto err;
      }
      free_write_reqs_.push_back(req);
    }
  }

  status = EnableRadio();
  if (status != ZX_OK) {
    errorf("could not enable radio\n");
    goto err;
  }

  status = StartQueues();
  if (status != ZX_OK) {
    errorf("could not start queues\n");
    goto err;
  }

  status = SetupInterface();
  if (status != ZX_OK) {
    errorf("could not setup interface\n");
    goto err;
  }

  // TODO(tkilbourn): configure erp?
  // TODO(tkilbourn): configure tx

  {
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
  }

  status = McuCommand(MCU_WAKEUP, 0xff, 0, 2);
  if (status != ZX_OK) {
    errorf("error waking MCU err=%d\n", status);
    goto err;
  }

  {
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
  }

  status = SetRxFilter();
  if (status != ZX_OK) {
    return status;
  }

  wlanmac_proxy_ = ddk::WlanmacIfcProtocolClient(ifc);

  wlan_channel_t chan;
  chan.primary = 1;
  chan.cbw = WLAN_CHANNEL_BANDWIDTH__20;
  status = WlanmacSetChannel(0, &chan);

  StartInterruptPolling();

  *out_sme_channel = iface_sme_channel_.release();
  infof("wlan started\n");
  return ZX_OK;

err:
  iface_sme_channel_.reset();
  return status;
}

zx_status_t Device::StartInterruptPolling() {
  // Clear all interrupts and start thread.
  IntStatus intStatus;
  auto status = ReadRegister(&intStatus);
  CHECK_READ(INT_STATUS, status);
  status = WriteRegister(intStatus);
  CHECK_WRITE(INT_STATUS, status);

  status = zx::port::create(0, &interrupt_port_);
  if (status != ZX_OK) {
    errorf("could not create port: %d\n", status);
    return status;
  }

  status = zx::timer::create(ZX_TIMER_SLACK_LATE, ZX_CLOCK_MONOTONIC, &async_tx_interrupt_timer_);
  if (status != ZX_OK) {
    errorf("could not create async TX timer: %d\n", status);
    return status;
  }

  status = async_tx_interrupt_timer_.wait_async(interrupt_port_, kAsyncTxInterruptKey,
                                                ZX_TIMER_SIGNALED, 0);
  if (status != ZX_OK) {
    errorf("could not wait on async TX timer: %d\n", status);
    return status;
  }

  status = zx::timer::create(ZX_TIMER_SLACK_LATE, ZX_CLOCK_MONOTONIC, &tbtt_interrupt_timer_);
  if (status != ZX_OK) {
    errorf("could not create TBTT timer: %d\n", status);
    return status;
  }

  status =
      tbtt_interrupt_timer_.wait_async(interrupt_port_, kTbttInterruptKey, ZX_TIMER_SIGNALED, 0);
  if (status != ZX_OK) {
    errorf("could not wait on TBTT timer: %d\n", status);
    return status;
  }

  interrupt_thrd_ = std::thread(&Device::InterruptWorker, this);
  async_tx_interrupt_timer_.set(zx::deadline_after(kAsyncTxInterruptIdlePollInterval),
                                kAsyncTxInterruptIdlePollSlack);
  return ZX_OK;
}

void Device::StopInterruptPolling() {
  async_tx_interrupt_timer_.cancel();
  tbtt_interrupt_timer_.cancel();
  if (interrupt_thrd_.joinable()) {
    zx_port_packet_t pkt = {
        .key = kInterruptShutdownKey,
        .type = ZX_PKT_TYPE_USER,
    };
    interrupt_port_.queue(&pkt);
    interrupt_thrd_.join();
  }
}

zx_status_t Device::OnTxReportInterruptTimer() {
  TxStatFifo stat_fifo;
  TxStatFifoExt stat_fifo_ext;
  int tracked_tx_packet_count = 0;
  while (true) {
    // TX_STAT_FIFO_EXT must be read before TX_STAT_FIFO.
    auto status = ReadRegister(&stat_fifo_ext);
    CHECK_READ(TX_STAT_FIFO_EXT, status);
    status = ReadRegister(&stat_fifo);
    CHECK_READ(TX_STAT_FIFO, status);
    if (!stat_fifo.txq_vld()) {
      break;
    }

    const int packet_id = stat_fifo.txq_pid();
    if (packet_id == kInvalidTxPacketId) {
      continue;
    }

    std::lock_guard<std::mutex> guard(lock_);
    // MAC addr and tx vector instructed from upper layer was put in the queue, get them
    auto entry = RemoveTxStatsFifoEntry(packet_id);
    // Sometimes ralink may use non-zero packet_id on its own. Do not proceed. (fxbug.dev/29584)
    // A report is spurious if (1) We are not expecting any report. (2) the |packet_id| is not
    // among the ones we are expecting.
    // Note: If a spurious |packet_id| coincides with one that is expected, it will not be
    // detected. Instead it may render the valid tx status report (that comes later) spurious.
    bool spurious = tx_status_report_pending_ == 0 || !entry.in_use;
    if (spurious) {
      warnf(
          "spurious tx report: ralink [packet_id: %d, stat_fifo: %u, stat_fifo_ext: %u] "
          "memory [pending_rerport: %zu, entry.in_use: %d]\n",
          packet_id, stat_fifo.val(), stat_fifo_ext.val(), tx_status_report_pending_, entry.in_use);
      continue;
    }

    --tx_status_report_pending_;
    if (wlanmac_proxy_.is_valid()) {
      wlan_tx_status report{};
      status = BuildTxStatusReport(entry, stat_fifo, stat_fifo_ext, &report);
      if (status == ZX_OK) {
        wlanmac_proxy_.ReportTxStatus(&report);
      } else {
        warnf("cannot build tx status report: %s.\n", zx_status_get_string(status));
      }
    }
    // As a driver, we did our job of keeping track of packet tx status, regardless whether the
    // report is sent to |wlanmac_proxy_|.
    tracked_tx_packet_count++;
  }

  zx::duration poll_interval;
  zx::duration poll_slack;
  if (tracked_tx_packet_count < kTxStatsFifoSize / 4) {
    // Assume that the hardware is (relatively) idle, so we can afford to poll with a large
    // interval to catch any remaining long-running TX.
    poll_interval = kAsyncTxInterruptIdlePollInterval;
    poll_slack = kAsyncTxInterruptIdlePollSlack;
  } else {
    // Assume that the hardware is busy. We may see more TX completion soon.
    poll_interval = kAsyncTxInterruptBusyPollInterval;
    poll_slack = kAsyncTxInterruptBusyPollSlack;
  }
  async_tx_interrupt_timer_.set(zx::deadline_after(poll_interval), poll_slack);
  return ZX_OK;
}

zx_status_t Device::OnTbttInterruptTimer() {
  IntStatus intStatus;
  auto status = ReadRegister(&intStatus);
  CHECK_READ(INT_STATUS, status);

  const bool pre_tbtt_interrupt = intStatus.mac_int_1();
  if (pre_tbtt_interrupt) {
    {
      std::lock_guard<std::mutex> guard(lock_);
      if (wlanmac_proxy_.is_valid()) {
        wlanmac_proxy_.Indication(WLAN_INDICATION_PRE_TBTT);
      }
    }

    // Clear the pre-TBTT interrupt.
    intStatus.clear();
    intStatus.set_mac_int_1(1);
    status = WriteRegister(intStatus);
    CHECK_WRITE(INT_STATUS, status);

    // Wait for TBTT.
    zx::duration tbtt = RemainingTbttTime();
    tbtt_interrupt_timer_.set(zx::deadline_after(tbtt), zx::usec(1));
    return ZX_OK;
  }
  const bool tbtt_interrupt = intStatus.mac_int_0();
  if (tbtt_interrupt) {
    {
      // Due to Ralinks limitation of not being able to report actual beacon transmission,
      // TBTT is used instead.
      std::lock_guard<std::mutex> guard(lock_);
      if (wlanmac_proxy_.is_valid()) {
        wlanmac_proxy_.Indication(WLAN_INDICATION_BCN_TX_COMPLETE);
      }
    }

    // clear the tbtt interrupts.
    intStatus.clear();
    intStatus.set_mac_int_0(1);
    status = WriteRegister(intStatus);
    CHECK_WRITE(INT_STATUS, status);

    // wait for next pre-tbtt.
    const zx::duration pre_tbtt = RemainingTbttTime() - kPreTbttLeadTime;
    tbtt_interrupt_timer_.set(zx::deadline_after(pre_tbtt), zx::usec(1));
    return ZX_OK;
  }

  // pre-tbtt or tbtt interrupt is about to happen very soon, poll.
  tbtt_interrupt_timer_.set(zx::deadline_after(kTbttInterruptPollInterval), zx::usec(1));
  return ZX_OK;
}

zx_status_t Device::InterruptWorker() {
  const char kThreadName[] = "ralink-interrupt-worker";
  zx::thread::self()->set_property(ZX_PROP_NAME, kThreadName, sizeof(kThreadName));

  zx_port_packet_t pkt;
  for (;;) {
    zx::time timeout = zx::deadline_after(zx::sec(5));
    auto status = interrupt_port_.wait(timeout, &pkt);
    if (status == ZX_ERR_TIMED_OUT) {
      continue;
    } else if (status != ZX_OK) {
      if (status == ZX_ERR_BAD_HANDLE) {
        infof("interrupt port closed, exiting loop\n");
      } else {
        errorf("error waiting on interrupt port: %d\n", status);
      }
      break;
    }

    switch (pkt.type) {
      case ZX_PKT_TYPE_USER:
        if (pkt.key == kInterruptShutdownKey) {
          return ZX_OK;
        }
        break;
      case ZX_PKT_TYPE_SIGNAL_ONE: {
        if (pkt.key == kAsyncTxInterruptKey) {
          status = OnTxReportInterruptTimer();
          if (status != ZX_OK) {
            return status;
          }
          async_tx_interrupt_timer_.wait_async(interrupt_port_, kAsyncTxInterruptKey,
                                               ZX_TIMER_SIGNALED, 0);
        } else if (pkt.key == kTbttInterruptKey) {
          status = OnTbttInterruptTimer();
          if (status != ZX_OK) {
            return status;
          }
          tbtt_interrupt_timer_.wait_async(interrupt_port_, kTbttInterruptKey, ZX_TIMER_SIGNALED,
                                           0);
        }
        break;
      }
      default:
        errorf("unknown port packet type: %u\n", pkt.type);
        break;
    }
  }
  return ZX_OK;
}

zx::duration Device::RemainingTbttTime() {
  TbttTimer tbttTimer;
  auto status = ReadRegister(&tbttTimer);
  if (status != ZX_OK) {
    return zx::usec(0);
  }
  return zx::usec(tbttTimer.tbtt_timer() * 64);
}

void Device::WlanmacStop() {
  debugfn();
  std::lock_guard<std::mutex> guard(lock_);
  StopInterruptPolling();
  // This is safe even if we're already unbound.
  wlanmac_proxy_.clear();

  // TODO(tkilbourn) disable radios, stop queues, etc.
}

// TODO(fxbug.dev/29193): Extract into a common library.
uint16_t GetMacHdrLength(const uint8_t* buf, uint16_t len) {
  if (len < sizeof(wlan::FrameControl)) {
    return 0;
  }

  auto fc = reinterpret_cast<const wlan::FrameControl*>(buf);
  switch (fc->type()) {
    case wlan::FrameType::kManagement: {
      if (len < sizeof(wlan::MgmtFrameHeader)) {
        return 0;
      }
      auto hdr = reinterpret_cast<const wlan::MgmtFrameHeader*>(buf);
      return hdr->len();
    }
    case wlan::FrameType::kData: {
      if (len < sizeof(wlan::DataFrameHeader)) {
        return 0;
      }
      auto hdr = reinterpret_cast<const wlan::DataFrameHeader*>(buf);
      return hdr->len();
    }
    case wlan::FrameType::kControl: {
      if (len < sizeof(wlan::CtrlFrameHdr)) {
        return 0;
      }
      auto hdr = reinterpret_cast<const wlan::CtrlFrameHdr*>(buf);
      return hdr->len();
    }
    default:
      warnf("cannot compute MAC header length for unknown frame type: %u\n", fc->type());
      return 0;
  }
}

size_t Device::WriteBulkout(uint8_t* dest, const wlan_tx_packet_t& wlan_pkt) {
  // Write and return the length of
  // MPDU Header + L2Pad + MSDU + Bulkout Aggregation Pad + Bulkout Aggregation Tail Pad

  ZX_DEBUG_ASSERT(dest != nullptr);

  const auto head_data = static_cast<const uint8_t*>(wlan_pkt.packet_head.data_buffer);
  auto head_len = wlan_pkt.packet_head.data_size;
  uint16_t frame_hdr_len = GetMacHdrLength(head_data, head_len);
  // If the header length is invalid use the entire length as header length.
  // This is just as problematic as before but we need to put proper error handling
  // here first, before we can address this problem.
  if (frame_hdr_len == 0) {
    frame_hdr_len = head_len;
  }

  size_t dest_offset = 0;
  auto l2pad_len = RoundUp(frame_hdr_len, 4) - frame_hdr_len;

  // TODO(fxbug.dev/29200): Augument BulkoutAggregation with pointers and lengths.
  if (l2pad_len == 0) {
    std::memcpy(dest, head_data, head_len);
    dest_offset += head_len;

  } else {
    // Insert L2pad between MPDU header and MSDU
    auto msdu = head_data + frame_hdr_len;
    std::memcpy(dest, head_data, frame_hdr_len);
    dest_offset += frame_hdr_len;
    std::memset(dest + dest_offset, 0, l2pad_len);  // L2padding with zeros
    dest_offset += l2pad_len;
    std::memcpy(dest + dest_offset, msdu, head_len - frame_hdr_len);
    dest_offset += head_len - frame_hdr_len;
  }

  uint16_t tail_len_eff = 0;

  if (wlan_pkt.packet_tail_list != nullptr) {
    auto tail = wlan_pkt.packet_tail_list;
    uint16_t tail_offset = wlan_pkt.tail_offset;
    const uint8_t* tail_data = static_cast<const uint8_t*>(tail->data_buffer) + tail_offset;
    tail_len_eff = tail->data_size - tail_offset;
    std::memcpy(dest + dest_offset, tail_data, tail_len_eff);
    dest_offset += tail_len_eff;
  }

  // Append Bulkout Aggregate padding and its Tail padding
  auto payload_len = head_len + tail_len_eff + l2pad_len;
  auto aggregate_pad_len = RoundUp(payload_len, 4) - payload_len;
  auto extra_pad_len = aggregate_pad_len + GetBulkoutAggrTailLen();
  std::memset(dest + payload_len, 0, extra_pad_len);
  payload_len += extra_pad_len;

  return payload_len;
}

void DumpWlanTxInfo(const wlan_tx_info_t& txinfo) {
  debugf("txinfo: tx_flags 0x%04x valid_fields 0x%04x phy %u cbw %u tx_vector_idx %u mcs %u\n",
         txinfo.tx_flags, txinfo.valid_fields, txinfo.phy, txinfo.cbw, txinfo.tx_vector_idx,
         txinfo.mcs);
}

void DumpTxwi(BulkoutAggregation* aggr) {
  if (aggr == nullptr)
    return;

  Txwi0& txwi0 = aggr->txwi0;
  Txwi1& txwi1 = aggr->txwi1;

  debugf("txwi:   frag %u mmps %u cfack %u ts %u ampdu %u mpdu_density %u txop %u mcs 0x%02x\n",
         txwi0.frag(), txwi0.mmps(), txwi0.cfack(), txwi0.ts(), txwi0.ampdu(), txwi0.mpdu_density(),
         txwi0.txop(), txwi0.mcs());
  debugf("        bw %u sgi %u stbc %u phy_mode %u ack %u nseq %u ba_win_size %u wcid 0x%02x\n",
         txwi0.bw(), txwi0.sgi(), txwi0.stbc(), txwi0.phy_mode(), txwi1.ack(), txwi1.nseq(),
         txwi1.ba_win_size(), txwi1.wcid());
  debugf("        mpdu_total_byte_count %u tx_packet_id 0x%x\n", txwi1.mpdu_total_byte_count(),
         txwi1.tx_packet_id());
}

zx_status_t Device::WlanmacEnableBeaconing(uint32_t options, bool enabled) {
  return EnableHwBcn(enabled);
}

zx_status_t Device::WlanmacConfigureBeacon(uint32_t options, const wlan_tx_packet_t* bcn_pkt) {
  ZX_DEBUG_ASSERT(bcn_pkt != nullptr);
  if (bcn_pkt == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto aggr_payload_len = GetBulkoutAggrPayloadLen(*bcn_pkt);
  size_t req_len = sizeof(TxInfo) + aggr_payload_len + GetBulkoutAggrTailLen();

  if (req_len > kMaxBeaconSizeByte) {
    errorf("Beacon exceeds limit of %zu bytes: %zu\n", kMaxBeaconSizeByte, req_len);
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[req_len]);
  auto aggr = reinterpret_cast<BulkoutAggregation*>(buf.get());
  auto status = FillAggregation(aggr, bcn_pkt, kInvalidTxPacketId, aggr_payload_len);
  if (status != ZX_OK) {
    errorf("could not fill usb request packet: %d\n", status);
    return status;
  }

  BcnOffset0 bcnOffset0;
  status = ReadRegister(&bcnOffset0);
  CHECK_READ(BCN_OFFSET_0, status);

  // The Beacon layout in shared memory does not include TxInfo. Hence, skip it.
  auto data = buf.get() + sizeof(TxInfo);
  uint8_t* data_end = data + req_len - sizeof(TxInfo);
  uint16_t index = BEACON_BASE + bcnOffset0.bcn0_offset() * kBeaconOffsetFactorByte;

  // Write Beacon in chunks to the device.
  const size_t max_chunk_size = 64;
  for (; data_end - data > 0; data += max_chunk_size, index += max_chunk_size) {
    size_t writing = std::min(max_chunk_size, static_cast<size_t>(data_end - data));
    status = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite, 0, index,
                             ZX_TIME_INFINITE, data, writing);
    if (status != ZX_OK) {
      std::printf("error writing Beacon to offset 0x%4x: %d\n", index, status);
      return ZX_ERR_IO;
    }
  }

  // Ensure hardware Beacons are activated.
  EnableHwBcn(true);

  return ZX_OK;
}

zx_status_t Device::WlanmacQueueTx(uint32_t options, wlan_tx_packet_t* wlan_pkt) {
  ZX_DEBUG_ASSERT(wlan_pkt != nullptr);

  auto aggr_payload_len = GetBulkoutAggrPayloadLen(*wlan_pkt);
  size_t usb_request_len = sizeof(TxInfo) + aggr_payload_len + GetBulkoutAggrTailLen();
  if (usb_request_len > kWriteBufSize) {
    errorf("usb request buffer size insufficient for tx packet -- %zu bytes needed\n",
           usb_request_len);
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  usb_request_t* req = nullptr;
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (free_write_reqs_.empty()) {
      // No free write requests! Drop the packet. Error code matches ath10k.
      // TODO(tkilbourn): buffer the wlan_tx_packet_ts.
      static size_t failed_writes = 0;
      if (failed_writes++ % 100 == 0) {
        warnf("dropped %zu tx (no free usb requests)\n", failed_writes);
      }
      return ZX_ERR_NO_RESOURCES;
    }
    req = free_write_reqs_.back();
    free_write_reqs_.pop_back();
  }
  ZX_DEBUG_ASSERT(req != nullptr);

  BulkoutAggregation* aggr;
  auto status = usb_request_mmap(req, reinterpret_cast<void**>(&aggr));
  if (status != ZX_OK) {
    errorf("could not map usb request: %d\n", status);
    std::lock_guard<std::mutex> guard(lock_);
    free_write_reqs_.push_back(req);
    return status;
  }

  // Record the packet to be transmitted in the packet TX stats FIFO.
  const int packet_id = AddTxStatsFifoEntry(*wlan_pkt);
  if (packet_id != kInvalidTxPacketId) {
    // The TX hardware will become busy.  Begin firing the timer immediately.
    async_tx_interrupt_timer_.set(zx::deadline_after(zx::msec(0)), kAsyncTxInterruptBusyPollSlack);
  }

  status = FillAggregation(aggr, wlan_pkt, packet_id, aggr_payload_len);
  if (status != ZX_OK) {
    errorf("could not fill usb request packet: %d\n", status);
    return status;
  }

  // Send the whole thing
  req->header.length = usb_request_len;
  usb_request_complete_t complete = {
      .callback = &Device::WriteRequestComplete,
      .ctx = this,
  };
  usb_request_queue(&usb_, req, &complete);

#if RALINK_DUMP_TX
  debugf("[Ralink] Outbound WLAN packet meta info\n");
  DumpWlanTxInfo(wlan_pkt->info);
  DumpTxwi(aggr);
  DumpLengths(*wlan_pkt, aggr, req);
#endif  // RALINK_DUMP_TX

  return ZX_OK;
}

Device::TxStatsFifoEntry Device::RemoveTxStatsFifoEntry(int packet_id) {
  // The entry in tx_stats_fifo_ is indexed by packet ID.
  ZX_DEBUG_ASSERT(kInvalidTxPacketId < packet_id && packet_id < kTxStatsFifoSize);
  TxStatsFifoEntry entry = tx_stats_fifo_[packet_id];

  tx_stats_fifo_[packet_id].in_use = false;
  return entry;
}

int Device::AddTxStatsFifoEntry(const wlan_tx_packet_t& wlan_pkt) {
  if ((wlan_pkt.info.valid_fields & WLAN_TX_INFO_VALID_TX_VECTOR_IDX) == 0) {
    return kInvalidTxPacketId;
  }

  std::lock_guard<std::mutex> guard(lock_);
  // 0 is reserved as invalid packet ID (e.g. for beacon frames); the hardware appears to
  // ignore the TX stats registers when packet ID 0 is used. Hence, tx_stats_fifo_counter_
  // iterates on the interval [0, kTxStatsFifoSize - 1) so we generate TX packet IDs on the
  // interval [1, kTxStatsFifoSize).
  const int packet_id = tx_stats_fifo_counter_ + 1;
  static size_t num_overrun = 0;
  static size_t max_overrun = 0;
  TxStatsFifoEntry& tx_stats = tx_stats_fifo_[packet_id];
  if (tx_stats_fifo_[packet_id].in_use) {
    ++num_overrun;
    return kInvalidTxPacketId;
  }
  if (num_overrun > 0) {
    if (num_overrun > max_overrun) {
      debugmstl("Recovered from a tx_stats_fifo_ overrun. max backlog increased: %zu -> %zu\n",
                max_overrun, num_overrun);
      max_overrun = num_overrun;
    }
    num_overrun = 0;
  }
  tx_stats_fifo_counter_ = (tx_stats_fifo_counter_ + 1) % (kTxStatsFifoSize - 1);
  const auto bytes = static_cast<const uint8_t*>(wlan_pkt.packet_head.data_buffer);
  auto addr1_offset = bytes + kMacHdrAddr1Offset;
  std::copy(addr1_offset, addr1_offset + wlan::common::kMacAddrLen, tx_stats.peer_addr);
  tx_stats.tx_vector_idx = wlan_pkt.info.tx_vector_idx;
  tx_stats.in_use = true;
  ++tx_status_report_pending_;
  return packet_id;
}

::wlan::TxVector FromStatFifoRegister(const TxStatFifo& stat_fifo) {
  return ::wlan::TxVector{
      .phy = static_cast<wlan_info_phy_type_t>(ralink_phy_to_ddk_phy(stat_fifo.txq_phy())),
      .gi = static_cast<uint8_t>(stat_fifo.txq_sgi() == 1 ? WLAN_GI__400NS : WLAN_GI__800NS),
      .cbw = static_cast<uint8_t>(stat_fifo.txq_bw() == 1 ? WLAN_CHANNEL_BANDWIDTH__40
                                                          : WLAN_CHANNEL_BANDWIDTH__20),
      .mcs_idx = stat_fifo.txq_mcs(),
  };
}

zx_status_t FillTxStatusEntries(tx_vec_idx_t vec_idx_first, tx_vec_idx_t vec_idx_last,
                                const TxStatFifo& stat_fifo, const uint8_t num_retries,
                                wlan_tx_status_entry_t* entries) {
  if (vec_idx_first < vec_idx_last) {
    debugmstl("invalid retry chain. first: %u < last: %u\n", vec_idx_first, vec_idx_last);
    ZX_DEBUG_ASSERT(0);
    return ZX_ERR_INTERNAL;
  }

  if (num_retries == 1) {
    // report first and last tx vectors only when there is only one retry
    // because in such cases ralink doesn't always follow the fallback-by-one rule.
    entries[0].tx_vector_idx = vec_idx_first;
    entries[0].attempts = 1;
    entries[1].tx_vector_idx = vec_idx_last;
    entries[1].attempts = 1;
    return ZX_OK;
  }

  uint8_t idx = 0;
  for (idx = 0; idx < WLAN_TX_STATUS_MAX_ENTRY && vec_idx_last + idx <= vec_idx_first; ++idx) {
    entries[idx].tx_vector_idx = vec_idx_first - idx;
    entries[idx].attempts = 1;
  }
  --idx;

  if (vec_idx_first != vec_idx_last + idx) {
    return ZX_ERR_INTERNAL;
  } else if (num_retries >= idx) {
    entries[idx].attempts += num_retries - idx;
  } else {
    // Occasionally the number of MCS fall-back can be larger than the number of
    // retries, when that happens, assume the retry chain is correct and the retry count
    // is incorrect. This seems to conform to the experimental results.
    debugmstl(
        "error parsing retry chain. "
        "retry_actual: %u, retry_calculated: %d, success: %u, stat_fifo: %x, \n",
        num_retries, idx, stat_fifo.txq_ok(), stat_fifo.val());
  }

  return ZX_OK;
}

zx_status_t Device::BuildTxStatusReport(const TxStatsFifoEntry& entry, const TxStatFifo& stat_fifo,
                                        const TxStatFifoExt& stat_fifo_ext,
                                        wlan_tx_status* report) {
  if (report == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  ::wlan::TxVector vec_last = FromStatFifoRegister(stat_fifo);
  tx_vec_idx_t idx_last;
  zx_status_t status = vec_last.ToIdx(&idx_last);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  tx_vec_idx_t idx_first = entry.tx_vector_idx;
  ::wlan::TxVector vec_first;
  status = ::wlan::TxVector::FromIdx(idx_first, &vec_first);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  // Start by clearing out report, setting MAC address and success
  std::memset(report, 0, sizeof(wlan_tx_status));
  std::copy(std::begin(entry.peer_addr), std::end(entry.peer_addr), std::begin(report->peer_addr));
  report->success = stat_fifo.txq_ok() == 1;
  uint8_t num_retries = stat_fifo_ext.txq_rty_cnt();

  if (num_retries == 0) {
    // No retry -> The transmission is attempted at the final tx vector instead of the intended.
    if (!report->success) {
      debugmstl("error in tx_stat_info: txq_rty_cnt == 0 but txq_ok == 0.\n");
    }
    idx_first = idx_last;
    vec_first = vec_last;
  } else if (!IsEqualExceptMcs(vec_first, vec_last)) {
    // auto fallback changed something other than mcs,
    // In this case only the first and last attempt can be used
    // setting num_retries to 1 to discard the rest
    num_retries = 1;
  }

  return FillTxStatusEntries(idx_first, idx_last, stat_fifo, num_retries, report->tx_status_entry);
}

zx_status_t Device::FillAggregation(BulkoutAggregation* aggr, const wlan_tx_packet_t* wlan_pkt,
                                    int packet_id, size_t aggr_payload_len) {
  // FillAggregation() fills up Aggregation Header, Payload, and its Tail marker.
  // Header is in the form of TxInfo. Its length field is to carry the length
  // of the Aggregation Payload.
  // Aggregation Payload consists of TXWI, MPDU header, L2pad, MSDU, and Aggregation Padding.
  // Though the name suggests 'aggregation', this code always prepared only one unit.
  // As a result, Tail marker of 4 bytes of zero padding is always appended.

  ZX_DEBUG_ASSERT(wlan_pkt != nullptr);

  std::memset(aggr, 0, sizeof(TxInfo) + GetTxwiLen());

  // TxInfo
  aggr->tx_info.set_aggr_payload_len(aggr_payload_len);
  // TODO(tkilbourn): set these more appropriately
  const bool protected_frame = (wlan_pkt->info.tx_flags & WLAN_TX_INFO_FLAGS_PROTECTED);
  uint8_t wiv = !protected_frame;
  aggr->tx_info.set_wiv(wiv);
  aggr->tx_info.set_qsel(2);

  // TxWI
  Txwi0& txwi0 = aggr->txwi0;
  txwi0.set_frag(0);
  txwi0.set_mmps(0);
  txwi0.set_cfack(0);
  txwi0.set_ts(0);  // TODO(porce): Set it 1 for beacon or proberesp.

  // TODO(fxbug.dev/29011): Use the outcome of the association negotiation
  txwi0.set_ampdu(1);
  txwi0.set_mpdu_density(Txwi0::kFourUsec);  // Aruba
  txwi0.set_txop(Txwi0::kHtTxop);

  uint8_t phy_mode = ddk_phy_to_ralink_phy(WLAN_INFO_PHY_TYPE_OFDM);  // Default
  if (wlan_pkt->info.valid_fields & WLAN_TX_INFO_VALID_PHY) {
    phy_mode = ddk_phy_to_ralink_phy(wlan_pkt->info.phy);
  }
  txwi0.set_phy_mode(phy_mode);

  uint8_t mcs = kMaxOfdmMcs;  // this is the same as the max HT mcs
  if (wlan_pkt->info.valid_fields & WLAN_TX_INFO_VALID_MCS) {
    mcs = mcs_to_ralink_mcs(phy_mode, wlan_pkt->info.mcs);
  }
  txwi0.set_mcs(mcs);

  wlan_channel_bandwidth_t cbw = WLAN_CHANNEL_BANDWIDTH__20;
  if (wlan_pkt->info.valid_fields & WLAN_TX_INFO_VALID_CHAN_WIDTH) {
    cbw = wlan_pkt->info.cbw;
    // TODO(porce): Investigate how to configure txwi differently
    // for CBW40ABOVE and CBW40BELOW
  }
  txwi0.set_bw(cbw == WLAN_CHANNEL_BANDWIDTH__20 ? k20MHz : k40MHz);

  txwi0.set_sgi(0);   // Long guard interval for robustness
  txwi0.set_stbc(0);  // TODO(porce): Define the value.

  // The frame header is always in the packet head.
  // If the frame requires protection, lookup the matching WCID.
  uint8_t wcid = kWcidUnknown;
  if (protected_frame) {
    if (wlan_pkt->packet_head.data_size >= kMacHdrAddr1Offset + ETH_MAC_SIZE) {
      auto frame_data = static_cast<const uint8_t*>(wlan_pkt->packet_head.data_buffer);
      auto addr1 = wlan::common::MacAddr(frame_data + kMacHdrAddr1Offset);

      auto wcid_lookup = GetWcid(addr1);
      if (wcid_lookup) {
        wcid = wcid_lookup.value();
      } else {
        auto fc = reinterpret_cast<const wlan::FrameControl*>(frame_data);
        warnf("no WCID found for protected frame: %u %u\n", fc->type(), fc->subtype());
      }
    }
  }

  Txwi1& txwi1 = aggr->txwi1;
  txwi1.set_ack(GetRxAckPolicy(*wlan_pkt));
  txwi1.set_nseq(0);

  // TODO(porce): Study if BlockAck window size can change without resetting the radio
  // upon completing the BlockAck session negotiation at MLME layer.
  // Separate the workflow for the BlockAck originator case from the responder case.
  txwi1.set_ba_win_size(64);
  txwi1.set_wcid(wcid);

  size_t mpdu_len = GetMpduLen(*wlan_pkt);
  txwi1.set_mpdu_total_byte_count(mpdu_len);
  txwi1.set_tx_packet_id(packet_id);

  Txwi2& txwi2 = aggr->txwi2;
  txwi2.set_iv(0);

  Txwi3& txwi3 = aggr->txwi3;
  txwi3.set_eiv(0);

  // Payload
  uint8_t* aggr_payload = aggr->payload(rt_type_);
  WriteBulkout(aggr_payload, *wlan_pkt);

  return ZX_OK;
}

zx_status_t Device::EnableHwBcn(bool active) {
  BcnTimeCfg bcnTimeCfg;
  auto status = ReadRegister(&bcnTimeCfg);
  CHECK_READ(BCN_TIME_CFG, status);
  if (bcnTimeCfg.bcn_tx_en() != active) {
    bcnTimeCfg.set_bcn_tx_en(active);
    bcnTimeCfg.set_tbtt_timer_en(active);
    status = WriteRegister(bcnTimeCfg);
    CHECK_WRITE(BCN_TIME_CFG, status);

    IntTimerEn intTimerEn;
    status = ReadRegister(&intTimerEn);
    CHECK_READ(interrupt_timer_EN, status);
    intTimerEn.set_pre_tbtt_int_en(active);
    status = WriteRegister(intTimerEn);
    CHECK_WRITE(interrupt_timer_EN, status);

    if (active) {
      const zx::duration pre_tbtt = RemainingTbttTime() - kPreTbttLeadTime;
      tbtt_interrupt_timer_.set(zx::deadline_after(pre_tbtt), zx::usec(1));
    } else {
      tbtt_interrupt_timer_.cancel();
    }
  }
  return ZX_OK;
}

zx_status_t Device::WlanmacSetChannel(uint32_t options, const wlan_channel_t* chan) {
  // Beware the multiple different return paths with different recovery requirements.

  ZX_DEBUG_ASSERT(chan != nullptr);
  debugf("channel change: from %s to %s attempting..\n", wlan::common::ChanStr(cfg_chan_).c_str(),
         wlan::common::ChanStr(*chan).c_str());

  switch (chan->cbw) {  // parameter sanity check
    case WLAN_CHANNEL_BANDWIDTH__20:
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      break;
    default:
      errorf("%s: unsupported CBW %u\n", __FUNCTION__, chan->cbw);
      return ZX_ERR_NOT_SUPPORTED;
      break;
  }

  zx_status_t status;
  if (options != 0) {
    status = ZX_ERR_INVALID_ARGS;
    goto setchan_failure;
  }

  status = StopRxQueue();
  if (status != ZX_OK) {
    // TODO(porce): Recover fully if the RxQueue stopped in a half-way.
    errorf("could not stop rx queue (status %d)\n", status);
    goto setchan_failure;
  }

  status = ConfigureChannel(*chan);
  if (status != ZX_OK) {
    errorf("failed in channel configuration (status %d)\n", status);
    goto setchan_recover;
  }

  status = ConfigureTxPower(*chan);
  if (status != ZX_OK) {
    errorf("failed in txpower configuration (status %d)\n", status);
    goto setchan_recover;
  }

  status = StartQueues();
  if (status != ZX_OK) {
    errorf("could not start queues (status %d)\n", status);
    // Try one more time to start queues before returning.
    goto setchan_recover;
  }

  debugf("channel change: from %s to %s succeeded\n", wlan::common::ChanStr(cfg_chan_).c_str(),
         wlan::common::ChanStr(*chan).c_str());
  cfg_chan_ = *chan;
  return ZX_OK;

setchan_recover : {
  zx_status_t recover_status = StartQueues();
  if (recover_status != ZX_OK) {
    errorf("could not start queues (recover status %d)\n", recover_status);
  }
}
  // fall-through to setchan_failure

setchan_failure:
  errorf("channel change: from %s to %s failed (status %d)\n",
         wlan::common::ChanStr(cfg_chan_).c_str(), wlan::common::ChanStr(*chan).c_str(), status);

  return status;
}

zx_status_t Device::SetBss(const uint8_t* bssid) {
  MacBssidDw0 bss0;
  MacBssidDw1 bss1;
  bss0.set_mac_addr_0(bssid[0]);
  bss0.set_mac_addr_1(bssid[1]);
  bss0.set_mac_addr_2(bssid[2]);
  bss0.set_mac_addr_3(bssid[3]);
  bss1.set_mac_addr_4(bssid[4]);
  bss1.set_mac_addr_5(bssid[5]);
  bss1.set_multi_bss_mode(MultiBssIdMode::k1BssIdMode);

  auto status = WriteRegister(bss0);
  CHECK_WRITE(BSSID_DW0, status);
  status = WriteRegister(bss1);
  CHECK_WRITE(BSSID_DW1, status);

  memcpy(bssid_, bssid, ETH_MAC_SIZE);

  return ZX_OK;
}

zx_status_t Device::WlanmacConfigureBss(uint32_t options, const wlan_bss_config_t* config) {
  if (options != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Some APs balk at being asked to associate immediately after disassociation, so add a
  // short quiesce period if we're reconnecting to one we just disconnected from (fxbug.dev/28757).
  if (!std::memcmp(config->bssid, last_disconnect_bssid_, ETH_MAC_SIZE)) {
    zx::time curr_time = zx::clock::get_monotonic();
    if (curr_time - last_disconnect_time_ < kDisconnectQuiescePeriod) {
      sleep_for((last_disconnect_time_ + kDisconnectQuiescePeriod) - curr_time);
    }
  }

  zx_status_t status = SetBss(config->bssid);
  if (status != ZX_OK) {
    return status;
  }

  // Additional configurations when BSS is managed by this device.
  // This will allow offloading Beacon management to hardware.
  if (!config->remote) {
    BcnOffset0 offset;
    offset.clear();
    offset.set_bcn0_offset(0xE0);
    auto status = WriteRegister(offset);
    CHECK_WRITE(BCN_OFFSET_0, status);

    BcnTimeCfg bcnTimeCfg;
    bcnTimeCfg.set_bcn_intval(1600);
    bcnTimeCfg.set_tsf_timer_en(1);
    bcnTimeCfg.set_tsf_sync_mode(3);
    bcnTimeCfg.set_tbtt_timer_en(1);
    bcnTimeCfg.set_bcn_tx_en(0);
    status = WriteRegister(bcnTimeCfg);
    CHECK_WRITE(BCN_TIME_CFG, status);

    TbttSyncCfg tsc;
    tsc.set_tbtt_adjust(0);
    tsc.set_bcn_exp_win(32);
    tsc.set_bcn_aifsn(1);
    tsc.set_bcn_cwmin(0);
    status = WriteRegister(tsc);
    CHECK_WRITE(TBTT_SYNC_CFG, status);

    // TODO(hahnr): Implement a less naive configuration for basic rate and xifs time.
    LegacyBasicRate lbr;
    lbr.set_rate_1mbps(1);
    lbr.set_rate_2mbps(1);
    lbr.set_rate_5_5mbps(1);
    lbr.set_rate_11mbps(1);
    status = WriteRegister(lbr);
    CHECK_WRITE(LEGACY_BASIC_RATE, status);

    XifsTimeCfg xtc;
    xtc.set_cck_sifs_time(16);
    xtc.set_ofdm_sifs_time(16);
    xtc.set_ofdm_xifs_time(4);
    xtc.set_eifs_time(342);
    xtc.set_bb_rxend_en(1);
    status = WriteRegister(xtc);
    CHECK_WRITE(XIFS_TIME_CFG, status);
  }

  return ZX_OK;
}

// Maps IEEE cipher suites to vendor specific cipher representations, called KeyMode.
// The word 'KeyMode' is intentionally used to prevent mixing this vendor specific cipher
// representation with IEEE's vendor specific cipher suites as specified in the last row of
// IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-131.
// The KeyMode identifies a vendor supported cipher by a number and not as IEEE does by a type
// and OUI.
KeyMode Device::MapIeeeCipherSuiteToKeyMode(const uint8_t cipher_oui[3], uint8_t cipher_type) {
  if (memcmp(cipher_oui, wlan::common::cipher::kStandardOui, 3)) {
    return KeyMode::kUnsupported;
  }

  switch (cipher_type) {
    case wlan::common::cipher::kTkip:
      return KeyMode::kTkip;
    case wlan::common::cipher::kCcmp128:
      return KeyMode::kAes;
    default:
      return KeyMode::kUnsupported;
  }
}

uint8_t Device::DeriveSharedKeyIndex(uint8_t bss_idx, uint8_t key_idx) {
  return bss_idx * kGroupKeysPerBss + key_idx;
}

zx_status_t Device::WriteKey(const uint8_t key[], size_t key_len, uint16_t index, KeyMode mode) {
  KeyEntry keyEntry = {};
  switch (mode) {
    case KeyMode::kNone: {
      if (key_len != kNoProtectionKeyLen || key != nullptr) {
        errorf("invalid key length %zu; expected %hhu\n", key_len, kNoProtectionKeyLen);
        return ZX_ERR_INVALID_ARGS;
      }
      // No need for copying the key since the key should be zeroed in this KeyMode.
      break;
    }
    case KeyMode::kTkip: {
      if (key_len != wlan::common::cipher::kTkipKeyLenBytes) {
        errorf("invalid TKIP key length %zu; expected %hhu\n", key_len,
               wlan::common::cipher::kTkipKeyLenBytes);
        return ZX_ERR_INVALID_ARGS;
      }

      memcpy(keyEntry.key, key, wlan::common::cipher::kTkipKeyLenBytes);
      break;
    }
    case KeyMode::kAes: {
      if (key_len != wlan::common::cipher::kCcmp128KeyLenBytes) {
        errorf("invalid CCMP-128 key length %zu; expected %hhu\n", key_len,
               wlan::common::cipher::kCcmp128KeyLenBytes);
        return ZX_ERR_INVALID_ARGS;
      }

      memcpy(keyEntry.key, key, wlan::common::cipher::kCcmp128KeyLenBytes);
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite, 0, index,
                                ZX_TIME_INFINITE, &keyEntry, sizeof(keyEntry));
  if (status != ZX_OK) {
    std::printf("Error writing Key Entry: %d\n", status);
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t Device::WritePairwiseKey(uint8_t wcid, const uint8_t key[], size_t key_len,
                                     KeyMode mode) {
  uint16_t index = PAIRWISE_KEY_BASE + wcid * sizeof(KeyEntry);
  return WriteKey(key, key_len, index, mode);
}

zx_status_t Device::WriteSharedKey(uint8_t skey, const uint8_t key[], size_t key_len,
                                   KeyMode mode) {
  if (skey > kMaxSharedKeys) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint16_t index = SHARED_KEY_BASE + skey * sizeof(KeyEntry);
  return WriteKey(key, key_len, index, mode);
}

zx_status_t Device::WriteWcid(uint8_t wcid, const uint8_t mac[]) {
  RxWcidEntry wcidEntry = {};
  memset(wcidEntry.ba_sess_mask, 0xFF, sizeof(wcidEntry.ba_sess_mask));
  memcpy(wcidEntry.mac, mac, sizeof(wcidEntry.mac));

  uint16_t index = RX_WCID_BASE + wcid * sizeof(wcidEntry);
  auto status = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite, 0, index,
                                ZX_TIME_INFINITE, &wcidEntry, sizeof(wcidEntry));
  if (status != ZX_OK) {
    std::printf("Error writing WCID Entry: %d\n", status);
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t Device::WriteWcidAttribute(uint8_t bss_idx, uint8_t wcid, KeyMode mode, KeyType type) {
  WcidAttrEntry wcidAttr = {};
  wcidAttr.set_keyType(type);
  wcidAttr.set_keyMode(mode & 0x07);
  wcidAttr.set_keyModeExt((mode & 0x08) >> 3);
  wcidAttr.set_bssIdx(bss_idx & 0x07);
  wcidAttr.set_bssIdxExt((bss_idx & 0x08) >> 3);
  wcidAttr.set_rxUsrDef(4);
  auto value = wcidAttr.val();
  auto status = WriteRegister(WCID_ATTR_BASE + wcid * sizeof(value), value);
  CHECK_WRITE(WCID_ATTRIBUTE, status);
  return ZX_OK;
}

zx_status_t Device::ResetWcid(uint8_t wcid, uint8_t skey, uint8_t key_type) {
  WriteWcid(wcid, wlan::common::kZeroMac.byte);
  WriteWcidAttribute(0, wcid, KeyMode::kNone, KeyType::kSharedKey);
  ResetIvEiv(wcid, 0, KeyMode::kNone);

  switch (key_type) {
    case WLAN_KEY_TYPE_PAIRWISE: {
      WritePairwiseKey(wcid, nullptr, kNoProtectionKeyLen, KeyMode::kNone);
      break;
    }
    case WLAN_KEY_TYPE_GROUP: {
      WriteSharedKey(skey, nullptr, kNoProtectionKeyLen, KeyMode::kNone);
      WriteSharedKeyMode(skey, KeyMode::kNone);
      break;
    }
    default:
      break;
  }
  return ZX_OK;
}

zx_status_t Device::ResetIvEiv(uint8_t wcid, uint8_t key_id, KeyMode mode) {
  IvEivEntry ivEntry = {};
  switch (mode) {
    case KeyMode::kNone:
      break;
    case KeyMode::kTkip:
      // IEEE Std.802.11-2016, 12.5.2.2
      // fallthrough
    case KeyMode::kAes:
      // IEEE Std.802.11-2016, 12.5.3.2
      ivEntry.iv[3] = 0x20 | key_id << 6;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  uint16_t index = IV_EIV_BASE + wcid * sizeof(ivEntry);
  auto status = usb_control_out(&usb_, (USB_DIR_OUT | USB_TYPE_VENDOR), kMultiWrite, 0, index,
                                ZX_TIME_INFINITE, &ivEntry, sizeof(ivEntry));
  if (status != ZX_OK) {
    std::printf("Error writing IVEIV Entry: %d\n", status);
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t Device::WriteSharedKeyMode(uint8_t skey, KeyMode mode) {
  if (skey > kMaxSharedKeys) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  SharedKeyModeEntry keyMode = {};

  uint8_t skey_idx = skey % kKeyModesPerSharedKeyMode;
  uint16_t offset = SHARED_KEY_MODE_BASE + (skey / kKeyModesPerSharedKeyMode) * 4;

  // Due to key rotation, read in existing value.
  auto status = ReadRegister(offset, &keyMode.value);
  CHECK_READ(SHARED_KEY_MODE, status);

  status = keyMode.set(skey_idx, mode);
  if (status != ZX_OK) {
    return status;
  }

  status = WriteRegister(offset, keyMode.value);
  CHECK_WRITE(SHARED_KEY_MODE, status);
  return ZX_OK;
}

std::optional<uint8_t> Device::GetWcid(const wlan::common::MacAddr& addr) {
  auto iter = addr_wcid_map_.find(addr);
  if (iter == addr_wcid_map_.end()) {
    return std::nullopt;
  }

  return {iter->second};
}

zx_status_t Device::AddPeer(const wlan::common::MacAddr& addr, uint8_t* out_wcid) {
  auto iter = addr_wcid_map_.find(addr);
  if (iter != addr_wcid_map_.end()) {
    *out_wcid = iter->second;
    return ZX_OK;
  }

  size_t wcid;
  bool no_wcid_available = used_wcid_bitmap_.Get(0, kMaxValidWcid + 1, &wcid);
  if (no_wcid_available) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = used_wcid_bitmap_.SetOne(wcid);
  if (status != ZX_OK) {
    return status;
  }

  addr_wcid_map_.emplace(addr, static_cast<uint8_t>(wcid));
  *out_wcid = wcid;

  return ZX_OK;
}

zx_status_t Device::RemovePeer(const wlan::common::MacAddr& addr) {
  auto iter = addr_wcid_map_.find(addr);
  if (iter == addr_wcid_map_.end()) {
    return ZX_ERR_NOT_FOUND;
  }

  uint8_t wcid = iter->second;
  auto status = used_wcid_bitmap_.ClearOne(wcid);
  if (status != ZX_OK) {
    return status;
  }

  addr_wcid_map_.erase(iter);

  return ZX_OK;
}

zx_status_t Device::WlanmacSetKey(uint32_t options, const wlan_key_config_t* key_config) {
  if (options != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto keyMode = MapIeeeCipherSuiteToKeyMode(key_config->cipher_oui, key_config->cipher_type);
  if (keyMode == KeyMode::kUnsupported) {
    errorf("unsupported cipher suite: %d\n", key_config->cipher_type);
    return ZX_ERR_NOT_SUPPORTED;
  }

  wlan::common::MacAddr peer_addr(key_config->peer_addr);
  uint8_t wcid = 0;
  auto status = AddPeer(peer_addr, &wcid);
  if (status != ZX_OK) {
    return status;
  }

  switch (key_config->key_type) {
    case WLAN_KEY_TYPE_PAIRWISE: {
      // The driver doesn't support multiple BSS yet. Always use bss index 0.
      uint8_t bss_idx = 0;

      // Reset everything on failure.
      auto reset = fit::defer([&]() {
        RemovePeer(peer_addr);
        ResetWcid(wcid, 0, WLAN_KEY_TYPE_PAIRWISE);
      });

      status = WriteWcid(wcid, key_config->peer_addr);
      if (status != ZX_OK) {
        break;
      }

      status = WritePairwiseKey(wcid, key_config->key, key_config->key_len, keyMode);
      if (status != ZX_OK) {
        break;
      }

      status = WriteWcidAttribute(bss_idx, wcid, keyMode, KeyType::kPairwiseKey);
      if (status != ZX_OK) {
        break;
      }

      status = ResetIvEiv(wcid, 0, keyMode);
      if (status != ZX_OK) {
        break;
      }

      reset.cancel();
      return status;
    }
    case WLAN_KEY_TYPE_GROUP: {
      // The driver doesn't support multiple BSS yet. Always use bss index 0.
      uint8_t bss_idx = 0;
      uint8_t key_idx = key_config->key_idx;
      uint8_t skey = DeriveSharedKeyIndex(bss_idx, key_idx);

      // Reset everything on failure.
      auto reset = fit::defer([&]() {
        RemovePeer(peer_addr);
        ResetWcid(wcid, skey, WLAN_KEY_TYPE_GROUP);
      });

      status = WriteSharedKey(skey, key_config->key, key_config->key_len, keyMode);
      if (status != ZX_OK) {
        break;
      }

      status = WriteSharedKeyMode(skey, keyMode);
      if (status != ZX_OK) {
        break;
      }

      status = WriteWcid(wcid, kBcastAddr);
      if (status != ZX_OK) {
        break;
      }

      status = WriteWcidAttribute(bss_idx, wcid, keyMode, KeyType::kSharedKey);
      if (status != ZX_OK) {
        break;
      }

      status = ResetIvEiv(wcid, key_idx, keyMode);
      if (status != ZX_OK) {
        break;
      }

      reset.cancel();
      return status;
    }
    default: {
      errorf("unsupported key type: %d\n", key_config->key_type);
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  return status;
}

zx_status_t Device::WlanmacClearAssoc(uint32_t options, const uint8_t* bssid, size_t bssid_len) {
  if (options != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (std::memcmp(bssid, bssid_, ETH_MAC_SIZE)) {
    errorf("request to disassociate from bssid we are not currently associated with\n");
    return ZX_ERR_NOT_FOUND;
  }

  // Set bssid to zeroes to force disassociation
  uint8_t null_bssid[ETH_MAC_SIZE] = {0};
  zx_status_t status = SetBss(null_bssid);
  if (status == ZX_OK) {
    last_disconnect_time_ = zx::clock::get_monotonic();
    std::memcpy(last_disconnect_bssid_, bssid, ETH_MAC_SIZE);
    std::memcpy(bssid_, null_bssid, ETH_MAC_SIZE);
  }
  return status;
}

void Device::ReadRequestComplete(void* ctx, usb_request_t* request) {
  auto dev = static_cast<Device*>(ctx);
  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  dev->HandleRxComplete(request);
}

void Device::WriteRequestComplete(void* ctx, usb_request_t* request) {
  auto dev = static_cast<Device*>(ctx);
  if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    usb_request_release(request);
    return;
  }

  dev->HandleTxComplete(request);
}

uint8_t Device::GetRxAckPolicy(const wlan_tx_packet_t& wlan_pkt) {
  // TODO(fxbug.dev/29242): Honor what MLME instructs the chipset for this particular wlan_pkt
  // whether to wait for an acknowledgement from the recipient or not.
  // It appears that Ralink has its own logic to override the instruction
  // specified in txwi1.ack field. It shall be recorded here as it's found.
  return 1;  // Wait for acknowledgement
}

size_t Device::GetMpduLen(const wlan_tx_packet_t& wlan_pkt) {
  auto len = wlan_pkt.packet_head.data_size;
  if (wlan_pkt.packet_tail_list != nullptr) {
    if (wlan_pkt.packet_tail_list->data_size < wlan_pkt.tail_offset) {
      return ZX_ERR_INVALID_ARGS;
    }
    len += wlan_pkt.packet_tail_list->data_size - wlan_pkt.tail_offset;
  }
  return len;
}

size_t Device::GetTxwiLen() { return (rt_type_ == RT5592) ? 20 : 16; }

size_t Device::GetBulkoutAggrTailLen() { return 4; }

size_t Device::GetBulkoutAggrPayloadLen(const wlan_tx_packet_t& wlan_pkt) {
  // Structure of BulkoutAggregation's payload
  // TXWI            : 16 or 20 bytes // (a).
  // MPDU header     :      (b) bytes // (b).
  // L2PAD           :      0~3 bytes // (c).
  // MSDU            :      (d) bytes // (d).  (b) + (d) is mpdu_len
  // Bulkout Agg Pad :      0~3 bytes // (e).

  const auto head_data = static_cast<const uint8_t*>(wlan_pkt.packet_head.data_buffer);
  auto head_len = wlan_pkt.packet_head.data_size;
  auto has_tail = wlan_pkt.packet_tail_list != nullptr;
  uint16_t tail_len_eff = 0;
  if (has_tail) {
    auto tail = wlan_pkt.packet_tail_list;
    uint16_t tail_offset = wlan_pkt.tail_offset;
    tail_len_eff = tail->data_size - tail_offset;
  }

  auto mpdu_hdr_len = GetMacHdrLength(head_data, head_len);
  auto msdu_len = head_len + tail_len_eff - mpdu_hdr_len;

  auto l2pad_len = GetL2PadLen(wlan_pkt);

  auto aggr_payload_len = GetTxwiLen() + mpdu_hdr_len + l2pad_len + msdu_len;
  aggr_payload_len = RoundUp(aggr_payload_len, 4);

  return aggr_payload_len;
}

size_t Device::GetUsbReqLen(const wlan_tx_packet_t& wlan_pkt) {
  // Structure of BulkoutAggregation

  // TxInfo               :   4 bytes // (a).
  // Aggregation Payload  : (b) bytes // (b).
  // Bulkout Agg Tail Pad :   4 bytes // (c).

  return sizeof(TxInfo) + GetBulkoutAggrPayloadLen(wlan_pkt) + GetBulkoutAggrTailLen();
}

void Device::DumpLengths(const wlan_tx_packet_t& wlan_pkt, BulkoutAggregation* usb_pkt,
                         usb_request_t* req) {
  {
    size_t usb_request_hdr_len = req->header.length;
    size_t aggr_payload_len = usb_pkt->tx_info.aggr_payload_len();

    debugf("len:    usb_request_hdr:%zu usb_tx_pkt:%zu aggr_payload_len:%zu\n", usb_request_hdr_len,
           GetUsbReqLen(wlan_pkt), aggr_payload_len);
  }

  {  // wlan_pkt
    uint16_t wlan_pkt_head_len = wlan_pkt.packet_head.data_size;
    uint16_t wlan_pkt_tail_offset = wlan_pkt.tail_offset;
    bool has_wlan_pkt_tail = (wlan_pkt.packet_tail_list != nullptr);
    uint16_t wlan_pkt_tail_len = has_wlan_pkt_tail ? wlan_pkt.packet_tail_list->data_size : 0;
    debugf("        mpdu_len:%zu wlan_pkt head:%u\n", GetMpduLen(wlan_pkt), wlan_pkt_head_len);
    if (has_wlan_pkt_tail) {
      debugf("        wlan_pkt tail:%u offset:%u\n", wlan_pkt_tail_len, wlan_pkt_tail_offset);
    }
  }

  debugf("        txinfo:%zu txwi:%zu BulkoutTail:%zu\n", sizeof(TxInfo), GetTxwiLen(),
         GetBulkoutAggrTailLen());
}

size_t Device::GetL2PadLen(const wlan_tx_packet_t& wlan_pkt) {
  const auto head_data = static_cast<const uint8_t*>(wlan_pkt.packet_head.data_buffer);
  auto head_len = wlan_pkt.packet_head.data_size;
  auto frame_hdr_len = GetMacHdrLength(head_data, head_len);
  auto l2pad_len = RoundUp(frame_hdr_len, 4) - frame_hdr_len;

  return l2pad_len;
}

}  // namespace ralink
