// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "register.h"

namespace ralink {

enum UsbVendorRequest : uint8_t {
    kDeviceMode = 1,
    kSingleWrite = 2,
    kSingleRead = 3,
    kMultiWrite = 6,
    kMultiRead = 7,
    kEepromWrite = 8,
    kEepromRead = 9,
    kLedControl = 10,
    kRxControl = 12,
};

enum UsbModeOffset : uint8_t {
    kReset = 1,
    kUnplug = 2,
    kFunction = 3,
    kTest = 4,
    kFirmware = 8,
    kAutorun = 17,
};

constexpr uint16_t RT5390 = 0x5390;
constexpr uint16_t REV_RT5390F = 0x0502;
constexpr uint16_t REV_RT5390R = 0x1502;
constexpr uint16_t RT5592 = 0x5592;
constexpr uint16_t REV_RT5592C = 0x221;

struct RxWcidEntry {
    uint8_t mac[6];
    uint8_t ba_sess_mask[2];
} __PACKED;

constexpr uint16_t RX_WCID_BASE = 0x1800;
constexpr uint16_t FW_IMAGE_BASE = 0x3000;
constexpr uint16_t PAIRWISE_KEY_BASE = 0x4000;
constexpr uint16_t BEACON_BASE = 0x4000;
constexpr uint16_t IV_EIV_BASE = 0x6000;
constexpr uint16_t WCID_ATTR_BASE = 0x6800;
constexpr uint16_t SHARED_KEY_BASE = 0x6c00;
constexpr uint16_t SHARED_KEY_MODE_BASE = 0x7000;

// B/G min/max TX power
constexpr int8_t kMinTxPower_BG = 0;   // Seemingly dBm unit, assuming 1 Tx chain
constexpr int8_t kMaxTxPower_BG = 31;  //
constexpr int8_t kMinTxPower_A = -7;   // Seemingly dBm unit, assuming 2 Tx chain
constexpr int8_t kMaxTxPower_A = 15;   //

// EIRP max power
constexpr uint16_t kEirpMaxPower = 0x50;  // Seemingly 0.5 dBm unit, making 40 dBm
// TX compensation max power
constexpr uint16_t kTxCompMaxPower = 0x0c;  // Unit uncertain.

// Device supports multiple rotating group keys for each BSS.
constexpr int8_t kGroupKeysPerBss = 4;
// A shared key mode allows configuring key mode for all the keys of two BSS.
constexpr int8_t kKeyModesPerSharedKeyMode = kGroupKeysPerBss * 2;
constexpr int8_t kMaxSharedKeys = 31;

constexpr int8_t kNoProtectionKeyLen = 0;

// WCID = 255 for addresses which are not known to the hardware.
constexpr uint8_t kWcidUnknown = 255;
constexpr uint8_t kWcidBcastAddr = 2;
constexpr uint8_t kWcidBssid = 1;

// Beacon offset's value is a multiple of 64 bytes.
constexpr uint8_t kBeaconOffsetFactorByte = 64;
constexpr size_t kMaxBeaconSizeByte = 512;

// Entry for pairwise and shared key table.
struct KeyEntry {
    uint8_t key[16];
    uint8_t txMic[8];
    uint8_t rxMic[8];
} __PACKED;

struct IvEivEntry {
    uint8_t iv[4];
    uint8_t eiv[4];
} __PACKED;

// KeyMode cipher definitions differ from IEEE's cipher suite types.
// Compare to: IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-131
// See also garnet/lib/wlan/common/include/wlan/common/cipher.h
enum KeyMode : uint8_t {
    kNone = 0,
    kWep42 = 1,
    kWep104 = 2,
    kTkip = 3,
    kAes = 4,
    kCkip42 = 5,
    kCkip104 = 6,
    kCkip128 = 7,
    kWapi = 8,

    kUnsupported = 9,
} __PACKED;

enum KeyType : uint8_t {
    kSharedKey = 0,
    kPairwiseKey = 1,
};

class WcidAttrEntry : public BitField<uint32_t> {
   public:
    WLAN_BIT_FIELD(keyType, 0, 1);
    WLAN_BIT_FIELD(keyMode, 1, 3);
    WLAN_BIT_FIELD(bssIdx, 4, 3);
    WLAN_BIT_FIELD(rxUsrDef, 7, 3);
    WLAN_BIT_FIELD(keyModeExt, 10, 1);
    WLAN_BIT_FIELD(bssIdxExt, 11, 1);
    WLAN_BIT_FIELD(rsv, 12, 3);
    WLAN_BIT_FIELD(wapiMcbc, 15, 1);
    WLAN_BIT_FIELD(wapiRsv, 16, 8);
    WLAN_BIT_FIELD(wapiKeyIdx, 24, 8);
};

// Each SharedKeyMode entry allows to set the key mode for 8 shared keys.
class SharedKeyModeEntry {
   public:
    uint32_t value;

    zx_status_t set(uint8_t skey_idx, KeyMode mode) {
        if (skey_idx >= kKeyModesPerSharedKeyMode) { return ZX_ERR_INVALID_ARGS; }

        uint8_t offset = skey_idx * 4;
        uint32_t cleared = value & ~(15 << offset);
        value = cleared | ((mode & 15) << offset);
        return ZX_OK;
    }
};

// Registers

// TODO(tkilbourn): differentiate between read-only and read/write registers

class IntStatus : public Register<0x0200> {
   public:
    WLAN_BIT_FIELD(rx_dly_int, 0, 1);
    WLAN_BIT_FIELD(tx_dly_int, 1, 1);
    WLAN_BIT_FIELD(rx_done_int, 2, 1);
    WLAN_BIT_FIELD(tx_done_int0, 3, 1);
    WLAN_BIT_FIELD(tx_done_int1, 4, 2);
    WLAN_BIT_FIELD(tx_done_int2, 5, 1);
    WLAN_BIT_FIELD(tx_done_int3, 6, 1);
    WLAN_BIT_FIELD(tx_done_int4, 7, 1);
    WLAN_BIT_FIELD(tx_done_int5, 8, 1);
    WLAN_BIT_FIELD(mcu_cmd_int, 9, 1);
    WLAN_BIT_FIELD(tx_rx_coherent, 10, 1);
    WLAN_BIT_FIELD(mac_int_0, 11, 1);
    WLAN_BIT_FIELD(mac_int_1, 12, 1);
    WLAN_BIT_FIELD(mac_int_2, 13, 1);
    WLAN_BIT_FIELD(mac_int_3, 14, 1);
    WLAN_BIT_FIELD(mac_int_4, 15, 1);
    WLAN_BIT_FIELD(rx_coherent, 16, 1);
    WLAN_BIT_FIELD(tx_coherent, 17, 1);
};

class WpdmaGloCfg : public Register<0x0208> {
   public:
    WLAN_BIT_FIELD(tx_dma_en, 0, 1);
    WLAN_BIT_FIELD(tx_dma_busy, 1, 1);
    WLAN_BIT_FIELD(rx_dma_en, 2, 1);
    WLAN_BIT_FIELD(rx_dma_busy, 3, 1);
    WLAN_BIT_FIELD(wpdma_bt_size, 4, 2);
    WLAN_BIT_FIELD(tx_wb_ddone, 6, 1);
    WLAN_BIT_FIELD(big_endian, 7, 1);
    WLAN_BIT_FIELD(hdr_seg_len, 8, 8);
};

class GpioCtrl : public Register<0x0228> {
   public:
    WLAN_BIT_FIELD(gpio0_data, 0, 1);
    WLAN_BIT_FIELD(gpio1_data, 1, 1);
    WLAN_BIT_FIELD(gpio2_data, 2, 1);
    WLAN_BIT_FIELD(gpio3_data, 3, 1);
    WLAN_BIT_FIELD(gpio4_data, 4, 1);
    WLAN_BIT_FIELD(gpio5_data, 5, 1);
    WLAN_BIT_FIELD(gpio6_data, 6, 1);
    WLAN_BIT_FIELD(gpio7_data, 7, 1);

