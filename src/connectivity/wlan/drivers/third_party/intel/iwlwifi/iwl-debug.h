/******************************************************************************
 *
 * Copyright(c) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_DEBUG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_DEBUG_H_

#include <stdbool.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-modparams.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/debug.h"

static inline bool iwl_have_debug_level(uint32_t level) {
#ifdef CPTCFG_IWLWIFI_DEBUG
  return iwlwifi_mod_params.debug_level & level;
#else
  return false;
#endif
}

/* No matter what is m (priv, bus, trans), this will work */
#define IWL_ERR_DEV(d, f, a...) __iwl_err((d), false, false, f, ##a)
#define IWL_WARN_DEV(d, f, a...) __iwl_warn((d), f, ##a)
#define IWL_ERR(m, f, a...) IWL_ERR_DEV((m)->dev, f, ##a)
#define IWL_WARN(m, f, a...) __iwl_warn((m)->dev, f, ##a)
#define IWL_INFO(m, f, a...) __iwl_info((m)->dev, f, ##a)
#define IWL_CRIT(m, f, a...) __iwl_crit((m)->dev, f, ##a)

#define IWL_ERR_THROTTLE(m, f, a...) __iwl_err_throttle((m)->dev, false, false, f, ##a)
#define IWL_WARN_THROTTLE(m, f, a...) __iwl_warn_throttle((m)->dev, f, ##a)
#define IWL_INFO_THROTTLE(m, f, a...) __iwl_info_throttle((m)->dev, f, ##a)
#define IWL_CRIT_THROTTLE(m, f, a...) __iwl_crit_throttle((m)->dev, f, ##a)

#define iwl_print_hex_error(m, p, len)                                            \
  do {                                                                            \
    print_hex_dump(KERN_ERR, "iwl data: ", DUMP_PREFIX_OFFSET, 16, 1, p, len, 1); \
  } while (0)

