/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018        Intel Corporation
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
/****************************************************************************
 * File Name : apiGroup_0x5_Datapath.h
 *
 *
 * Description : This file contains all the structures and definitions required
 * for Commands, Responses and Notifications in Group 0x5 (Datapath Services and HW configuration)
 ****************************************************************************/

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_APIGROUPDATAPATH_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_APIGROUPDATAPATH_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/common.h"

/* ***************************************************************************
 * 0x0F - GRP_DATAPATH_TLC_MNG_CONFIG_CMD
 *************************************************************************** */

/**
 *****************************************************************************
 * @ingroup TlcMngConfig
 *
 * @title{config the TLC Manager}
 * @type{Command}
 *
 * The purpose is for the driver to configure the TLC engine
 * with initial parameters and limitations, which will be used
 *
 * @return
 * @fwImages
 *      Protocol
 *
 *****************************************************************************/

typedef enum _TLC_MNG_NSS_E {
  TLC_MNG_NSS_1,
  TLC_MNG_NSS_2,

  TLC_MNG_NSS_MAX,
} TLC_MNG_NSS_E;

typedef enum _TLC_MNG_MODE_E {
  TLC_MNG_MODE_CCK = 0,
  TLC_MNG_MODE_OFDM_LEGACY = TLC_MNG_MODE_CCK,
  TLC_MNG_MODE_LEGACY = TLC_MNG_MODE_CCK,
  TLC_MNG_MODE_HT,
  TLC_MNG_MODE_VHT,
  TLC_MNG_MODE_HE,
  TLC_MNG_MODE_INVALID,  // keep last
  TLC_MNG_MODE_NUM = TLC_MNG_MODE_INVALID,
} TLC_MNG_MODE_E;

// TLC_MNG_CONFIG_FLAGS - set the bit when the feature is enabled

/* STBC supported.
 * NOTE: For HT/VHT this bit implies STBC support for all bandwidths. For HE this bit is used to
 * imply STBC support for bandwidth <= 80Mhz only (bit 19 of the HE PHY Capabilities IE)
 */
#define TLC_MNG_CONFIG_FLAGS_STBC_MSK BIT(0)

/*
 * LDPC supported
 */
#define TLC_MNG_CONFIG_FLAGS_LDPC_MSK BIT(1)

/*
 * STBC supported for 160Mhz bandwidth in HE
 * (in HE PHY Capabilites IE, bit 63 is set)
 */
#define TLC_MNG_CONFIG_FLAGS_HE_STBC_160MHZ_MSK BIT(2)

/*
 * HE Dual Carrier Modulation (DCM) supported for BPSK (MCS 0) SISO
 * (in HE PHY Capabilites IE, bits 27-28 are > 0)
 */
#define TLC_MNG_CONFIG_FLAGS_HE_DCM_NSS_1_MSK BIT(3)

/*
 * HE Dual Carrier Modulation (DCM) supported for BPSK (MCS 0) MIMO
 * (in HE PHY Capabilites IE, bits 27-28 are > 0, and bit 29 is set)
 */
#define TLC_MNG_CONFIG_FLAGS_HE_DCM_NSS_2_MSK BIT(4)

/*
 * QCOM APs have a problem receiving frames with 2x LTF @160MHz, even though this is madatory by
 * spec.
 * Set this bit in order to prevent transmission with 2x LTF for all bandwidths (done this way in
 * order to keep the W/A as simple as possible).
 * NOTE - since setting this appies for all bandwidths, avoid setting it if the association doesn't
 *        support 160MHz.
 */
#define TLC_MNG_CONFIG_FLAGS_HE_BLOCK_2X_LTF_MSK BIT(15)

typedef enum _TLC_MNG_CH_WIDTH_E {
  TLC_MNG_CH_WIDTH_20MHZ,
  TLC_MNG_CH_WIDTH_40MHZ,
  TLC_MNG_CH_WIDTH_80MHZ,
  TLC_MNG_CH_WIDTH_160MHZ,
  TLC_MNG_CH_WIDTH_MAX,
} TLC_MNG_CH_WIDTH_E;

#define TLC_MNG_CHAIN_A_MSK BIT(0)
#define TLC_MNG_CHAIN_B_MSK BIT(1)

#define TLC_AMSDU_NOT_SUPPORTED 0
#define TLC_AMSDU_SUPPORTED 1

typedef struct _TLC_MNG_CONFIG_PARAMS_CMD_API_S_VER_2 {
  uint8_t maxChWidth;      // one of TLC_MNG_CH_WIDTH_E
  uint8_t bestSuppMode;    // best mode supported - as defined above in TLC_MNG_MODE_E
  uint8_t chainsEnabled;   // bitmask of TLC_MNG_CHAIN_[A/B]_MSK
  uint8_t amsduSupported;  // TX AMSDU transmission is supported
  // Use TLC_AMSDU_[NOT_]SUPPORTED
  uint16_t configFlags;  // bitmask of TLC_MNG_CONFIG_FLAGS_*
  uint16_t nonHt;        // bitmap of supported non-HT CCK and OFDM rates
  /* bit   | rate
     -------|--------
     0    | R_1M   CCK
     1    | R_2M   CCK
     2    | R_5_5M CCK
     3    | R_11M  CCK
     4    | R_6M   OFDM
     5    | R_9M   OFDM
     6    | R_12M  OFDM
     7    | R_18M  OFDM
     8    | R_24M  OFDM
     9    | R_36M  OFDM
     10   | R_48M  OFDM
     11   | R_54M  OFDM
     */
  uint16_t mcs[TLC_MNG_NSS_MAX][2];  // supported HT/VHT/HE rates per nss. [0] for 80mhz width
  // and lower, [1] for 160mhz.
  // This is done in order to conform with HE capabilities.
  uint16_t maxMpduLen;  // Max length of MPDU, in bytes.
  // Used to calculate allowed A-MSDU sizes.
  uint8_t sgiChWidthSupport;  // bitmap of SGI support per channel width.
  // use 1 << BIT(TLC_MNG_CH_WIDTH_*) to indicate sgi support
  // for that channel width.
  // unused for HE.
  uint8_t reserved1[1];

  wlan_band_t band;
} TLC_MNG_CONFIG_PARAMS_CMD_API_S_VER_2;

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_APIGROUPDATAPATH_H_