    WLAN_BIT_FIELD(gpio0_dir, 8, 1);
    WLAN_BIT_FIELD(gpio1_dir, 9, 1);
    WLAN_BIT_FIELD(gpio2_dir, 10, 1);
    WLAN_BIT_FIELD(gpio3_dir, 11, 1);
    WLAN_BIT_FIELD(gpio4_dir, 12, 1);
    WLAN_BIT_FIELD(gpio5_dir, 13, 1);
    WLAN_BIT_FIELD(gpio6_dir, 14, 1);
    WLAN_BIT_FIELD(gpio7_dir, 15, 1);

    WLAN_BIT_FIELD(gpio8_data, 16, 1);
    WLAN_BIT_FIELD(gpio9_data, 17, 1);
    WLAN_BIT_FIELD(gpio10_data, 18, 1);
    WLAN_BIT_FIELD(gpio11_data, 19, 1);

    WLAN_BIT_FIELD(gpio8_dir, 24, 1);
    WLAN_BIT_FIELD(gpio9_dir, 25, 1);
    WLAN_BIT_FIELD(gpio10_dir, 26, 1);
    WLAN_BIT_FIELD(gpio11_dir, 27, 1);
};

class UsbDmaCfg : public Register<0x02a0> {
   public:
    WLAN_BIT_FIELD(rx_agg_to, 0, 8);
    WLAN_BIT_FIELD(rx_agg_limit, 8, 8);
    WLAN_BIT_FIELD(phy_wd_en, 16, 1);
    WLAN_BIT_FIELD(tx_clear, 19, 1);
    WLAN_BIT_FIELD(txop_hald, 20, 1);
    WLAN_BIT_FIELD(rx_agg_en, 21, 1);
    WLAN_BIT_FIELD(udma_rx_en, 22, 1);
    WLAN_BIT_FIELD(udma_tx_en, 23, 1);
    WLAN_BIT_FIELD(epout_vld, 24, 5);
    WLAN_BIT_FIELD(rx_busy, 30, 1);
    WLAN_BIT_FIELD(tx_busy, 31, 1);
};

class UsCycCnt : public Register<0x02a4> {
   public:
    WLAN_BIT_FIELD(us_cyc_count, 0, 8);
    WLAN_BIT_FIELD(bt_mode_en, 8, 1);
    WLAN_BIT_FIELD(test_sel, 16, 8);
    WLAN_BIT_FIELD(test_en, 24, 1);
    WLAN_BIT_FIELD(edt_bypass, 28, 1);
};

class SysCtrl : public Register<0x0400> {
   public:
    WLAN_BIT_FIELD(mcu_ready, 7, 1);
    WLAN_BIT_FIELD(pme_oen, 13, 1);
};

class HostCmd : public Register<0x0404> {
   public:
    WLAN_BIT_FIELD(command, 0, 32);
};

class MaxPcnt : public Register<0x040c> {
   public:
    WLAN_BIT_FIELD(max_rx0q_pcnt, 0, 8);
    WLAN_BIT_FIELD(max_tx2q_pcnt, 8, 8);
    WLAN_BIT_FIELD(max_tx1q_pcnt, 16, 8);
    WLAN_BIT_FIELD(max_tx0q_pcnt, 24, 8);
};

class PbfCfg : public Register<0x0408> {
   public:
    // bit 0 unknown
    WLAN_BIT_FIELD(rx0q_en, 1, 1);
    WLAN_BIT_FIELD(tx2q_en, 2, 1);
    WLAN_BIT_FIELD(tx1q_en, 3, 1);
    WLAN_BIT_FIELD(tx0q_en, 4, 1);
    // bit 5-7 unknown
    WLAN_BIT_FIELD(hcca_mode, 8, 1);
    WLAN_BIT_FIELD(rx0q_mode, 9, 1);
    WLAN_BIT_FIELD(tx2q_mode, 10, 1);
    WLAN_BIT_FIELD(tx1q_mode, 11, 1);
    WLAN_BIT_FIELD(tx0q_mode, 12, 1);
    WLAN_BIT_FIELD(rx_drop_mode, 13, 1);
    WLAN_BIT_FIELD(null1_mode, 14, 1);
    WLAN_BIT_FIELD(null0_mode, 15, 1);
    WLAN_BIT_FIELD(tx2q_num, 16, 5);
    WLAN_BIT_FIELD(tx1q_num, 21, 3);
    WLAN_BIT_FIELD(null2_sel, 24, 3);
};

class BcnOffset0 : public Register<0x042C> {
   public:
    WLAN_BIT_FIELD(bcn0_offset, 0, 8);
    WLAN_BIT_FIELD(bcn1_offset, 8, 8);
    WLAN_BIT_FIELD(bcn2_offset, 16, 8);
    WLAN_BIT_FIELD(bcn3_offset, 24, 8);
};

class BcnOffset1 : public Register<0x0430> {
   public:
    WLAN_BIT_FIELD(bcn4_offset, 0, 8);
    WLAN_BIT_FIELD(bcn5_offset, 8, 8);
    WLAN_BIT_FIELD(bcn6_offset, 16, 8);
    WLAN_BIT_FIELD(bcn7_offset, 24, 8);
};

// CSR: Control / Status Register
class RfCsrCfg : public Register<0x0500> {
   public:
    WLAN_BIT_FIELD(rf_csr_data, 0, 8);
    WLAN_BIT_FIELD(rf_csr_addr, 8, 6);
    WLAN_BIT_FIELD(rf_csr_rw, 16, 1);
    WLAN_BIT_FIELD(rf_csr_kick, 17, 1);
};

class EfuseCtrl : public Register<0x0580> {
   public:
    WLAN_BIT_FIELD(sel_efuse, 31, 1);
    WLAN_BIT_FIELD(efsrom_kick, 30, 1);
    WLAN_BIT_FIELD(efsrom_ain, 16, 10);
    WLAN_BIT_FIELD(efsrom_mode, 6, 2);
};

class RfuseData0 : public Register<0x059c> {};
class RfuseData1 : public Register<0x0598> {};
class RfuseData2 : public Register<0x0594> {};
class RfuseData3 : public Register<0x0590> {};

class LdoCfg0 : public Register<0x05d4> {
   public:
    WLAN_BIT_FIELD(delay3, 0, 8);
    WLAN_BIT_FIELD(delay2, 8, 8);
    WLAN_BIT_FIELD(delay1, 16, 8);
    WLAN_BIT_FIELD(bgsel, 24, 2);
    WLAN_BIT_FIELD(ldo_core_vlevel, 26, 3);
    WLAN_BIT_FIELD(ldo25_level, 29, 2);
    WLAN_BIT_FIELD(ldo25_largea, 31, 1);
};

class DebugIndex : public Register<0x05e8> {
   public:
    WLAN_BIT_FIELD(testcsr_dbg_idx, 0, 8);
    WLAN_BIT_FIELD(reserved_xtal, 31, 1);
};

class AsicVerId : public Register<0x1000> {
   public:
    WLAN_BIT_FIELD(rev_id, 0, 16);
    WLAN_BIT_FIELD(ver_id, 16, 16);
};

class MacSysCtrl : public Register<0x1004> {
   public:
    WLAN_BIT_FIELD(mac_srst, 0, 1);
    WLAN_BIT_FIELD(bbp_hrst, 1, 1);
    WLAN_BIT_FIELD(mac_tx_en, 2, 1);
    WLAN_BIT_FIELD(mac_rx_en, 3, 1);
};

class MacAddrDw0 : public Register<0x1008> {
   public:
    WLAN_BIT_FIELD(mac_addr_0, 0, 8);
    WLAN_BIT_FIELD(mac_addr_1, 8, 8);
    WLAN_BIT_FIELD(mac_addr_2, 16, 8);
    WLAN_BIT_FIELD(mac_addr_3, 24, 8);
};

class MacAddrDw1 : public Register<0x100c> {
   public:
    WLAN_BIT_FIELD(mac_addr_4, 0, 8);
    WLAN_BIT_FIELD(mac_addr_5, 8, 8);
    WLAN_BIT_FIELD(unicast_to_me_mask, 16, 8);
};

enum MultiBssIdMode : uint8_t {
    k1BssIdMode = 0,
    k2BssIdMode = 1,
    k4BssIdMode = 2,
    k8BssIdMode = 3,
};

class MacBssidDw0 : public Register<0x1010> {
   public:
    WLAN_BIT_FIELD(mac_addr_0, 0, 8);
    WLAN_BIT_FIELD(mac_addr_1, 8, 8);
    WLAN_BIT_FIELD(mac_addr_2, 16, 8);
    WLAN_BIT_FIELD(mac_addr_3, 24, 8);
};

class MacBssidDw1 : public Register<0x1014> {
   public:
    WLAN_BIT_FIELD(mac_addr_4, 0, 8);
    WLAN_BIT_FIELD(mac_addr_5, 8, 8);
    WLAN_BIT_FIELD(multi_bss_mode, 16, 2);
    WLAN_BIT_FIELD(multi_bcn_num, 18, 3);
    WLAN_BIT_FIELD(new_multi_bssid_mode, 21, 1);
    WLAN_BIT_FIELD(multi_bssid_mode_bit4, 22, 1);
    WLAN_BIT_FIELD(multi_bssid_mode_bit3, 23, 1);
};

class MaxLenCfg : public Register<0x1018> {
   public:
    WLAN_BIT_FIELD(max_mpdu_len, 0, 12);
    WLAN_BIT_FIELD(max_psdu_len, 12, 2);
    WLAN_BIT_FIELD(min_psdu_len, 14, 2);  // From Linux kernel source
    WLAN_BIT_FIELD(min_mpdu_len, 16, 4);
};

class BbpCsrCfg : public Register<0x101c> {
   public:
    WLAN_BIT_FIELD(bbp_data, 0, 8);
    WLAN_BIT_FIELD(bbp_addr, 8, 8);
    WLAN_BIT_FIELD(bbp_csr_rw, 16, 1);
    WLAN_BIT_FIELD(bbp_csr_kick, 17, 1);
    WLAN_BIT_FIELD(bbp_par_dur, 18, 1);
    WLAN_BIT_FIELD(bbp_rw_mode, 19, 1);
};

class LedCfg : public Register<0x102c> {
   public:
    WLAN_BIT_FIELD(led_on_time, 0, 8);
    WLAN_BIT_FIELD(led_off_time, 8, 8);
    WLAN_BIT_FIELD(slow_blk_time, 16, 6);
    WLAN_BIT_FIELD(r_led_mode, 24, 2);
    WLAN_BIT_FIELD(g_led_mode, 26, 2);
    WLAN_BIT_FIELD(y_led_mode, 28, 2);
    WLAN_BIT_FIELD(led_pol, 30, 1);
};

class ForceBaWinsize : public Register<0x1040> {
   public:
    WLAN_BIT_FIELD(force_ba_winsize, 0, 6);
    WLAN_BIT_FIELD(force_ba_winsize_en, 6, 1);
};

class XifsTimeCfg : public Register<0x1100> {
   public:
    WLAN_BIT_FIELD(cck_sifs_time, 0, 8);
    WLAN_BIT_FIELD(ofdm_sifs_time, 8, 8);
    WLAN_BIT_FIELD(ofdm_xifs_time, 16, 4);
    WLAN_BIT_FIELD(eifs_time, 20, 9);
    WLAN_BIT_FIELD(bb_rxend_en, 29, 1);
};

class BkoffSlotCfg : public Register<0x1104> {
   public:
    WLAN_BIT_FIELD(slot_time, 0, 8);
    WLAN_BIT_FIELD(cc_delay_time, 8, 4);
};

class ChTimeCfg : public Register<0x110c> {
   public:
    WLAN_BIT_FIELD(ch_sta_timer_en, 0, 1);
    WLAN_BIT_FIELD(tx_as_ch_busy, 1, 1);
    WLAN_BIT_FIELD(rx_as_ch_busy, 2, 1);
    WLAN_BIT_FIELD(nav_as_ch_busy, 3, 1);
    WLAN_BIT_FIELD(eifs_as_ch_busy, 4, 1);
};

class BcnTimeCfg : public Register<0x1114> {
   public:
    WLAN_BIT_FIELD(bcn_intval, 0, 16);
    WLAN_BIT_FIELD(tsf_timer_en, 16, 1);
    WLAN_BIT_FIELD(tsf_sync_mode, 17, 2);
    WLAN_BIT_FIELD(tbtt_timer_en, 19, 1);
    WLAN_BIT_FIELD(bcn_tx_en, 20, 1);
    WLAN_BIT_FIELD(tsf_ins_comp, 24, 8);
};

class TbttSyncCfg : public Register<0x1118> {
   public:
    WLAN_BIT_FIELD(tbtt_adjust, 0, 8);
    WLAN_BIT_FIELD(bcn_exp_win, 8, 8);
    WLAN_BIT_FIELD(bcn_aifsn, 16, 4);
    WLAN_BIT_FIELD(bcn_cwmin, 20, 4);
};

class TbttTimer : public Register<0x01124> {
   public:
    WLAN_BIT_FIELD(tbtt_timer, 0, 16);
};

class IntTimerCfg : public Register<0x1128> {
   public:
    WLAN_BIT_FIELD(pre_tbtt_timer, 0, 16);
    WLAN_BIT_FIELD(gp_timer, 16, 16);
};

class IntTimerEn : public Register<0x112C> {
   public:
    WLAN_BIT_FIELD(pre_tbtt_int_en, 0, 1);
    WLAN_BIT_FIELD(gp_timer_en, 1, 1);
};

class ChIdleSta : public Register<0x1130> {
   public:
    WLAN_BIT_FIELD(ch_idle_time, 0, 32);
};

class ChBusySta : public Register<0x1134> {
   public:
    WLAN_BIT_FIELD(ch_busy_time, 0, 32);
};

class ExtChBusySta : public Register<0x1138> {
   public:
    WLAN_BIT_FIELD(ext_ch_busy_time, 0, 32);
};

class MacStatusReg : public Register<0x1200> {
   public:
    WLAN_BIT_FIELD(tx_status, 0, 1);
    WLAN_BIT_FIELD(rx_status, 1, 1);
};

class PwrPinCfg : public Register<0x1204> {
   public:
    WLAN_BIT_FIELD(io_rf_pe, 0, 1);
    WLAN_BIT_FIELD(io_ra_pe, 1, 1);
    WLAN_BIT_FIELD(io_pll_pd, 2, 1);
    WLAN_BIT_FIELD(io_adda_pd, 3, 1);
};

class AutoWakeupCfg : public Register<0x1208> {
   public:
    WLAN_BIT_FIELD(wakeup_lead_time, 0, 8);
    WLAN_BIT_FIELD(sleep_tbtt_num, 8, 7);
    WLAN_BIT_FIELD(auto_wakeup_en, 15, 1);
};

class TxPwrCfg0 : public Register<0x1314> {
   public:
    WLAN_BIT_FIELD(tx_pwr_cck_1, 0, 8);
    WLAN_BIT_FIELD(tx_pwr_cck_5, 8, 8);
    WLAN_BIT_FIELD(tx_pwr_ofdm_6, 16, 8);
    WLAN_BIT_FIELD(tx_pwr_ofdm_12, 24, 8);
};

// TODO(porce): Implement TxPwrCfg0Ext. Study which chipset needs this.

class TxPwrCfg1 : public Register<0x1318> {
   public:
    WLAN_BIT_FIELD(tx_pwr_ofdm_24, 0, 8);
    WLAN_BIT_FIELD(tx_pwr_ofdm_48, 8, 8);
    WLAN_BIT_FIELD(tx_pwr_mcs_0, 16, 8);
    WLAN_BIT_FIELD(tx_pwr_mcs_2, 24, 8);
};

class TxPwrCfg2 : public Register<0x131c> {
   public:
    WLAN_BIT_FIELD(tx_pwr_mcs_4, 0, 8);
    WLAN_BIT_FIELD(tx_pwr_mcs_6, 8, 8);
    WLAN_BIT_FIELD(tx_pwr_mcs_8, 16, 8);
    WLAN_BIT_FIELD(tx_pwr_mcs_10, 24, 8);
};

class TxPwrCfg3 : public Register<0x1320> {
   public:
    WLAN_BIT_FIELD(tx_pwr_mcs_12, 0, 8);
    WLAN_BIT_FIELD(tx_pwr_mcs_14, 8, 8);
    WLAN_BIT_FIELD(tx_pwr_stbc_0, 16, 8);
    WLAN_BIT_FIELD(tx_pwr_stbc_2, 24, 8);
};

class TxPwrCfg4 : public Register<0x1324> {
   public:
    WLAN_BIT_FIELD(tx_pwr_stbc_4, 0, 8);
    WLAN_BIT_FIELD(tx_pwr_stbc_6, 8, 8);
};

class TxPinCfg : public Register<0x1328> {
   public:
    WLAN_BIT_FIELD(pa_pe_a0_en, 0, 1);
    WLAN_BIT_FIELD(pa_pe_g0_en, 1, 1);
    WLAN_BIT_FIELD(pa_pe_a1_en, 2, 1);
    WLAN_BIT_FIELD(pa_pe_g1_en, 3, 1);
    WLAN_BIT_FIELD(pa_pe_a0_pol, 4, 1);
    WLAN_BIT_FIELD(pa_pe_g0_pol, 5, 1);
    WLAN_BIT_FIELD(pa_pe_a1_pol, 6, 1);
    WLAN_BIT_FIELD(pa_pe_g1_pol, 7, 1);
    WLAN_BIT_FIELD(lna_pe_a0_en, 8, 1);
    WLAN_BIT_FIELD(lna_pe_g0_en, 9, 1);
    WLAN_BIT_FIELD(lna_pe_a1_en, 10, 1);
    WLAN_BIT_FIELD(lna_pe_g1_en, 11, 1);
    WLAN_BIT_FIELD(lna_pe_a0_pol, 12, 1);
    WLAN_BIT_FIELD(lna_pe_g0_pol, 13, 1);
    WLAN_BIT_FIELD(lna_pe_a1_pol, 14, 1);
    WLAN_BIT_FIELD(lna_pe_g1_pol, 15, 1);
    WLAN_BIT_FIELD(rftr_en, 16, 1);
    WLAN_BIT_FIELD(rftr_pol, 17, 1);
    WLAN_BIT_FIELD(trsw_en, 18, 1);
    WLAN_BIT_FIELD(trsw_pol, 19, 1);
    WLAN_BIT_FIELD(rfrx_en, 20, 1);
    WLAN_BIT_FIELD(pa_pe_a2_en, 24, 1);
    WLAN_BIT_FIELD(pa_pe_g2_en, 25, 1);
    WLAN_BIT_FIELD(pa_pe_a2_pol, 26, 1);
    WLAN_BIT_FIELD(pa_pe_g2_pol, 27, 1);
    WLAN_BIT_FIELD(lna_pe_a2_en, 28, 1);
    WLAN_BIT_FIELD(lna_pe_g2_en, 29, 1);
    WLAN_BIT_FIELD(lna_pe_a2_pol, 30, 1);
    WLAN_BIT_FIELD(lna_pe_g2_pol, 31, 1);
};

class TxBandCfg : public Register<0x132c> {
   public:
    // For CBW40
    // 0x0: Use lower 20MHz (or "use lower 40MHz band in 20MHz tx")
    // 0x1: Use upper 20MHz (or "use upper 40MHz band in 20MHz tx")
    WLAN_BIT_FIELD(tx_band_sel, 0, 1);