#define __IWL_DEBUG_DEV(dev, level, limit, fmt, args...) \
  __iwl_dbg(dev, level, limit, __func__, fmt, ##args)
#define IWL_DEBUG(m, level, fmt, args...) __IWL_DEBUG_DEV((m)->dev, level, false, fmt, ##args)
#define IWL_DEBUG_DEV(dev, level, fmt, args...) __IWL_DEBUG_DEV(dev, level, false, fmt, ##args)
#define IWL_DEBUG_LIMIT(m, level, fmt, args...) __IWL_DEBUG_DEV((m)->dev, level, true, fmt, ##args)

#ifdef CPTCFG_IWLWIFI_DEBUG
#define iwl_print_hex_dump(m, level, p, len)                                          \
  do {                                                                                \
    if (iwl_have_debug_level(level))                                                  \
      print_hex_dump(KERN_DEBUG, "iwl data: ", DUMP_PREFIX_OFFSET, 16, 1, p, len, 1); \
  } while (0)
#else
#define iwl_print_hex_dump(m, level, p, len)
#endif /* CPTCFG_IWLWIFI_DEBUG */

/*
 * To use the debug system:
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of
 *
 * #define IWL_DL_xxxx VALUE
 *
 * where xxxx should be the name of the classification (for example, WEP).
 *
 * You then need to either add a IWL_xxxx_DEBUG() macro definition for your
 * classification, or use IWL_DEBUG(IWL_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * The active debug levels can be accessed via files
 *
 *  /sys/module/iwlwifi/parameters/debug
 * when CPTCFG_IWLWIFI_DEBUG=y.
 *
 *  /sys/kernel/debug/phy0/iwlwifi/debug/debug_level
 * when CPTCFG_IWLWIFI_DEBUGFS=y.
 *
 */

/* 0x0000000F - 0x00000001 */
#define IWL_DL_INFO 0x00000001
#define IWL_DL_MAC80211 0x00000002
#define IWL_DL_HCMD 0x00000004
#define IWL_DL_TDLS 0x00000008
/* 0x000000F0 - 0x00000010 */
#define IWL_DL_QUOTA 0x00000010
#define IWL_DL_TE 0x00000020
#define IWL_DL_EEPROM 0x00000040
#define IWL_DL_RADIO 0x00000080
/* 0x00000F00 - 0x00000100 */
#define IWL_DL_POWER 0x00000100
#define IWL_DL_TEMP 0x00000200
#define IWL_DL_RPM 0x00000400
#define IWL_DL_SCAN 0x00000800
/* 0x0000F000 - 0x00001000 */
#define IWL_DL_ASSOC 0x00001000
#define IWL_DL_DROP 0x00002000
#define IWL_DL_LAR 0x00004000
#define IWL_DL_COEX 0x00008000
/* 0x000F0000 - 0x00010000 */
#define IWL_DL_FW 0x00010000
#define IWL_DL_RF_KILL 0x00020000
#define IWL_DL_FW_ERRORS 0x00040000
#define IWL_DL_IO 0x00080000
/* 0x00F00000 - 0x00100000 */
#define IWL_DL_RATE 0x00100000
#define IWL_DL_CALIB 0x00200000
#define IWL_DL_WEP 0x00400000
#define IWL_DL_TX 0x00800000
/* 0x0F000000 - 0x01000000 */
#define IWL_DL_RX 0x01000000
#define IWL_DL_ISR 0x02000000
#define IWL_DL_HT 0x04000000
#define IWL_DL_EXTERNAL 0x08000000
/* 0xF0000000 - 0x10000000 */
#define IWL_DL_11H 0x10000000
#define IWL_DL_STATS 0x20000000
#define IWL_DL_TX_REPLY 0x40000000
#define IWL_DL_TX_QUEUES 0x80000000

#define IWL_DEBUG_INFO(p, f, a...) IWL_DEBUG(p, IWL_DL_INFO, f, ##a)
#define IWL_DEBUG_TDLS(p, f, a...) IWL_DEBUG(p, IWL_DL_TDLS, f, ##a)
#define IWL_DEBUG_MAC80211(p, f, a...) IWL_DEBUG(p, IWL_DL_MAC80211, f, ##a)
#define IWL_DEBUG_EXTERNAL(p, f, a...) IWL_DEBUG(p, IWL_DL_EXTERNAL, f, ##a)
#define IWL_DEBUG_TEMP(p, f, a...) IWL_DEBUG(p, IWL_DL_TEMP, f, ##a)
#define IWL_DEBUG_SCAN(p, f, a...) IWL_DEBUG(p, IWL_DL_SCAN, f, ##a)
#define IWL_DEBUG_RX(p, f, a...) IWL_DEBUG(p, IWL_DL_RX, f, ##a)
#define IWL_DEBUG_TX(p, f, a...) IWL_DEBUG(p, IWL_DL_TX, f, ##a)
#define IWL_DEBUG_ISR(p, f, a...) IWL_DEBUG(p, IWL_DL_ISR, f, ##a)
#define IWL_DEBUG_WEP(p, f, a...) IWL_DEBUG(p, IWL_DL_WEP, f, ##a)
#define IWL_DEBUG_HC(p, f, a...) IWL_DEBUG(p, IWL_DL_HCMD, f, ##a)
#define IWL_DEBUG_QUOTA(p, f, a...) IWL_DEBUG(p, IWL_DL_QUOTA, f, ##a)
#define IWL_DEBUG_TE(p, f, a...) IWL_DEBUG(p, IWL_DL_TE, f, ##a)
#define IWL_DEBUG_EEPROM(d, f, a...) IWL_DEBUG_DEV(d, IWL_DL_EEPROM, f, ##a)
#define IWL_DEBUG_CALIB(p, f, a...) IWL_DEBUG(p, IWL_DL_CALIB, f, ##a)
#define IWL_DEBUG_FW(p, f, a...) IWL_DEBUG(p, IWL_DL_FW, f, ##a)
#define IWL_DEBUG_RF_KILL(p, f, a...) IWL_DEBUG(p, IWL_DL_RF_KILL, f, ##a)
#define IWL_DEBUG_FW_ERRORS(p, f, a...) IWL_DEBUG(p, IWL_DL_FW_ERRORS, f, ##a)
#define IWL_DEBUG_IO(p, f, a...) IWL_DEBUG(p, IWL_DL_IO, f, ##a)
#define IWL_DEBUG_DROP(p, f, a...) IWL_DEBUG(p, IWL_DL_DROP, f, ##a)
#define IWL_DEBUG_DROP_LIMIT(p, f, a...) IWL_DEBUG_LIMIT(p, IWL_DL_DROP, f, ##a)
#define IWL_DEBUG_COEX(p, f, a...) IWL_DEBUG(p, IWL_DL_COEX, f, ##a)
#define IWL_DEBUG_RATE(p, f, a...) IWL_DEBUG(p, IWL_DL_RATE, f, ##a)
#define IWL_DEBUG_RATE_LIMIT(p, f, a...) IWL_DEBUG_LIMIT(p, IWL_DL_RATE, f, ##a)
#define IWL_DEBUG_ASSOC(p, f, a...) IWL_DEBUG(p, IWL_DL_ASSOC | IWL_DL_INFO, f, ##a)
#define IWL_DEBUG_ASSOC_LIMIT(p, f, a...) IWL_DEBUG_LIMIT(p, IWL_DL_ASSOC | IWL_DL_INFO, f, ##a)
#define IWL_DEBUG_HT(p, f, a...) IWL_DEBUG(p, IWL_DL_HT, f, ##a)
#define IWL_DEBUG_STATS(p, f, a...) IWL_DEBUG(p, IWL_DL_STATS, f, ##a)
#define IWL_DEBUG_STATS_LIMIT(p, f, a...) IWL_DEBUG_LIMIT(p, IWL_DL_STATS, f, ##a)
#define IWL_DEBUG_TX_REPLY(p, f, a...) IWL_DEBUG(p, IWL_DL_TX_REPLY, f, ##a)
#define IWL_DEBUG_TX_QUEUES(p, f, a...) IWL_DEBUG(p, IWL_DL_TX_QUEUES, f, ##a)
#define IWL_DEBUG_RADIO(p, f, a...) IWL_DEBUG(p, IWL_DL_RADIO, f, ##a)
#define IWL_DEBUG_DEV_RADIO(p, f, a...) IWL_DEBUG_DEV(p, IWL_DL_RADIO, f, ##a)
#define IWL_DEBUG_POWER(p, f, a...) IWL_DEBUG(p, IWL_DL_POWER, f, ##a)
#define IWL_DEBUG_11H(p, f, a...) IWL_DEBUG(p, IWL_DL_11H, f, ##a)
#define IWL_DEBUG_RPM(p, f, a...) IWL_DEBUG(p, IWL_DL_RPM, f, ##a)
#define IWL_DEBUG_LAR(p, f, a...) IWL_DEBUG(p, IWL_DL_LAR, f, ##a)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_DEBUG_H_