    WLAN_BIT_FIELD(a, 1, 1);   // or denoted as 5g_band_sel_p
    WLAN_BIT_FIELD(bg, 2, 1);  // or denoted as 5g_band_sel_n
};

class TxSwCfg0 : public Register<0x1330> {
   public:
    WLAN_BIT_FIELD(dly_txpe_en, 0, 8);
    WLAN_BIT_FIELD(dly_pape_en, 8, 8);
    WLAN_BIT_FIELD(dly_trsw_en, 16, 8);
    WLAN_BIT_FIELD(dly_rftr_en, 24, 8);
};

class TxSwCfg1 : public Register<0x1334> {
   public:
    WLAN_BIT_FIELD(dly_pape_dis, 0, 8);
    WLAN_BIT_FIELD(dly_trsw_dis, 8, 8);
    WLAN_BIT_FIELD(dly_rftr_dis, 16, 8);
};

class TxSwCfg2 : public Register<0x1338> {
   public:
    WLAN_BIT_FIELD(dly_dac_dis, 0, 8);
    WLAN_BIT_FIELD(dly_dac_en, 8, 8);
    WLAN_BIT_FIELD(dly_lna_dis, 16, 8);
    WLAN_BIT_FIELD(dly_lna_en, 24, 8);
};

class TxopCtrlCfg : public Register<0x1340> {
   public:
    WLAN_BIT_FIELD(txop_trun_en, 0, 6);
    WLAN_BIT_FIELD(lsig_txop_en, 6, 1);

    // These control the behavior of secondary 20MHz channel's CCA
    // and an option to fall back to 20MHz transmission from 40MHz one
    WLAN_BIT_FIELD(ext_cca_en, 7, 1);
    WLAN_BIT_FIELD(ext_cca_dly, 8, 8);
    WLAN_BIT_FIELD(ext_cw_min, 16, 4);
    WLAN_BIT_FIELD(ed_cca_en, 20, 1);
};

class TxRtsCfg : public Register<0x1344> {
   public:
    WLAN_BIT_FIELD(rts_rty_limit, 0, 8);
    WLAN_BIT_FIELD(rts_thres, 8, 16);
    WLAN_BIT_FIELD(rts_fbk_en, 24, 1);
};

class TxTimeoutCfg : public Register<0x1348> {
   public:
    WLAN_BIT_FIELD(mpdu_life_time, 4, 4);
    WLAN_BIT_FIELD(rx_ack_timeout, 8, 8);
    WLAN_BIT_FIELD(txop_timeout, 16, 8);
    WLAN_BIT_FIELD(ackto_end_txop, 24, 1);
};

class TxRtyCfg : public Register<0x134c> {
   public:
    WLAN_BIT_FIELD(short_rty_limit, 0, 8);
    WLAN_BIT_FIELD(long_rty_limit, 8, 8);
    WLAN_BIT_FIELD(long_rty_thres, 16, 12);
    WLAN_BIT_FIELD(nag_rty_mode, 28, 1);
    WLAN_BIT_FIELD(agg_rty_mode, 29, 1);
    WLAN_BIT_FIELD(tx_autofb_en, 30, 1);
};

class TxLinkCfg : public Register<0x1350> {
   public:
    WLAN_BIT_FIELD(remote_mfb_lifetime, 0, 8);
    WLAN_BIT_FIELD(tx_mfb_en, 8, 1);
    WLAN_BIT_FIELD(remote_umfs_en, 9, 1);
    WLAN_BIT_FIELD(tx_mrq_en, 10, 1);
    WLAN_BIT_FIELD(tx_rdg_en, 11, 1);
    WLAN_BIT_FIELD(tx_cfack_en, 12, 1);
    WLAN_BIT_FIELD(remote_mfb, 16, 8);
    WLAN_BIT_FIELD(remote_mfs, 24, 8);
};

class HtFbkCfg0 : public Register<0x1354> {
   public:
    WLAN_BIT_FIELD(ht_mcs0_fbk, 0, 4);
    WLAN_BIT_FIELD(ht_mcs1_fbk, 4, 4);
    WLAN_BIT_FIELD(ht_mcs2_fbk, 8, 4);
    WLAN_BIT_FIELD(ht_mcs3_fbk, 12, 4);
    WLAN_BIT_FIELD(ht_mcs4_fbk, 16, 4);
    WLAN_BIT_FIELD(ht_mcs5_fbk, 20, 4);
    WLAN_BIT_FIELD(ht_mcs6_fbk, 24, 4);
    WLAN_BIT_FIELD(ht_mcs7_fbk, 28, 4);
};

class HtFbkCfg1 : public Register<0x1358> {
   public:
    WLAN_BIT_FIELD(ht_mcs8_fbk, 0, 4);
    WLAN_BIT_FIELD(ht_mcs9_fbk, 4, 4);
    WLAN_BIT_FIELD(ht_mcs10_fbk, 8, 4);
    WLAN_BIT_FIELD(ht_mcs11_fbk, 12, 4);
    WLAN_BIT_FIELD(ht_mcs12_fbk, 16, 4);
    WLAN_BIT_FIELD(ht_mcs13_fbk, 20, 4);
    WLAN_BIT_FIELD(ht_mcs14_fbk, 24, 4);
    WLAN_BIT_FIELD(ht_mcs15_fbk, 28, 4);
};

class LgFbkCfg0 : public Register<0x135c> {
   public:
    WLAN_BIT_FIELD(ofdm0_fbk, 0, 4);
    WLAN_BIT_FIELD(ofdm1_fbk, 4, 4);
    WLAN_BIT_FIELD(ofdm2_fbk, 8, 4);
    WLAN_BIT_FIELD(ofdm3_fbk, 12, 4);
    WLAN_BIT_FIELD(ofdm4_fbk, 16, 4);
    WLAN_BIT_FIELD(ofdm5_fbk, 20, 4);
    WLAN_BIT_FIELD(ofdm6_fbk, 24, 4);
    WLAN_BIT_FIELD(ofdm7_fbk, 28, 4);
};

class LgFbkCfg1 : public Register<0x1360> {
   public:
    WLAN_BIT_FIELD(cck0_fbk, 0, 4);
    WLAN_BIT_FIELD(cck1_fbk, 4, 4);
    WLAN_BIT_FIELD(cck2_fbk, 8, 4);
    WLAN_BIT_FIELD(cck3_fbk, 12, 4);
};

template <uint16_t A> class ProtCfg : public Register<A> {
   public:
    WLAN_BIT_FIELD(prot_rate, 0, 16);
    WLAN_BIT_FIELD(prot_ctrl, 16, 2);
    WLAN_BIT_FIELD(prot_nav, 18, 2);
    WLAN_BIT_FIELD(txop_allow_cck_tx, 20, 1);
    WLAN_BIT_FIELD(txop_allow_ofdm_tx, 21, 1);
    WLAN_BIT_FIELD(txop_allow_mm20_tx, 22, 1);
    WLAN_BIT_FIELD(txop_allow_mm40_tx, 23, 1);
    WLAN_BIT_FIELD(txop_allow_gf20_tx, 24, 1);
    WLAN_BIT_FIELD(txop_allow_gf40_tx, 25, 1);
    WLAN_BIT_FIELD(rtsth_en, 26, 1);
};

class CckProtCfg : public ProtCfg<0x1364> {};
class OfdmProtCfg : public ProtCfg<0x1368> {};
class Mm20ProtCfg : public ProtCfg<0x136c> {};
class Mm40ProtCfg : public ProtCfg<0x1370> {};
class Gf20ProtCfg : public ProtCfg<0x1374> {};
class Gf40ProtCfg : public ProtCfg<0x1378> {};

class ExpAckTime : public Register<0x1380> {
   public:
    WLAN_BIT_FIELD(exp_cck_ack_time, 0, 15);
    WLAN_BIT_FIELD(exp_ofdm_ack_time, 16, 15);
};

class RxFiltrCfg : public Register<0x1400> {
   public:
    WLAN_BIT_FIELD(drop_crc_err, 0, 1);
    WLAN_BIT_FIELD(drop_phy_err, 1, 1);
    WLAN_BIT_FIELD(drop_uc_nome, 2, 1);
    WLAN_BIT_FIELD(drop_not_mybss, 3, 1);
    WLAN_BIT_FIELD(drop_ver_err, 4, 1);
    WLAN_BIT_FIELD(drop_mc, 5, 1);
    WLAN_BIT_FIELD(drop_bc, 6, 1);
    WLAN_BIT_FIELD(drop_dupl, 7, 1);
    WLAN_BIT_FIELD(drop_cfack, 8, 1);
    WLAN_BIT_FIELD(drop_cfend, 9, 1);
    WLAN_BIT_FIELD(drop_ack, 10, 1);
    WLAN_BIT_FIELD(drop_cts, 11, 1);
    WLAN_BIT_FIELD(drop_rts, 12, 1);
    WLAN_BIT_FIELD(drop_pspoll, 13, 1);
    WLAN_BIT_FIELD(drop_ba, 14, 1);
    WLAN_BIT_FIELD(drop_bar, 15, 1);
    WLAN_BIT_FIELD(drop_ctrl_rsv, 16, 1);
};

class AutoRspCfg : public Register<0x1404> {
   public:
    WLAN_BIT_FIELD(auto_rsp_en, 0, 1);
    WLAN_BIT_FIELD(bac_ackpolicy_en, 1, 1);

    // CBW40 CTS behavior control
    WLAN_BIT_FIELD(cts_40m_mode, 2, 1);
    WLAN_BIT_FIELD(cts_40m_ref, 3, 1);

    WLAN_BIT_FIELD(cck_short_en, 4, 1);
    WLAN_BIT_FIELD(ctrl_wrap_en, 5, 1);
    WLAN_BIT_FIELD(bac_ack_policy, 6, 1);
    WLAN_BIT_FIELD(ctrl_pwr_bit, 7, 1);
};

class LegacyBasicRate : public Register<0x1408> {
   public:
    WLAN_BIT_FIELD(rate_1mbps, 0, 1);
    WLAN_BIT_FIELD(rate_2mbps, 1, 1);
    WLAN_BIT_FIELD(rate_5_5mbps, 2, 1);
    WLAN_BIT_FIELD(rate_11mbps, 3, 1);
    WLAN_BIT_FIELD(rate_6mbps, 4, 1);
    WLAN_BIT_FIELD(rate_9mbps, 5, 1);
    WLAN_BIT_FIELD(rate_12mbps, 6, 1);
    WLAN_BIT_FIELD(rate_18mbps, 7, 1);
    WLAN_BIT_FIELD(rate_24mbps, 8, 1);
    WLAN_BIT_FIELD(rate_36mbps, 9, 1);
    WLAN_BIT_FIELD(rate_48mbps, 10, 1);
    WLAN_BIT_FIELD(rate_54mbps, 11, 1);
};

class HtBasicRate : public Register<0x140c> {
    // TODO: figure out what these bits are
};

class TxopHldrEt : public Register<0x1608> {
   public:
    WLAN_BIT_FIELD(per_rx_rst_en, 0, 1);
    WLAN_BIT_FIELD(tx40m_blk_en, 1, 1);  // CBW40
    WLAN_BIT_FIELD(tx_bcn_hipri_dis, 2, 1);
    WLAN_BIT_FIELD(pape_map1s_en, 3, 1);
    WLAN_BIT_FIELD(pape_map, 4, 1);
    WLAN_BIT_FIELD(reserved_unk, 5, 11);
    WLAN_BIT_FIELD(tx_fbk_thres, 16, 2);
    WLAN_BIT_FIELD(tx_fbk_thres_en, 18, 1);
    WLAN_BIT_FIELD(tx_dma_timeout, 19, 5);
    WLAN_BIT_FIELD(ampdu_acc_en, 24, 1);
};

class RxStaCnt0 : public Register<0x1700> {
   public:
    WLAN_BIT_FIELD(crc_errcnt, 0, 16);
    WLAN_BIT_FIELD(phy_errcnt, 16, 16);
};

class RxStaCnt1 : public Register<0x1704> {
   public:
    WLAN_BIT_FIELD(cca_errcnt, 0, 16);
    WLAN_BIT_FIELD(plpc_errcnt, 16, 16);
};

class RxStaCnt2 : public Register<0x1708> {
   public:
    WLAN_BIT_FIELD(rx_dupl_cnt, 0, 16);
    WLAN_BIT_FIELD(rx_ovfl_cnt, 16, 16);
};

class TxStaCnt0 : public Register<0x170c> {
   public:
    WLAN_BIT_FIELD(tx_fail_cnt, 0, 16);
    WLAN_BIT_FIELD(tx_bcn_cnt, 16, 16);
};

class TxStaCnt1 : public Register<0x1710> {
   public:
    WLAN_BIT_FIELD(tx_succ_cnt, 0, 16);
    WLAN_BIT_FIELD(tx_rty_cnt, 16, 16);
};

class TxStaCnt2 : public Register<0x1714> {
   public:
    WLAN_BIT_FIELD(tx_zero_cnt, 0, 16);
    WLAN_BIT_FIELD(tx_udfl_cnt, 16, 16);
};

class TxStatFifo : public Register<0x1718> {
   public:
    WLAN_BIT_FIELD(txq_vld, 0, 1);
    WLAN_BIT_FIELD(txq_pid, 1, 4);
    WLAN_BIT_FIELD(txq_ok, 5, 1);
    WLAN_BIT_FIELD(txq_agg, 6, 1);
    WLAN_BIT_FIELD(txq_ackreq, 7, 1);
    WLAN_BIT_FIELD(txq_wcid, 8, 8);
    WLAN_BIT_FIELD(txq_rate, 16, 16);
};

// EEPROM word offsets
constexpr uint16_t EEPROM_CHIP_ID = 0x0000;
constexpr uint16_t EEPROM_VERSION = 0x0001;
constexpr uint16_t EEPROM_MAC_ADDR_0 = 0x0002;
constexpr uint16_t EEPROM_MAC_ADDR_1 = 0x0003;
constexpr uint16_t EEPROM_MAC_ADDR_2 = 0x0004;
constexpr uint16_t EEPROM_NIC_CONF2 = 0x0021;
constexpr uint16_t EEPROM_RSSI_A = 0x0025;
constexpr uint16_t EEPROM_RSSI_A2 = 0x0026;
constexpr uint16_t EEPROM_TXPOWER_BG1 = 0x0029;  // Seemingly 0.5 dBm unit
constexpr uint16_t EEPROM_TXPOWER_BG2 = 0x0030;
constexpr uint16_t EEPROM_TXPOWER_A1 = 0x003c;
constexpr uint16_t EEPROM_TXPOWER_A2 = 0x0053;
constexpr uint16_t EEPROM_TXPOWER_BYRATE = 0x006f;  // Unit uncertain
constexpr uint16_t EEPROM_BBP_START = 0x0078;

constexpr size_t EEPROM_TXPOWER_BYRATE_SIZE = 9;
constexpr size_t EEPROM_BBP_SIZE = 16;

// EEPROM byte offsets
constexpr size_t EEPROM_GAIN_CAL_TX0_CH0_14 = 0x130;
constexpr size_t EEPROM_GAIN_CAL_TX0_CH36_64 = 0x144;
constexpr size_t EEPROM_GAIN_CAL_TX0_CH100_138 = 0x146;
constexpr size_t EEPROM_GAIN_CAL_TX0_CH140_165 = 0x148;
constexpr size_t EEPROM_PHASE_CAL_TX0_CH0_14 = 0x131;
constexpr size_t EEPROM_PHASE_CAL_TX0_CH36_64 = 0x145;
constexpr size_t EEPROM_PHASE_CAL_TX0_CH100_138 = 0x147;
constexpr size_t EEPROM_PHASE_CAL_TX0_CH140_165 = 0x149;
constexpr size_t EEPROM_GAIN_CAL_TX1_CH0_14 = 0x133;
constexpr size_t EEPROM_GAIN_CAL_TX1_CH36_64 = 0x14a;
constexpr size_t EEPROM_GAIN_CAL_TX1_CH100_138 = 0x14c;
constexpr size_t EEPROM_GAIN_CAL_TX1_CH140_165 = 0x14e;
constexpr size_t EEPROM_PHASE_CAL_TX1_CH0_14 = 0x134;
constexpr size_t EEPROM_PHASE_CAL_TX1_CH36_64 = 0x14b;
constexpr size_t EEPROM_PHASE_CAL_TX1_CH100_138 = 0x14d;
constexpr size_t EEPROM_PHASE_CAL_TX1_CH140_165 = 0x14f;
constexpr size_t EEPROM_COMP_CTL = 0x13c;
constexpr size_t EEPROM_IMB_COMP_CTL = 0x13d;

class EepromNicConf0 : public EepromField<0x001a> {
   public:
    WLAN_BIT_FIELD(rxpath, 0, 4);
    WLAN_BIT_FIELD(txpath, 4, 4);
    WLAN_BIT_FIELD(rf_type, 8, 4);
};

class EepromNicConf1 : public EepromField<0x001b> {
   public:
    WLAN_BIT_FIELD(hw_radio, 0, 1);
    WLAN_BIT_FIELD(external_tx_alc, 1, 1);
    WLAN_BIT_FIELD(external_lna_2g, 2, 1);
    WLAN_BIT_FIELD(external_lna_5g, 3, 1);
    WLAN_BIT_FIELD(cardbus_accel, 4, 1);
    WLAN_BIT_FIELD(bw40m_sb_2g, 5, 1);  // CBW40
    WLAN_BIT_FIELD(bw40m_sb_5g, 6, 1);  // CBW40
    WLAN_BIT_FIELD(wps_pbc, 7, 1);
    WLAN_BIT_FIELD(bw40m_2g, 8, 1);  // CBW40
    WLAN_BIT_FIELD(bw40m_5g, 9, 1);  // CBW40
    WLAN_BIT_FIELD(broadband_ext_lna, 10, 1);
    WLAN_BIT_FIELD(ant_diversity, 11, 2);
    WLAN_BIT_FIELD(internal_tx_alc, 13, 1);
    WLAN_BIT_FIELD(bt_coexist, 14, 1);
    WLAN_BIT_FIELD(dac_test, 15, 1);
};

class EepromFreq : public EepromField<0x001d> {
   public:
    WLAN_BIT_FIELD(offset, 0, 8);
};

class EepromLna : public EepromField<0x0022> {
   public:
    WLAN_BIT_FIELD(bg, 0, 8);
    WLAN_BIT_FIELD(a0, 8, 8);
};

class EepromRssiBg : public EepromField<0x0023> {
   public:
    WLAN_BIT_FIELD(offset0, 0, 8);
    WLAN_BIT_FIELD(offset1, 8, 8);
};

class EepromRssiBg2 : public EepromField<0x0024> {
   public:
    WLAN_BIT_FIELD(offset2, 0, 8);
    WLAN_BIT_FIELD(lna_a1, 8, 8);
};

class EepromEirpMaxTxPower : public EepromField<0x0027> {
   public:
    WLAN_BIT_FIELD(power_2g, 0, 8);
    WLAN_BIT_FIELD(power_5g, 8, 8);
};

class EepromTxPowerDelta : public EepromField<0x0028> {
   public:
    WLAN_BIT_FIELD(value_2g, 0, 6);
    WLAN_BIT_FIELD(type_2g, 6, 1);
    WLAN_BIT_FIELD(enable_2g, 7, 1);
    WLAN_BIT_FIELD(value_5g, 8, 6);
    WLAN_BIT_FIELD(type_5g, 14, 1);
    WLAN_BIT_FIELD(enable_5g, 15, 1);
};

// Host to MCU communication

class H2mMailboxCsr : public Register<0x7010> {
   public:
    WLAN_BIT_FIELD(arg0, 0, 8);
    WLAN_BIT_FIELD(arg1, 8, 8);
    WLAN_BIT_FIELD(cmd_token, 16, 8);
    WLAN_BIT_FIELD(owner, 24, 8);
};

class H2mMailboxCid : public Register<0x7014> {
   public:
    WLAN_BIT_FIELD(cmd0, 0, 8);
    WLAN_BIT_FIELD(cmd1, 8, 8);
    WLAN_BIT_FIELD(cmd2, 16, 8);
    WLAN_BIT_FIELD(cmd3, 24, 8);
};

class H2mMailboxStatus : public Register<0x701c> {};
class H2mBbpAgent : public Register<0x7028> {};
class H2mIntSrc : public Register<0x7024> {};

// MCU commands
constexpr uint8_t MCU_BOOT_SIGNAL = 0x72;
constexpr uint8_t MCU_WAKEUP = 0x31;
constexpr uint8_t MCU_FREQ_OFFSET = 0x74;

// BBP registers

class Bbp1 : public BbpRegister<1> {
   public:
    // 2, 1, 0, 3 corresponds to -12, -6, 0, 6 dBm
    WLAN_BIT_FIELD(tx_power_ctrl, 0, 2);

    WLAN_BIT_FIELD(tx_antenna, 3, 2);
};

class Bbp3 : public BbpRegister<3> {
   public:
    WLAN_BIT_FIELD(rx_adc, 0, 2);
    WLAN_BIT_FIELD(rx_antenna, 3, 2);
    WLAN_BIT_FIELD(ht40_minus, 5, 1);  // CBW40BELOW
    WLAN_BIT_FIELD(adc_mode_switch, 6, 1);
    WLAN_BIT_FIELD(adc_init_mode, 7, 1);
};

class Bbp4 : public BbpRegister<4> {
   public:
    WLAN_BIT_FIELD(tx_bf, 0, 1);

    // 0x0: CBW20
    // 0x2: CBW40ABOVE, CBW40BELOW
    WLAN_BIT_FIELD(bandwidth, 3, 2);
    WLAN_BIT_FIELD(mac_if_ctrl, 6, 1);
};

class Bbp27 : public BbpRegister<27> {
   public:
    WLAN_BIT_FIELD(rx_chain_sel, 5, 2);
};

class Bbp105 : public BbpRegister<105> {
   public:
    WLAN_BIT_FIELD(sig_on_pri, 0, 1);
    WLAN_BIT_FIELD(feq, 1, 1);
    WLAN_BIT_FIELD(mld, 2, 1);
    WLAN_BIT_FIELD(chan_update_from_remod, 3, 1);
};

class Bbp138 : public BbpRegister<138> {
   public:
    WLAN_BIT_FIELD(rx_adc1, 1, 1);
    WLAN_BIT_FIELD(rx_adc2, 2, 1);
    WLAN_BIT_FIELD(tx_dac1, 5, 1);
    WLAN_BIT_FIELD(tx_dac2, 6, 1);
};

class Bbp152 : public BbpRegister<152> {
   public:
    WLAN_BIT_FIELD(rx_default_ant, 7, 1);
};

class Bbp254 : public BbpRegister<254> {
   public:
    WLAN_BIT_FIELD(unk_bit7, 7, 1);
};

// RFCSR registers

class Rfcsr1 : public RfcsrRegister<1> {
   public:
    WLAN_BIT_FIELD(rf_block_en, 0, 1);
    WLAN_BIT_FIELD(pll_pd, 1, 1);
    WLAN_BIT_FIELD(rx0_pd, 2, 1);
    WLAN_BIT_FIELD(tx0_pd, 3, 1);
    WLAN_BIT_FIELD(rx1_pd, 4, 1);
    WLAN_BIT_FIELD(tx1_pd, 5, 1);
    WLAN_BIT_FIELD(rx2_pd, 6, 1);
    WLAN_BIT_FIELD(tx2_pd, 7, 1);
};

class Rfcsr2 : public RfcsrRegister<2> {
   public:
    WLAN_BIT_FIELD(rescal_en, 7, 1);
};

class Rfcsr3 : public RfcsrRegister<3> {
   public:
    WLAN_BIT_FIELD(vcocal_en, 7, 1);
};

class Rfcsr8 : public RfcsrRegister<8> {
   public:
    WLAN_BIT_FIELD(n, 0, 8);
};

class Rfcsr9 : public RfcsrRegister<9> {
   public:
    WLAN_BIT_FIELD(k, 0, 4);
    WLAN_BIT_FIELD(n, 4, 1);
    WLAN_BIT_FIELD(mod, 7, 1);
};

class Rfcsr11 : public RfcsrRegister<11> {
   public:
    WLAN_BIT_FIELD(r, 0, 2);
    WLAN_BIT_FIELD(mod, 6, 2);
};

class Rfcsr17 : public RfcsrRegister<17> {
   public:
    WLAN_BIT_FIELD(freq_offset, 0, 7);
    WLAN_BIT_FIELD(high_bit, 7, 1);
};

class Rfcsr30 : public RfcsrRegister<30> {
   public:
    WLAN_BIT_FIELD(tx_h20m, 1, 1);  // 0x1 for CBW40*?
    WLAN_BIT_FIELD(rx_h20m, 2, 1);  // 0x1 for CBW40*?
    WLAN_BIT_FIELD(rx_vcm, 3, 2);
    WLAN_BIT_FIELD(rf_calibration, 7, 1);
};

class Rfcsr38 : public RfcsrRegister<38> {
   public:
    WLAN_BIT_FIELD(rx_lo1_en, 5, 1);
};

class Rfcsr39 : public RfcsrRegister<39> {
   public:
    WLAN_BIT_FIELD(rx_div, 6, 1);
    WLAN_BIT_FIELD(rx_lo2_en, 7, 1);
};

class Rfcsr49 : public RfcsrRegister<49> {
   public:
    WLAN_BIT_FIELD(tx, 0, 6);
    WLAN_BIT_FIELD(ep, 6, 2);
};

class Rfcsr50 : public RfcsrRegister<50> {
   public:
    WLAN_BIT_FIELD(tx, 0, 6);
    WLAN_BIT_FIELD(ep, 6, 2);
};

class RxInfo : public AddressableBitField<uint16_t, uint32_t, 0> {
   public:
    constexpr explicit RxInfo(uint32_t val) : AddressableBitField(val) {}
    WLAN_BIT_FIELD(usb_dma_rx_pkt_len, 0, 16);
};

class RxDesc : public BitField<uint32_t> {
   public:
    constexpr explicit RxDesc(uint32_t val) : BitField<uint32_t>(val) {}
    WLAN_BIT_FIELD(ba, 0, 1);
    WLAN_BIT_FIELD(data, 1, 1);
    WLAN_BIT_FIELD(nulldata, 2, 1);
    WLAN_BIT_FIELD(frag, 3, 1);
    WLAN_BIT_FIELD(unicast_to_me, 4, 1);
    WLAN_BIT_FIELD(multicast, 5, 1);
    WLAN_BIT_FIELD(broadcast, 6, 1);
    WLAN_BIT_FIELD(my_bss, 7, 1);
    WLAN_BIT_FIELD(crc_error, 8, 1);
    WLAN_BIT_FIELD(cipher_error, 9, 2);
    WLAN_BIT_FIELD(amsdu, 11, 1);
    WLAN_BIT_FIELD(htc, 12, 1);
    WLAN_BIT_FIELD(rssi, 13, 1);
    WLAN_BIT_FIELD(l2pad, 14, 1);
    WLAN_BIT_FIELD(ampdu, 15, 1);
    WLAN_BIT_FIELD(decrypted, 16, 1);
    WLAN_BIT_FIELD(plcp_rssi, 17, 1);
    WLAN_BIT_FIELD(cipher_alg, 18, 1);
    WLAN_BIT_FIELD(last_amsdu, 19, 1);
    WLAN_BIT_FIELD(plcp_signal, 20, 12);
};

class Rxwi0 : public AddressableBitField<uint16_t, uint32_t, 1> {
   public:
    constexpr explicit Rxwi0(uint32_t val) : AddressableBitField(val) {}
    WLAN_BIT_FIELD(wcid, 0, 8);
    WLAN_BIT_FIELD(key_idx, 8, 2);
    WLAN_BIT_FIELD(bss_idx, 10, 3);
    WLAN_BIT_FIELD(udf, 13, 3);
    WLAN_BIT_FIELD(mpdu_total_byte_count, 16, 12);
    WLAN_BIT_FIELD(tid, 28, 4);
};

class Rxwi1 : public AddressableBitField<uint16_t, uint32_t, 2> {
   public:
    constexpr explicit Rxwi1(uint32_t val) : AddressableBitField(val) {}
    WLAN_BIT_FIELD(frag, 0, 4);
    WLAN_BIT_FIELD(seq, 4, 12);
    WLAN_BIT_FIELD(mcs, 16, 7);
    WLAN_BIT_FIELD(bw, 23, 1);
    WLAN_BIT_FIELD(sgi, 24, 1);
    WLAN_BIT_FIELD(stbc, 25, 2);
    // Reserved 3 bits.
    WLAN_BIT_FIELD(phy_mode, 30, 2);
};

enum Bandwidth : uint8_t {
    k20MHz = 0x00,
    k40MHz = 0x01,
};

enum PhyMode : uint8_t {
    kLegacyCck = 0,
    kLegacyOfdm = 1,
    kHtMixMode = 2,
    kHtGreenfield = 3,

    kUnknown = 255,
};

enum LegacyCckMcs : uint8_t {
    kLongPreamble1Mbps = 0,
    kLongPreamble2Mbps = 1,
    kLongPreamble5_5Mbps = 2,
    kLongPreamble11Mbps = 3,
    // 4-7 reserved
    kShortPreamble1Mbps = 8,
    kShortPreamble2Mbps = 9,
    kShortPreamble5_5Mbps = 10,
    kShortPreamble11Mbps = 11,
    // All other values reserved
};
constexpr uint8_t kMaxOfdmMcs = 7;
constexpr uint8_t kMaxHtMcs = 7;  // excluding "duplicate 6Mbps" mcs
constexpr uint8_t kHtDuplicateMcs = 32;

class Rxwi2 : public AddressableBitField<uint16_t, uint32_t, 3> {
   public:
    constexpr explicit Rxwi2(uint32_t val) : AddressableBitField(val) {}
    WLAN_BIT_FIELD(rssi0, 0, 8);
    WLAN_BIT_FIELD(rssi1, 8, 8);
    WLAN_BIT_FIELD(rssi2, 16, 8);
};

class Rxwi3 : public AddressableBitField<uint16_t, uint32_t, 4> {
   public:
    constexpr explicit Rxwi3(uint32_t val) : AddressableBitField(val) {}
    WLAN_BIT_FIELD(snr0, 0, 8);
    WLAN_BIT_FIELD(snr1, 8, 8);
};

class TxInfo : public BitField<uint32_t> {
   public:
    WLAN_BIT_FIELD(aggr_payload_len, 0, 16);  // Bulk-out Aggregation format payload length
    // Reserved 8 bits.
    WLAN_BIT_FIELD(wiv, 24, 1);
    WLAN_BIT_FIELD(qsel, 25, 2);
    // Reserved 3 bits.
    WLAN_BIT_FIELD(next_vld, 30, 1);
    WLAN_BIT_FIELD(tx_burst, 31, 1);

    enum QSEL_PRIORITY {
        kHigh = 0x00,    // For Management function
        kMedium = 0x01,  // For HCCA function
        kLow = 0x02,     // For EDCA function
        kNa = 0x03,      // Nah..
    };
};

class Txwi0 : public BitField<uint32_t> {
   public:
    WLAN_BIT_FIELD(frag, 0, 1);
    WLAN_BIT_FIELD(mmps, 1, 1);
    WLAN_BIT_FIELD(cfack, 2, 1);
    WLAN_BIT_FIELD(ts, 3, 1);
    WLAN_BIT_FIELD(ampdu, 4, 1);
    WLAN_BIT_FIELD(mpdu_density, 5, 3);
    WLAN_BIT_FIELD(txop, 8, 2);
    WLAN_BIT_FIELD(mcs, 16, 7);
    WLAN_BIT_FIELD(bw, 23, 1);
    WLAN_BIT_FIELD(sgi, 24, 1);
    WLAN_BIT_FIELD(stbc, 25, 2);
    // Reserved 3 bits.
    // Definition based on Rxwi's format.
    WLAN_BIT_FIELD(phy_mode, 30, 2);
    // Alternative definition based on Txwi's format.
    // WLAN_BIT_FIELD(ofdm, 30, 1);
    // WLAN_BIT_FIELD(mimo, 31, 1);

    // Also defined in //garnet/drivers/wlan/wlan/element.h
    enum MinMPDUStartSpacing {  // for mpdu_density
        kNoRestrict = 0,
        kQuarterUsec = 1,
        kHalfUsec = 2,
        kOneUsec = 3,
        kTwoUsec = 4,
        kFourUsec = 5,
        kEightUsec = 6,
        kSixteenUsec = 7,
    };

    enum TXOP {
        kHtTxop = 0x00,
        kPifsTx = 0x01,
        kSifsTx = 0x02,
        kBackOff = 0x03,
    };
};

class Txwi1 : public BitField<uint32_t> {
   public:
    WLAN_BIT_FIELD(ack, 0, 1);
    WLAN_BIT_FIELD(nseq, 1, 1);
    WLAN_BIT_FIELD(ba_win_size, 2, 6);
    WLAN_BIT_FIELD(wcid, 8, 8);
    WLAN_BIT_FIELD(mpdu_total_byte_count, 16, 12);
    WLAN_BIT_FIELD(tx_packet_id, 28, 4);
};

class Txwi2 : public BitField<uint32_t> {
   public:
    WLAN_BIT_FIELD(iv, 0, 32);
};

class Txwi3 : public BitField<uint32_t> {
   public:
    WLAN_BIT_FIELD(eiv, 0, 32);
};

struct BulkoutAggregation {
    // Aggregation Header
    // TODO(porce): Investigate if Aggregation Header and TxInfo are identical.
    TxInfo tx_info;

    // Structure of BulkoutAggregation's payload
    // TXWI            : 16 or 20 bytes // (a).
    // MPDU header     :      (b) bytes // (b).
    // L2PAD           :      0~3 bytes // (c).
    // MSDU            :      (d) bytes // (d).  (b) + (d) is mpdu_len
    // Bulkout Agg Pad :      0~3 bytes // (e).
    Txwi0 txwi0;
    Txwi1 txwi1;
    Txwi2 txwi2;
    Txwi3 txwi3;
    // Txwi4 txwi4 for RT5592

    uint8_t* payload(uint16_t rt_type) {  // TODO(porce): better naming
        // Return where MPDU is to be stored.
        // Precisely, this will consist of MPDU header + (L2PAD) + MSDU + (AggregatePAD)
        size_t txwi_len = (rt_type == RT5592) ? 20 : 16;
        return reinterpret_cast<uint8_t*>(this) + sizeof(TxInfo) + txwi_len;
    }

    // BulkoutAggregation Tail padding (4 bytes of zeros)
} __PACKED;

}  // namespace ralink
