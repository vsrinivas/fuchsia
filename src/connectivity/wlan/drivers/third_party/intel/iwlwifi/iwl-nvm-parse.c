/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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
 *****************************************************************************/
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-nvm-parse.h"

#include <stdint.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/acpi.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/cmdhdr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/commands.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/nvm-reg.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/img.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-modparams.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-prph.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"

/* NVM offsets (in words) definitions */
enum nvm_offsets {
  /* NVM HW-Section offset (in words) definitions */
  SUBSYSTEM_ID = 0x0A,
  HW_ADDR = 0x15,

  /* NVM SW-Section offset (in words) definitions */
  NVM_SW_SECTION = 0x1C0,
  NVM_VERSION = 0,
  RADIO_CFG = 1,
  SKU = 2,
  N_HW_ADDRS = 3,
  NVM_CHANNELS = 0x1E0 - NVM_SW_SECTION,

  /* NVM REGULATORY -Section offset (in words) definitions */
  NVM_CHANNELS_SDP = 0,
};

enum ext_nvm_offsets {
  /* NVM HW-Section offset (in words) definitions */
  MAC_ADDRESS_OVERRIDE_EXT_NVM = 1,

  /* NVM SW-Section offset (in words) definitions */
  NVM_VERSION_EXT_NVM = 0,
  RADIO_CFG_FAMILY_EXT_NVM = 0,
  SKU_FAMILY_8000 = 2,
  N_HW_ADDRS_FAMILY_8000 = 3,

  /* NVM REGULATORY -Section offset (in words) definitions */
  NVM_CHANNELS_EXTENDED = 0,
  NVM_LAR_OFFSET_OLD = 0x4C7,
  NVM_LAR_OFFSET = 0x507,
  NVM_LAR_ENABLED = 0x7,
};

/* SKU Capabilities (actual values from NVM definition) */
enum nvm_sku_bits {
  NVM_SKU_CAP_BAND_24GHZ = BIT(0),
  NVM_SKU_CAP_BAND_52GHZ = BIT(1),
  NVM_SKU_CAP_11N_ENABLE = BIT(2),
  NVM_SKU_CAP_11AC_ENABLE = BIT(3),
  NVM_SKU_CAP_MIMO_DISABLE = BIT(5),
};

/*
 * These are the channel numbers in the order that they are stored in the NVM
 */
static const uint8_t iwl_nvm_channels[] = {
    /* 2.4 GHz */
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    /* 5 GHz */
    36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149,
    153, 157, 161, 165};

static const uint8_t iwl_ext_nvm_channels[] = {
    /* 2.4 GHz */
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    /* 5 GHz */
    36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120,
    124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177, 181};

#define IWL_NVM_NUM_CHANNELS ARRAY_SIZE(iwl_nvm_channels)
#define IWL_NVM_NUM_CHANNELS_EXT ARRAY_SIZE(iwl_ext_nvm_channels)
#define NUM_2GHZ_CHANNELS 14
#define NUM_2GHZ_CHANNELS_EXT 14
#define FIRST_2GHZ_HT_MINUS 5
#define LAST_2GHZ_HT_PLUS 9
#define LAST_5GHZ_HT 165
#define LAST_5GHZ_HT_FAMILY_8000 181
#define N_HW_ADDR_MASK 0xF

/* rate data (static) */
uint16_t iwl_cfg80211_rates[] = {
    1 * 10,               // 1 Mbps
    2 * 10,               // 2 Mbps
    (uint8_t)(5.5 * 10),  // 5.5 Mbps
    11 * 10,              // 11 Mbps
    6 * 10,               // 6 Mbps
    9 * 10,               // 9 Mbps
    12 * 10,              // 12 Mbps
    18 * 10,              // 18 Mbps
    24 * 10,              // 24 Mbps
    36 * 10,              // 36 Mbps
    48 * 10,              // 48 Mbps
    54 * 10,              // 54 Mbps
};
#define RATES_24_OFFS 0
#define N_RATES_24 ARRAY_SIZE(iwl_cfg80211_rates)
#define RATES_52_OFFS 4  // 5GHz band doesn't support legacy rates (1/2/5.5/11Mbps).
#define N_RATES_52 (N_RATES_24 - RATES_52_OFFS)

// Get the index of the 'rate' in the iwl_cfg80211_rates[].
//
// Note that it is the 'hw_index' field in the original code and was removed in fxr/338189.
//
size_t iwl_get_rate_index(uint16_t rate) {
  for (size_t i = 0; i < ARRAY_SIZE(iwl_cfg80211_rates); i++) {
    if (iwl_cfg80211_rates[i] == rate) {
      return i;
    }
  }

  IWL_WARN(nullptr, "Unexpected rate value (%u)\n", rate);
  return 0;
}

/**
 * enum iwl_nvm_channel_flags - channel flags in NVM
 * @NVM_CHANNEL_VALID: channel is usable for this SKU/geo
 * @NVM_CHANNEL_IBSS: usable as an IBSS channel
 * @NVM_CHANNEL_ACTIVE: active scanning allowed
 * @NVM_CHANNEL_RADAR: radar detection required
 * @NVM_CHANNEL_INDOOR_ONLY: only indoor use is allowed
 * @NVM_CHANNEL_GO_CONCURRENT: GO operation is allowed when connected to BSS
 *  on same channel on 2.4 or same UNII band on 5.2
 * @NVM_CHANNEL_UNIFORM: uniform spreading required
 * @NVM_CHANNEL_20MHZ: 20 MHz channel okay
 * @NVM_CHANNEL_40MHZ: 40 MHz channel okay
 * @NVM_CHANNEL_80MHZ: 80 MHz channel okay
 * @NVM_CHANNEL_160MHZ: 160 MHz channel okay
 * @NVM_CHANNEL_DC_HIGH: DC HIGH required/allowed (?)
 */
enum iwl_nvm_channel_flags {
  NVM_CHANNEL_VALID = BIT(0),
  NVM_CHANNEL_IBSS = BIT(1),
  NVM_CHANNEL_ACTIVE = BIT(3),
  NVM_CHANNEL_RADAR = BIT(4),
  NVM_CHANNEL_INDOOR_ONLY = BIT(5),
  NVM_CHANNEL_GO_CONCURRENT = BIT(6),
  NVM_CHANNEL_UNIFORM = BIT(7),
  NVM_CHANNEL_20MHZ = BIT(8),
  NVM_CHANNEL_40MHZ = BIT(9),
  NVM_CHANNEL_80MHZ = BIT(10),
  NVM_CHANNEL_160MHZ = BIT(11),
  NVM_CHANNEL_DC_HIGH = BIT(12),
};

static inline void iwl_nvm_print_channel_flags(struct device* dev, uint32_t level, int chan,
                                               uint16_t flags) {
#define CHECK_AND_PRINT_I(x) ((flags & NVM_CHANNEL_##x) ? " " #x : "")

  if (!(flags & NVM_CHANNEL_VALID)) {
    IWL_DEBUG_DEV(dev, level, "Ch. %d: 0x%x: No traffic\n", chan, flags);
    return;
  }

  /* Note: already can print up to 101 characters, 110 is the limit! */
  IWL_DEBUG_DEV(dev, level, "Ch. %d: 0x%x:%s%s%s%s%s%s%s%s%s%s%s%s\n", chan, flags,
                CHECK_AND_PRINT_I(VALID), CHECK_AND_PRINT_I(IBSS), CHECK_AND_PRINT_I(ACTIVE),
                CHECK_AND_PRINT_I(RADAR), CHECK_AND_PRINT_I(INDOOR_ONLY),
                CHECK_AND_PRINT_I(GO_CONCURRENT), CHECK_AND_PRINT_I(UNIFORM),
                CHECK_AND_PRINT_I(20MHZ), CHECK_AND_PRINT_I(40MHZ), CHECK_AND_PRINT_I(80MHZ),
                CHECK_AND_PRINT_I(160MHZ), CHECK_AND_PRINT_I(DC_HIGH));
#undef CHECK_AND_PRINT_I
}

static uint32_t iwl_get_channel_flags(uint8_t ch_num, int ch_idx, bool is_5ghz, uint16_t nvm_flags,
                                      const struct iwl_cfg* cfg) {
  return 0;
#if 0   // NEEDS_PORTING
	u32 flags = IEEE80211_CHAN_NO_HT40;

	if (band == NL80211_BAND_2GHZ && (nvm_flags & NVM_CHANNEL_40MHZ)) {
		if (ch_num <= LAST_2GHZ_HT_PLUS)
			flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
		if (ch_num >= FIRST_2GHZ_HT_MINUS)
			flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
	} else if (nvm_flags & NVM_CHANNEL_40MHZ) {
		if ((ch_idx - NUM_2GHZ_CHANNELS) % 2 == 0)
			flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
		else
			flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
	}
	if (!(nvm_flags & NVM_CHANNEL_80MHZ))
		flags |= IEEE80211_CHAN_NO_80MHZ;
	if (!(nvm_flags & NVM_CHANNEL_160MHZ))
		flags |= IEEE80211_CHAN_NO_160MHZ;

	if (!(nvm_flags & NVM_CHANNEL_IBSS))
		flags |= IEEE80211_CHAN_NO_IR;

	if (!(nvm_flags & NVM_CHANNEL_ACTIVE))
		flags |= IEEE80211_CHAN_NO_IR;

	if (nvm_flags & NVM_CHANNEL_RADAR)
		flags |= IEEE80211_CHAN_RADAR;

	if (nvm_flags & NVM_CHANNEL_INDOOR_ONLY)
		flags |= IEEE80211_CHAN_INDOOR_ONLY;

	/* Set the GO concurrent flag only in case that NO_IR is set.
	 * Otherwise it is meaningless
	 */
	if ((nvm_flags & NVM_CHANNEL_GO_CONCURRENT) &&
	    (flags & IEEE80211_CHAN_NO_IR))
		flags |= IEEE80211_CHAN_IR_CONCURRENT;

	return flags;
#endif  // NEEDS_PORTING
}

// Initializes the channel list in the NVM data structure according to:
//
// + Channel flags info in NVM blob,
// + Channel list in this driver,
// + Band flags
//
static size_t iwl_init_channel_map(struct device* dev, const struct iwl_cfg* cfg,
                                   struct iwl_nvm_data* data, const __le16* const nvm_ch_flags,
                                   uint32_t sbands_flags) {
  size_t ch_idx;
  size_t n_channels = 0;
  struct ieee80211_channel* channel;
  uint16_t ch_flags;
  size_t num_of_ch, num_2ghz_channels;
  const uint8_t* nvm_chan;

  if (cfg->nvm_type != IWL_NVM_EXT) {
    num_of_ch = IWL_NVM_NUM_CHANNELS;
    nvm_chan = &iwl_nvm_channels[0];
    num_2ghz_channels = NUM_2GHZ_CHANNELS;
  } else {
    num_of_ch = IWL_NVM_NUM_CHANNELS_EXT;
    nvm_chan = &iwl_ext_nvm_channels[0];
    num_2ghz_channels = NUM_2GHZ_CHANNELS_EXT;
  }

  for (ch_idx = 0; ch_idx < num_of_ch; ch_idx++) {
    bool is_5ghz = (ch_idx >= num_2ghz_channels);

    ch_flags = le16_to_cpup(nvm_ch_flags + ch_idx);

    if (is_5ghz && !data->sku_cap_band_52ghz_enable) {
      continue;
    }

    /* workaround to disable wide channels in 5GHz */
    if ((sbands_flags & IWL_NVM_SBANDS_FLAGS_NO_WIDE_IN_5GHZ) && is_5ghz) {
      ch_flags &= ~(NVM_CHANNEL_40MHZ | NVM_CHANNEL_80MHZ | NVM_CHANNEL_160MHZ);
    }

    if (ch_flags & NVM_CHANNEL_160MHZ) {
      data->vht160_supported = true;
    }

    // TODO(fxbug.dev/69717): Remove this workaround for IWL_NVM_EXT.
    if (!ieee80211_is_valid_chan(nvm_chan[ch_idx])) {
      continue;
    }

    if (!(sbands_flags & IWL_NVM_SBANDS_FLAGS_LAR) && !(ch_flags & NVM_CHANNEL_VALID)) {
      /*
       * Channels might become valid later if lar is
       * supported, hence we still want to add them to
       * the list of supported channels to cfg80211.
       */
      iwl_nvm_print_channel_flags(dev, IWL_DL_EEPROM, nvm_chan[ch_idx], ch_flags);
      continue;
    }

    channel = &data->channels[n_channels];
    n_channels++;

    channel->ch_num = nvm_chan[ch_idx];
    channel->band = is_5ghz ? WLAN_BAND_FIVE_GHZ : WLAN_BAND_TWO_GHZ;
    channel->center_freq = ieee80211_get_center_freq((uint8_t)channel->ch_num);

    /* Initialize regulatory-based run-time data */

    /*
     * Default value - highest tx power value.  max_power
     * is not used in mvm, and is used for backwards compatibility
     */
    channel->max_power = IWL_DEFAULT_MAX_TX_POWER;

    /* don't put limitations in case we're using LAR */
    if (!(sbands_flags & IWL_NVM_SBANDS_FLAGS_LAR))
      channel->flags = iwl_get_channel_flags(nvm_chan[ch_idx], ch_idx, is_5ghz, ch_flags, cfg);
    else {
      channel->flags = 0;
    }

    iwl_nvm_print_channel_flags(dev, IWL_DL_EEPROM, channel->ch_num, ch_flags);
    IWL_DEBUG_EEPROM(dev, "Ch. %d: %ddBm\n", channel->ch_num, channel->max_power);
  }

  return n_channels;
}

#if 0  // NEEDS_PORTING
static void iwl_init_vht_hw_capab(struct iwl_trans* trans, struct iwl_nvm_data* data,
                                  struct ieee80211_sta_vht_cap* vht_cap, uint8_t tx_chains,
                                  uint8_t rx_chains) {
  const struct iwl_cfg* cfg = trans->cfg;
  int num_rx_ants = num_of_ant(rx_chains);
  int num_tx_ants = num_of_ant(tx_chains);

  vht_cap->vht_supported = true;

  vht_cap->cap = IEEE80211_VHT_CAP_SHORT_GI_80 | IEEE80211_VHT_CAP_RXSTBC_1 |
                 IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
                 3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT |
                 max_ampdu_exponent << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

  if (data->vht160_supported) {
    vht_cap->cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ | IEEE80211_VHT_CAP_SHORT_GI_160;
  }

  if (cfg->vht_mu_mimo_supported) {
    vht_cap->cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
  }

  if (cfg->ht_params->ldpc) {
    vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC;
  }

  if (data->sku_cap_mimo_disabled) {
    num_rx_ants = 1;
    num_tx_ants = 1;
  }

  if (num_tx_ants > 1) {
    vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
  } else {
    vht_cap->cap |= IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN;
  }

  switch (iwlwifi_mod_params.amsdu_size) {
    case IWL_AMSDU_DEF:
      if (cfg->mq_rx_supported) {
        vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
      } else {
        vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
      }
      break;
    case IWL_AMSDU_2K:
      if (cfg->mq_rx_supported) {
        vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
      } else {
        WARN(1, "RB size of 2K is not supported by this device\n");
      }
      break;
    case IWL_AMSDU_4K:
      vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
      break;
    case IWL_AMSDU_8K:
      vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
      break;
    case IWL_AMSDU_12K:
      vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
      break;
    default:
      break;
  }

  vht_cap->vht_mcs.rx_mcs_map =
      cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 | IEEE80211_VHT_MCS_SUPPORT_0_9 << 2 |
                  IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |
                  IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |
                  IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 14);

  if (num_rx_ants == 1 || cfg->rx_with_siso_diversity) {
    vht_cap->cap |= IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN;
    /* this works because NOT_SUPPORTED == 3 */
    vht_cap->vht_mcs.rx_mcs_map |= cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << 2);
  }

  vht_cap->vht_mcs.tx_mcs_map = vht_cap->vht_mcs.rx_mcs_map;

	vht_cap->vht_mcs.tx_highest |=
		cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE);
}

static const u8 iwl_vendor_caps[] = {
	0xdd,			/* vendor element */
	0x06,			/* length */
	0x00, 0x17, 0x35,	/* Intel OUI */
	0x08,			/* type (Intel Capabilities) */
	/* followed by 16 bits of capabilities */
#define IWL_VENDOR_CAP_IMPROVED_BF_FDBK_HE	BIT(0)
	IWL_VENDOR_CAP_IMPROVED_BF_FDBK_HE,
	0x00
};

static const struct ieee80211_sband_iftype_data iwl_he_capa[] = {
	{
		.types_mask = BIT(NL80211_IFTYPE_STATION),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS,
				.mac_cap_info[4] =
					IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU |
					IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39,
				.mac_cap_info[5] =
					IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B40 |
					IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B41 |
					IEEE80211_HE_MAC_CAP5_UL_2x996_TONE_RU |
					IEEE80211_HE_MAC_CAP5_HE_DYNAMIC_SM_PS |
					IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ,
				.phy_cap_info[3] =
					IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1,
				.phy_cap_info[4] =
					IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
					IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_8 |
					IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8,
				.phy_cap_info[6] =
					IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
					IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
					IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
				.phy_cap_info[7] =
					IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP |
					IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
				.phy_cap_info[8] =
					IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
					IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
					IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
					IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU |
					IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242,
				.phy_cap_info[9] =
					IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
					IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
					(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_RESERVED <<
					IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_POS),
				.phy_cap_info[10] =
					IEEE80211_HE_PHY_CAP10_HE_MU_M1RU_MAX_LTF,
			},
			/*
			 * Set default Tx/Rx HE MCS NSS Support field.
			 * Indicate support for up to 2 spatial streams and all
			 * MCS, without any special cases
			 */
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xffff),
				.tx_mcs_80p80 = cpu_to_le16(0xffff),
			},
			/*
			 * Set default PPE thresholds, with PPET16 set to 0,
			 * PPET8 set to 7
			 */
			.ppe_thres = {0x61, 0x1c, 0xc7, 0x71},
		},
	},
	{
		.types_mask = BIT(NL80211_IFTYPE_AP),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US,
				.phy_cap_info[3] =
					IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1,
				.phy_cap_info[6] =
					IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
				.phy_cap_info[7] =
					IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
				.phy_cap_info[8] =
					IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
					IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242,
				.phy_cap_info[9] =
					IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_RESERVED
					<< IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_POS,
			},
			/*
			 * Set default Tx/Rx HE MCS NSS Support field.
			 * Indicate support for up to 2 spatial streams and all
			 * MCS, without any special cases
			 */
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xffff),
				.tx_mcs_80p80 = cpu_to_le16(0xffff),
			},
			/*
			 * Set default PPE thresholds, with PPET16 set to 0,
			 * PPET8 set to 7
			 */
			.ppe_thres = {0x61, 0x1c, 0xc7, 0x71},
		},
	},
};

static void iwl_init_he_6ghz_capa(struct iwl_trans *trans,
				  struct iwl_nvm_data *data,
				  struct ieee80211_supported_band *sband,
				  u8 tx_chains, u8 rx_chains)
{
	struct ieee80211_sta_ht_cap ht_cap;
	struct ieee80211_sta_vht_cap vht_cap = {};
	struct ieee80211_sband_iftype_data *iftype_data;
	u16 he_6ghz_capa = 0;
	u32 exp;
	int i;

	if (sband->band != NL80211_BAND_6GHZ)
		return;

	/* grab HT/VHT capabilities and calculate HE 6 GHz capabilities */
	iwl_init_ht_hw_capab(trans, data, &ht_cap, NL80211_BAND_5GHZ,
			     tx_chains, rx_chains);
	WARN_ON(!ht_cap.ht_supported);
	iwl_init_vht_hw_capab(trans, data, &vht_cap, tx_chains, rx_chains);
	WARN_ON(!vht_cap.vht_supported);

	he_6ghz_capa |=
		u16_encode_bits(ht_cap.ampdu_density,
				IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START);
	exp = u32_get_bits(vht_cap.cap,
			   IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
	he_6ghz_capa |=
		u16_encode_bits(exp, IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
	exp = u32_get_bits(vht_cap.cap, IEEE80211_VHT_CAP_MAX_MPDU_MASK);
	he_6ghz_capa |=
		u16_encode_bits(exp, IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN);
	/* we don't support extended_ht_cap_info anywhere, so no RD_RESPONDER */
	if (vht_cap.cap & IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN)
		he_6ghz_capa |= IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS;
	if (vht_cap.cap & IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN)
		he_6ghz_capa |= IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS;

	IWL_DEBUG_EEPROM(trans->dev, "he_6ghz_capa=0x%x\n", he_6ghz_capa);

	/* we know it's writable - we set it before ourselves */
	iftype_data = (void *)(uintptr_t)sband->iftype_data;
	for (i = 0; i < sband->n_iftype_data; i++)
		iftype_data[i].he_6ghz_capa.capa = cpu_to_le16(he_6ghz_capa);
}

static void
iwl_nvm_fixup_sband_iftd(struct iwl_trans *trans,
			 struct ieee80211_supported_band *sband,
			 struct ieee80211_sband_iftype_data *iftype_data,
			 u8 tx_chains, u8 rx_chains,
			 const struct iwl_fw *fw)
{
	bool is_ap = iftype_data->types_mask & BIT(NL80211_IFTYPE_AP);

	/* Advertise an A-MPDU exponent extension based on
	 * operating band
	 */
	if (sband->band != NL80211_BAND_2GHZ)
		iftype_data->he_cap.he_cap_elem.mac_cap_info[3] |=
			IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_1;
	else
		iftype_data->he_cap.he_cap_elem.mac_cap_info[3] |=
			IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3;

	if (is_ap && iwlwifi_mod_params.nvm_file)
		iftype_data->he_cap.he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;

	if ((tx_chains & rx_chains) == ANT_AB) {
		iftype_data->he_cap.he_cap_elem.phy_cap_info[2] |=
			IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ;
		iftype_data->he_cap.he_cap_elem.phy_cap_info[5] |=
			IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2 |
			IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2;
		if (!is_ap)
			iftype_data->he_cap.he_cap_elem.phy_cap_info[7] |=
				IEEE80211_HE_PHY_CAP7_MAX_NC_2;
	} else if (!is_ap) {
		/* If not 2x2, we need to indicate 1x1 in the
		 * Midamble RX Max NSTS - but not for AP mode
		 */
		iftype_data->he_cap.he_cap_elem.phy_cap_info[1] &=
			~IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS;
		iftype_data->he_cap.he_cap_elem.phy_cap_info[2] &=
			~IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS;
		iftype_data->he_cap.he_cap_elem.phy_cap_info[7] |=
			IEEE80211_HE_PHY_CAP7_MAX_NC_1;
	}

	switch (CSR_HW_RFID_TYPE(trans->hw_rf_id)) {
	case IWL_CFG_RF_TYPE_GF:
	case IWL_CFG_RF_TYPE_MR:
	case IWL_CFG_RF_TYPE_MS:
		iftype_data->he_cap.he_cap_elem.phy_cap_info[9] |=
			IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU;
		if (!is_ap)
			iftype_data->he_cap.he_cap_elem.phy_cap_info[9] |=
				IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU;
		break;
	}

	if (fw_has_capa(&fw->ucode_capa, IWL_UCODE_TLV_CAPA_BROADCAST_TWT))
		iftype_data->he_cap.he_cap_elem.mac_cap_info[2] |=
			IEEE80211_HE_MAC_CAP2_BCAST_TWT;

	if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_22000 &&
	    !is_ap) {
		iftype_data->vendor_elems.data = iwl_vendor_caps;
		iftype_data->vendor_elems.len = ARRAY_SIZE(iwl_vendor_caps);
	}
}

static void iwl_init_he_hw_capab(struct iwl_trans *trans,
				 struct iwl_nvm_data *data,
				 struct ieee80211_supported_band *sband,
				 u8 tx_chains, u8 rx_chains,
				 const struct iwl_fw *fw)
{
	struct ieee80211_sband_iftype_data *iftype_data;
	int i;

	/* should only initialize once */
	if (WARN_ON(sband->iftype_data))
		return;

	BUILD_BUG_ON(sizeof(data->iftd.low) != sizeof(iwl_he_capa));
	BUILD_BUG_ON(sizeof(data->iftd.high) != sizeof(iwl_he_capa));

	switch (sband->band) {
	case NL80211_BAND_2GHZ:
		iftype_data = data->iftd.low;
		break;
	case NL80211_BAND_5GHZ:
	case NL80211_BAND_6GHZ:
		iftype_data = data->iftd.high;
		break;
	default:
		WARN_ON(1);
		return;
	}

	memcpy(iftype_data, iwl_he_capa, sizeof(iwl_he_capa));

	sband->iftype_data = iftype_data;
	sband->n_iftype_data = ARRAY_SIZE(iwl_he_capa);

	for (i = 0; i < sband->n_iftype_data; i++)
		iwl_nvm_fixup_sband_iftd(trans, sband, &iftype_data[i],
					 tx_chains, rx_chains, fw);

	iwl_init_he_6ghz_capa(trans, data, sband, tx_chains, rx_chains);
}

#endif  // NEEDS_PORTING

static void iwl_init_sbands(struct iwl_trans* trans, struct iwl_nvm_data* data,
                            const __le16* nvm_ch_flags, uint8_t tx_chains, uint8_t rx_chains,
                            uint32_t sbands_flags) {
  struct device* dev = trans->dev;
  const struct iwl_cfg* cfg = trans->cfg;
  size_t n_used = 0;
  struct ieee80211_supported_band* sband;

  size_t n_channels = iwl_init_channel_map(dev, cfg, data, nvm_ch_flags, sbands_flags);
  sband = &data->bands[WLAN_BAND_TWO_GHZ];
  sband->band = WLAN_BAND_TWO_GHZ;
  sband->bitrates = &iwl_cfg80211_rates[RATES_24_OFFS];
  sband->n_bitrates = N_RATES_24;
  n_used += iwl_init_sband_channels(data, sband, n_channels, WLAN_BAND_TWO_GHZ);

  iwl_init_ht_hw_capab(cfg, data, &sband->ht_cap, WLAN_BAND_TWO_GHZ, tx_chains, rx_chains);

#if 0   // NEEDS_PORTING
  // TODO(84773): HE support.
	if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax) {
		iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains, fw);
  }
#endif  // NEEDS_PORTING

  sband = &data->bands[WLAN_BAND_FIVE_GHZ];
  sband->band = WLAN_BAND_FIVE_GHZ;
  sband->bitrates = &iwl_cfg80211_rates[RATES_52_OFFS];
  sband->n_bitrates = N_RATES_52;
  n_used += iwl_init_sband_channels(data, sband, n_channels, WLAN_BAND_FIVE_GHZ);

  iwl_init_ht_hw_capab(cfg, data, &sband->ht_cap, WLAN_BAND_FIVE_GHZ, tx_chains, rx_chains);

#if 0   // NEEDS_PORTING
  // TODO(36684): Supports VHT (802.11ac)
	if (data->sku_cap_11ac_enable && !iwlwifi_mod_params.disable_11ac) {
		iwl_init_vht_hw_capab(trans, data, &sband->vht_cap, tx_chains, rx_chains);
  }

  // TODO(84773): HE support.
  if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax) {
    iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains, fw);
  }

	/* 6GHz band. */
	sband = &data->bands[NL80211_BAND_6GHZ];
	sband->band = NL80211_BAND_6GHZ;
	/* use the same rates as 5GHz band */
	sband->bitrates = &iwl_cfg80211_rates[RATES_52_OFFS];
	sband->n_bitrates = N_RATES_52;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  NL80211_BAND_6GHZ);

	if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax)
		iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains,
				     fw);
	else
		sband->n_channels = 0;
#endif  // NEEDS_PORTING

  if (n_channels != n_used) {
    IWL_ERR_DEV(dev, "NVM: used only %zu of %zu channels\n", n_used, n_channels);
  }
}

static int iwl_get_sku(const struct iwl_cfg* cfg, const __le16* nvm_sw, const __le16* phy_sku) {
  if (cfg->nvm_type != IWL_NVM_EXT) {
    return le16_to_cpup(nvm_sw + SKU);
  }

  return le32_to_cpup((__le32*)(phy_sku + SKU_FAMILY_8000));
}

static int iwl_get_nvm_version(const struct iwl_cfg* cfg, const __le16* nvm_sw) {
  if (cfg->nvm_type != IWL_NVM_EXT) {
    return le16_to_cpup(nvm_sw + NVM_VERSION);
  } else {
    return le32_to_cpup((__le32*)(nvm_sw + NVM_VERSION_EXT_NVM));
  }
}

static int iwl_get_radio_cfg(const struct iwl_cfg* cfg, const __le16* nvm_sw,
                             const __le16* phy_sku) {
  if (cfg->nvm_type != IWL_NVM_EXT) {
    return le16_to_cpup(nvm_sw + RADIO_CFG);
  }

  return le32_to_cpup((__le32*)(phy_sku + RADIO_CFG_FAMILY_EXT_NVM));
}

static int iwl_get_n_hw_addrs(const struct iwl_cfg* cfg, const __le16* nvm_sw) {
  int n_hw_addr;

  if (cfg->nvm_type != IWL_NVM_EXT) {
    return le16_to_cpup(nvm_sw + N_HW_ADDRS);
  }

  n_hw_addr = le32_to_cpup((__le32*)(nvm_sw + N_HW_ADDRS_FAMILY_8000));

  return n_hw_addr & N_HW_ADDR_MASK;
}

static void iwl_set_radio_cfg(const struct iwl_cfg* cfg, struct iwl_nvm_data* data,
                              uint32_t radio_cfg) {
  if (cfg->nvm_type != IWL_NVM_EXT) {
    data->radio_cfg_type = NVM_RF_CFG_TYPE_MSK(radio_cfg);
    data->radio_cfg_step = NVM_RF_CFG_STEP_MSK(radio_cfg);
    data->radio_cfg_dash = NVM_RF_CFG_DASH_MSK(radio_cfg);
    data->radio_cfg_pnum = NVM_RF_CFG_PNUM_MSK(radio_cfg);
    return;
  }

  /* set the radio configuration for family 8000 */
  data->radio_cfg_type = EXT_NVM_RF_CFG_TYPE_MSK(radio_cfg);
  data->radio_cfg_step = EXT_NVM_RF_CFG_STEP_MSK(radio_cfg);
  data->radio_cfg_dash = EXT_NVM_RF_CFG_DASH_MSK(radio_cfg);
  data->radio_cfg_pnum = EXT_NVM_RF_CFG_FLAVOR_MSK(radio_cfg);
  data->valid_tx_ant = EXT_NVM_RF_CFG_TX_ANT_MSK(radio_cfg);
  data->valid_rx_ant = EXT_NVM_RF_CFG_RX_ANT_MSK(radio_cfg);
}

static void iwl_flip_hw_address(__le32 mac_addr0, __le32 mac_addr1, uint8_t* dest) {
  const uint8_t* hw_addr;

  hw_addr = (const uint8_t*)&mac_addr0;
  dest[0] = hw_addr[3];
  dest[1] = hw_addr[2];
  dest[2] = hw_addr[1];
  dest[3] = hw_addr[0];

  hw_addr = (const uint8_t*)&mac_addr1;
  dest[4] = hw_addr[1];
  dest[5] = hw_addr[0];
}

static void iwl_set_hw_address_from_csr(struct iwl_trans* trans, struct iwl_nvm_data* data) {
  __le32 mac_addr0 = cpu_to_le32(iwl_read32(trans, trans->cfg->csr->mac_addr0_strap));
  __le32 mac_addr1 = cpu_to_le32(iwl_read32(trans, trans->cfg->csr->mac_addr1_strap));

  iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);
  /*
   * If the OEM fused a valid address, use it instead of the one in the
   * OTP
   */
  if (is_valid_ether_addr(data->hw_addr)) {
    return;
  }

  mac_addr0 = cpu_to_le32(iwl_read32(trans, trans->cfg->csr->mac_addr0_otp));
  mac_addr1 = cpu_to_le32(iwl_read32(trans, trans->cfg->csr->mac_addr1_otp));

  iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);
}

static void iwl_set_hw_address_family_8000(struct iwl_trans* trans, const struct iwl_cfg* cfg,
                                           struct iwl_nvm_data* data, const __le16* mac_override,
                                           const __be16* nvm_hw) {
  const uint8_t* hw_addr;

  if (mac_override) {
    static const uint8_t reserved_mac[] = {0x02, 0xcc, 0xaa, 0xff, 0xee, 0x00};

    hw_addr = (const uint8_t*)(mac_override + MAC_ADDRESS_OVERRIDE_EXT_NVM);

    /*
     * Store the MAC address from MAO section.
     * No byte swapping is required in MAO section
     */
    memcpy(data->hw_addr, hw_addr, ETH_ALEN);

    /*
     * Force the use of the OTP MAC address in case of reserved MAC
     * address in the NVM, or if address is given but invalid.
     */
    if (is_valid_ether_addr(data->hw_addr) && memcmp(reserved_mac, hw_addr, ETH_ALEN) != 0) {
      return;
    }

    IWL_ERR(trans, "mac address from nvm override section is not valid\n");
  }

  if (nvm_hw) {
    /* read the mac address from WFMP registers */
    __le32 mac_addr0 = cpu_to_le32(iwl_trans_read_prph(trans, WFMP_MAC_ADDR_0));
    __le32 mac_addr1 = cpu_to_le32(iwl_trans_read_prph(trans, WFMP_MAC_ADDR_1));

    iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);

    return;
  }

  IWL_ERR(trans, "mac address is not found\n");
}

static zx_status_t iwl_set_hw_address(struct iwl_trans* trans, const struct iwl_cfg* cfg,
                                      struct iwl_nvm_data* data, const __be16* nvm_hw,
                                      const __le16* mac_override) {
#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  struct iwl_dbg_cfg* dbg_cfg = &trans->dbg_cfg;

  if (dbg_cfg->hw_address.len) {
    if (dbg_cfg->hw_address.len == ETH_ALEN && is_valid_ether_addr(dbg_cfg->hw_address.data)) {
      memcpy(data->hw_addr, dbg_cfg->hw_address.data, ETH_ALEN);
      return 0;
    }
    IWL_ERR(trans, "mac address from config file is invalid\n");
  }
#endif
  if (cfg->mac_addr_from_csr) {
    iwl_set_hw_address_from_csr(trans, data);
  } else if (cfg->nvm_type != IWL_NVM_EXT) {
    const uint8_t* hw_addr = (const uint8_t*)(nvm_hw + HW_ADDR);

    /* The byte order is little endian 16 bit, meaning 214365 */
    data->hw_addr[0] = hw_addr[1];
    data->hw_addr[1] = hw_addr[0];
    data->hw_addr[2] = hw_addr[3];
    data->hw_addr[3] = hw_addr[2];
    data->hw_addr[4] = hw_addr[5];
    data->hw_addr[5] = hw_addr[4];
  } else {
    iwl_set_hw_address_family_8000(trans, cfg, data, mac_override, nvm_hw);
  }

  if (!is_valid_ether_addr(data->hw_addr)) {
    IWL_ERR(trans, "no valid mac address was found\n");
    return ZX_ERR_INVALID_ARGS;
  }

  IWL_INFO(trans, "base HW address: %02x:%02x:%02x:%02x:%02x:%02x\n", data->hw_addr[0],
           data->hw_addr[1], data->hw_addr[2], data->hw_addr[3], data->hw_addr[4],
           data->hw_addr[5]);

  return ZX_OK;
}

static bool iwl_nvm_no_wide_in_5ghz(struct device* dev, const struct iwl_cfg* cfg,
                                    const __be16* nvm_hw) {
  /*
   * Workaround a bug in Indonesia SKUs where the regulatory in
   * some 7000-family OTPs erroneously allow wide channels in
   * 5GHz.  To check for Indonesia, we take the SKU value from
   * bits 1-4 in the subsystem ID and check if it is either 5 or
   * 9.  In those cases, we need to force-disable wide channels
   * in 5GHz otherwise the FW will throw a sysassert when we try
   * to use them.
   */
  if (cfg->device_family == IWL_DEVICE_FAMILY_7000) {
    /*
     * Unlike the other sections in the NVM, the hw
     * section uses big-endian.
     */
    uint16_t subsystem_id = (uint16_t)be16_to_cpup(nvm_hw + SUBSYSTEM_ID);
    uint8_t sku = (subsystem_id & 0x1e) >> 1;

    if (sku == 5 || sku == 9) {
      IWL_DEBUG_EEPROM(dev, "disabling wide channels in 5GHz (0x%0x %d)\n", subsystem_id, sku);
      return true;
    }
  }

  return false;
}

struct iwl_nvm_data* iwl_parse_nvm_data(struct iwl_trans* trans, const struct iwl_cfg* cfg,
                                        const __be16* nvm_hw, const __le16* nvm_sw,
                                        const __le16* nvm_calib, const __le16* regulatory,
                                        const __le16* mac_override, const __le16* phy_sku,
                                        uint8_t tx_chains, uint8_t rx_chains,
                                        bool lar_fw_supported) {
  struct device* dev = trans->dev;
  struct iwl_nvm_data* data;
  bool lar_enabled;
  uint32_t sku, radio_cfg;
  uint32_t sbands_flags = 0;
  uint16_t lar_config;
  const __le16* ch_section;

  if (cfg->nvm_type != IWL_NVM_EXT) {
    data = calloc(1, sizeof(*data) + sizeof(struct ieee80211_channel) * IWL_NVM_NUM_CHANNELS);
  } else {
    data = calloc(1, sizeof(*data) + sizeof(struct ieee80211_channel) * IWL_NVM_NUM_CHANNELS_EXT);
  }
  if (!data) {
    return NULL;
  }

  data->nvm_version = iwl_get_nvm_version(cfg, nvm_sw);

  radio_cfg = iwl_get_radio_cfg(cfg, nvm_sw, phy_sku);
  iwl_set_radio_cfg(cfg, data, radio_cfg);
  if (data->valid_tx_ant) {
    tx_chains &= data->valid_tx_ant;
  }
  if (data->valid_rx_ant) {
    rx_chains &= data->valid_rx_ant;
  }

  sku = iwl_get_sku(cfg, nvm_sw, phy_sku);

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  if (trans->dbg_cfg.disable_52GHz) { /* remove support for 5.2 */
    sku &= ~NVM_SKU_CAP_BAND_52GHZ;
  }
  if (trans->dbg_cfg.disable_24GHz) { /* remove support for 2.4 */
    sku &= ~NVM_SKU_CAP_BAND_24GHZ;
  }
#endif

  data->sku_cap_band_24ghz_enable = sku & NVM_SKU_CAP_BAND_24GHZ;
  data->sku_cap_band_52ghz_enable = sku & NVM_SKU_CAP_BAND_52GHZ;
  data->sku_cap_11n_enable = sku & NVM_SKU_CAP_11N_ENABLE;
  if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_ALL) {
    data->sku_cap_11n_enable = false;
  }
  data->sku_cap_11ac_enable = data->sku_cap_11n_enable && (sku & NVM_SKU_CAP_11AC_ENABLE);
  data->sku_cap_mimo_disabled = sku & NVM_SKU_CAP_MIMO_DISABLE;

  data->n_hw_addrs = iwl_get_n_hw_addrs(cfg, nvm_sw);

  if (cfg->nvm_type != IWL_NVM_EXT) {
    /* Checking for required sections */
    if (!nvm_calib) {
      IWL_ERR(trans, "Can't parse empty Calib NVM sections\n");
      free(data);
      return NULL;
    }

    ch_section =
        cfg->nvm_type == IWL_NVM_SDP ? &regulatory[NVM_CHANNELS_SDP] : &nvm_sw[NVM_CHANNELS];

    lar_enabled = true;
  } else {
    uint16_t lar_offset = data->nvm_version < 0xE39 ? NVM_LAR_OFFSET_OLD : NVM_LAR_OFFSET;

    lar_config = le16_to_cpup(regulatory + lar_offset);
    data->lar_enabled = !!(lar_config & NVM_LAR_ENABLED);
    lar_enabled = data->lar_enabled;
    ch_section = &regulatory[NVM_CHANNELS_EXTENDED];
  }

  /* If no valid mac address was found - bail out */
  if (ZX_OK != iwl_set_hw_address(trans, cfg, data, nvm_hw, mac_override)) {
    free(data);
    return NULL;
  }

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  iwl_init_he_override(trans, &data->bands[WLAN_BAND_TWO_GHZ]);
  iwl_init_he_override(trans, &data->bands[WLAN_BAND_FIVE_GHZ]);
#endif
  if (lar_fw_supported && lar_enabled) {
    sbands_flags |= IWL_NVM_SBANDS_FLAGS_LAR;
  }

  if (iwl_nvm_no_wide_in_5ghz(dev, cfg, nvm_hw)) {
    sbands_flags |= IWL_NVM_SBANDS_FLAGS_NO_WIDE_IN_5GHZ;
  }

  iwl_init_sbands(trans, data, ch_section, tx_chains, rx_chains, sbands_flags);
  data->calib_version = 255;

  return data;
}

#if 0   // NEEDS_PORTING
static u32 iwl_nvm_get_regdom_bw_flags(const u16 *nvm_chan,
				       int ch_idx, u16 nvm_flags,
				       struct iwl_reg_capa reg_capa,
				       const struct iwl_cfg *cfg)
{
	u32 flags = NL80211_RRF_NO_HT40;

	if (ch_idx < NUM_2GHZ_CHANNELS &&
	    (nvm_flags & NVM_CHANNEL_40MHZ)) {
		if (nvm_chan[ch_idx] <= LAST_2GHZ_HT_PLUS)
			flags &= ~NL80211_RRF_NO_HT40PLUS;
		if (nvm_chan[ch_idx] >= FIRST_2GHZ_HT_MINUS)
			flags &= ~NL80211_RRF_NO_HT40MINUS;
	} else if (nvm_flags & NVM_CHANNEL_40MHZ) {
		if ((ch_idx - NUM_2GHZ_CHANNELS) % 2 == 0)
			flags &= ~NL80211_RRF_NO_HT40PLUS;
		else
			flags &= ~NL80211_RRF_NO_HT40MINUS;
	}

	if (!(nvm_flags & NVM_CHANNEL_80MHZ))
		flags |= NL80211_RRF_NO_80MHZ;
	if (!(nvm_flags & NVM_CHANNEL_160MHZ))
		flags |= NL80211_RRF_NO_160MHZ;

	if (!(nvm_flags & NVM_CHANNEL_ACTIVE))
		flags |= NL80211_RRF_NO_IR;

	if (nvm_flags & NVM_CHANNEL_RADAR)
		flags |= NL80211_RRF_DFS;

	if (nvm_flags & NVM_CHANNEL_INDOOR_ONLY)
		flags |= NL80211_RRF_NO_OUTDOOR;

	/* Set the GO concurrent flag only in case that NO_IR is set.
	 * Otherwise it is meaningless
	 */
	if ((nvm_flags & NVM_CHANNEL_GO_CONCURRENT) &&
	    (flags & NL80211_RRF_NO_IR))
		flags |= NL80211_RRF_GO_CONCURRENT;

	/*
	 * reg_capa is per regulatory domain so apply it for every channel
	 */
	if (ch_idx >= NUM_2GHZ_CHANNELS) {
		if (!reg_capa.allow_40mhz)
			flags |= NL80211_RRF_NO_HT40;

		if (!reg_capa.allow_80mhz)
			flags |= NL80211_RRF_NO_80MHZ;

		if (!reg_capa.allow_160mhz)
			flags |= NL80211_RRF_NO_160MHZ;
	}
	if (reg_capa.disable_11ax)
		flags |= NL80211_RRF_NO_HE;

	return flags;
}

static struct iwl_reg_capa iwl_get_reg_capa(u16 flags, u8 resp_ver)
{
	struct iwl_reg_capa reg_capa;

	if (resp_ver >= REG_CAPA_V2_RESP_VER) {
		reg_capa.allow_40mhz = flags & REG_CAPA_V2_40MHZ_ALLOWED;
		reg_capa.allow_80mhz = flags & REG_CAPA_V2_80MHZ_ALLOWED;
		reg_capa.allow_160mhz = flags & REG_CAPA_V2_160MHZ_ALLOWED;
		reg_capa.disable_11ax = flags & REG_CAPA_V2_11AX_DISABLED;
	} else {
		reg_capa.allow_40mhz = !(flags & REG_CAPA_40MHZ_FORBIDDEN);
		reg_capa.allow_80mhz = flags & REG_CAPA_80MHZ_ALLOWED;
		reg_capa.allow_160mhz = flags & REG_CAPA_160MHZ_ALLOWED;
		reg_capa.disable_11ax = flags & REG_CAPA_11AX_DISABLED;
	}
	return reg_capa;
}
#endif  // NEEDS_PORTING

// This function will parse the regulatory data and store in the mvm->mcc_info.
void iwl_parse_nvm_mcc_info(struct mcc_info* mcc_info, const struct iwl_cfg* cfg, int num_of_ch,
                            const __le32* channels, uint16_t fw_mcc, uint16_t geo_info) {
  int ch_idx;
  const uint8_t* nvm_chan = cfg->nvm_type == IWL_NVM_EXT ? iwl_ext_nvm_channels : iwl_nvm_channels;
  int max_num_ch = cfg->nvm_type == IWL_NVM_EXT ? IWL_NVM_NUM_CHANNELS_EXT : IWL_NVM_NUM_CHANNELS;

  if (num_of_ch > max_num_ch) {
    IWL_WARN(mcc_info, "num_of_ch is restricted from %d to %d\n", num_of_ch, max_num_ch);
    num_of_ch = max_num_ch;
  }

  IWL_DEBUG_DEV(mcc_info, IWL_DL_LAR, "building regdom for %d channels\n", num_of_ch);

  memset(mcc_info, 0, sizeof(*mcc_info));
  ZX_ASSERT(((size_t)num_of_ch) <= MAX_MCC_INFO_CH);
  mcc_info->num_ch = num_of_ch;

  /* set alpha2 from FW. */
  mcc_info->country.alpha2[0] = fw_mcc >> 8;
  mcc_info->country.alpha2[1] = fw_mcc & 0xff;

  for (ch_idx = 0; ch_idx < num_of_ch; ch_idx++) {
    uint16_t ch_flags = (uint16_t)le32_to_cpup(channels + ch_idx);

    iwl_nvm_print_channel_flags(NULL, IWL_DL_LAR, nvm_chan[ch_idx], ch_flags);
    mcc_info->channels[ch_idx] = nvm_chan[ch_idx];
    mcc_info->ch_flags[ch_idx] = ch_flags;

#if 0   // NEEDS_PORTING
    reg_query_regdb_wmm(regd->alpha2, center_freq, rule);
#endif  // NEEDS_PORTING
  }
}

bool reg_channel_is_txable(struct mcc_info* mcc_info, uint8_t ch_num) {
  for (size_t k = 0; k < mcc_info->num_ch; k++) {
    if (ch_num == mcc_info->channels[k]) {
      uint32_t ch_flags = mcc_info->ch_flags[k];

      return (ch_flags & NVM_CHANNEL_VALID) && (ch_flags & NVM_CHANNEL_ACTIVE);
    }
  }

  return false;
}

size_t reg_filter_channels(bool active_scan, struct mcc_info* mcc_info, size_t num_ch,
                           const uint8_t* ch_list, uint8_t* out_list) {
  size_t i, j = 0;  // i as input while j is output.

  // If this is a passive, we just copy the list. No filtering.
  if (!active_scan) {
    memcpy(out_list, ch_list, num_ch * sizeof(ch_list[0]));
    return num_ch;
  }

  // If just one channel is specified, this implies that the upper layer learns that this channel
  // is allowed to use (for example, from a passive scan in the DFS channels).  In this case, we
  // allow this channel to be used for active scan.
  if (num_ch == 1) {
    memcpy(out_list, ch_list, num_ch * sizeof(ch_list[0]));
    return num_ch;
  }

  if (!num_ch) {
    // wildcard. search in the current channel list.
    ch_list = mcc_info->channels;
    num_ch = mcc_info->num_ch;
  }

  for (i = 0; i < num_ch; i++) {
    if (reg_channel_is_txable(mcc_info, ch_list[i])) {
      out_list[j++] = ch_list[i];
    }
  }

  return j;
}

void iwl_nvm_fixups(uint32_t hw_id, unsigned int section, uint8_t* data, unsigned int len) {
#define IWL_4165_DEVICE_ID 0x5501
#define NVM_SKU_CAP_MIMO_DISABLE BIT(5)

  if (section == NVM_SECTION_TYPE_PHY_SKU && hw_id == IWL_4165_DEVICE_ID && data && len >= 5 &&
      (data[4] & NVM_SKU_CAP_MIMO_DISABLE))
  /* OTP 0x52 bug work around: it's a 1x1 device */
  {
    data[3] = ANT_B | (ANT_B << 4);
  }
}

#if 0  // NEEDS_PORTING
/*
 * Reads external NVM from a file into mvm->nvm_sections
 *
 * HOW TO CREATE THE NVM FILE FORMAT:
 * ------------------------------
 * 1. create hex file, format:
 *      3800 -> header
 *      0000 -> header
 *      5a40 -> data
 *
 *   rev - 6 bit (word1)
 *   len - 10 bit (word1)
 *   id - 4 bit (word2)
 *   rsv - 12 bit (word2)
 *
 * 2. flip 8bits with 8 bits per line to get the right NVM file format
 *
 * 3. create binary file from the hex file
 *
 * 4. save as "iNVM_xxx.bin" under /lib/firmware
 */
int iwl_read_external_nvm(struct iwl_trans* trans, const char* nvm_file_name,
                          struct iwl_nvm_section* nvm_sections) {
  int ret, section_size;
  uint16_t section_id;
  const struct firmware* fw_entry;
  const struct {
    __le16 word1;
    __le16 word2;
    uint8_t data[];
  } * file_sec;
  const uint8_t* eof;
  uint8_t* temp;
  int max_section_size;
  const __le32* dword_buff;

#define NVM_WORD1_LEN(x) (8 * (x & 0x03FF))
#define NVM_WORD2_ID(x) (x >> 12)
#define EXT_NVM_WORD2_LEN(x) (2 * (((x)&0xFF) << 8 | (x) >> 8))
#define EXT_NVM_WORD1_ID(x) ((x) >> 4)
#define NVM_HEADER_0 (0x2A504C54)
#define NVM_HEADER_1 (0x4E564D2A)
#define NVM_HEADER_SIZE (4 * sizeof(uint32_t))

  IWL_DEBUG_EEPROM(trans->dev, "Read from external NVM\n");

  /* Maximal size depends on NVM version */
  if (trans->cfg->nvm_type != IWL_NVM_EXT) {
    max_section_size = IWL_MAX_NVM_SECTION_SIZE;
  } else {
    max_section_size = IWL_MAX_EXT_NVM_SECTION_SIZE;
  }

  /*
   * Obtain NVM image via request_firmware. Since we already used
   * request_firmware_nowait() for the firmware binary load and only
   * get here after that we assume the NVM request can be satisfied
   * synchronously.
   */
  ret = request_firmware(&fw_entry, nvm_file_name, trans->dev);
  if (ret) {
    IWL_ERR(trans, "ERROR: %s isn't available %d\n", nvm_file_name, ret);
    return ret;
  }

  IWL_INFO(trans, "Loaded NVM file %s (%zu bytes)\n", nvm_file_name, fw_entry->size);

  if (fw_entry->size > MAX_NVM_FILE_LEN) {
    IWL_ERR(trans, "NVM file too large\n");
    ret = -EINVAL;
    goto out;
  }

  eof = fw_entry->data + fw_entry->size;
  dword_buff = (__le32*)fw_entry->data;

  /* some NVM file will contain a header.
   * The header is identified by 2 dwords header as follow:
   * dword[0] = 0x2A504C54
   * dword[1] = 0x4E564D2A
   *
   * This header must be skipped when providing the NVM data to the FW.
   */
  if (fw_entry->size > NVM_HEADER_SIZE && dword_buff[0] == cpu_to_le32(NVM_HEADER_0) &&
      dword_buff[1] == cpu_to_le32(NVM_HEADER_1)) {
    file_sec = (void*)(fw_entry->data + NVM_HEADER_SIZE);
    IWL_INFO(trans, "NVM Version %08X\n", le32_to_cpu(dword_buff[2]));
    IWL_INFO(trans, "NVM Manufacturing date %08X\n", le32_to_cpu(dword_buff[3]));

    /* nvm file validation, dword_buff[2] holds the file version */
		if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_8000 &&
		    trans->hw_rev_step == SILICON_C_STEP &&
		    le32_to_cpu(dword_buff[2]) < 0xE4A) {
      ret = -EFAULT;
      goto out;
    }
  } else {
    file_sec = (void*)fw_entry->data;
  }

  while (true) {
    if (file_sec->data > eof) {
      IWL_ERR(trans, "ERROR - NVM file too short for section header\n");
      ret = -EINVAL;
      break;
    }

    /* check for EOF marker */
    if (!file_sec->word1 && !file_sec->word2) {
      ret = 0;
      break;
    }

    if (trans->cfg->nvm_type != IWL_NVM_EXT) {
      section_size = 2 * NVM_WORD1_LEN(le16_to_cpu(file_sec->word1));
      section_id = NVM_WORD2_ID(le16_to_cpu(file_sec->word2));
    } else {
      section_size = 2 * EXT_NVM_WORD2_LEN(le16_to_cpu(file_sec->word2));
      section_id = EXT_NVM_WORD1_ID(le16_to_cpu(file_sec->word1));
    }

    if (section_size > max_section_size) {
      IWL_ERR(trans, "ERROR - section too large (%d)\n", section_size);
      ret = -EINVAL;
      break;
    }

    if (!section_size) {
      IWL_ERR(trans, "ERROR - section empty\n");
      ret = -EINVAL;
      break;
    }

    if (file_sec->data + section_size > eof) {
      IWL_ERR(trans, "ERROR - NVM file too short for section (%d bytes)\n", section_size);
      ret = -EINVAL;
      break;
    }

    if (WARN(section_id >= NVM_MAX_NUM_SECTIONS, "Invalid NVM section ID %d\n", section_id)) {
      ret = -EINVAL;
      break;
    }

    temp = kmemdup(file_sec->data, section_size, GFP_KERNEL);
    if (!temp) {
      ret = -ENOMEM;
      break;
    }

    iwl_nvm_fixups(trans->hw_id, section_id, temp, section_size);

    kfree(nvm_sections[section_id].data);
    nvm_sections[section_id].data = temp;
    nvm_sections[section_id].length = section_size;

    /* advance to the next section */
    file_sec = (void*)(file_sec->data + section_size);
  }
out:
  release_firmware(fw_entry);
  return ret;
}
IWL_EXPORT_SYMBOL(iwl_read_external_nvm);

struct iwl_nvm_data *iwl_get_nvm(struct iwl_trans *trans,
				 const struct iwl_fw *fw)
{
	struct iwl_nvm_get_info cmd = {};
	struct iwl_nvm_data *nvm;
	struct iwl_host_cmd hcmd = {
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &cmd, },
		.len = { sizeof(cmd) },
		.id = WIDE_ID(REGULATORY_AND_NVM_GROUP, NVM_GET_INFO)
	};
	int  ret;
	bool empty_otp;
	u32 mac_flags;
	u32 sbands_flags = 0;
	/*
	 * All the values in iwl_nvm_get_info_rsp v4 are the same as
	 * in v3, except for the channel profile part of the
	 * regulatory.  So we can just access the new struct, with the
	 * exception of the latter.
	 */
	struct iwl_nvm_get_info_rsp *rsp;
	struct iwl_nvm_get_info_rsp_v3 *rsp_v3;
	bool v4 = fw_has_api(&fw->ucode_capa,
			     IWL_UCODE_TLV_API_REGULATORY_NVM_INFO);
	size_t rsp_size = v4 ? sizeof(*rsp) : sizeof(*rsp_v3);
	void *channel_profile;

	ret = iwl_trans_send_cmd(trans, &hcmd);
	if (ret)
		return ERR_PTR(ret);

	if (WARN(iwl_rx_packet_payload_len(hcmd.resp_pkt) != rsp_size,
		 "Invalid payload len in NVM response from FW %d",
		 iwl_rx_packet_payload_len(hcmd.resp_pkt))) {
		ret = -EINVAL;
		goto out;
	}

	rsp = (void *)hcmd.resp_pkt->data;
	empty_otp = !!(le32_to_cpu(rsp->general.flags) &
		       NVM_GENERAL_FLAGS_EMPTY_OTP);
	if (empty_otp)
		IWL_INFO(trans, "OTP is empty\n");

	nvm = kzalloc(struct_size(nvm, channels, IWL_NUM_CHANNELS), GFP_KERNEL);
	if (!nvm) {
		ret = -ENOMEM;
		goto out;
	}

	iwl_set_hw_address_from_csr(trans, nvm);
	/* TODO: if platform NVM has MAC address - override it here */

	if (!is_valid_ether_addr(nvm->hw_addr)) {
		IWL_ERR(trans, "no valid mac address was found\n");
		ret = -EINVAL;
		goto err_free;
	}

	IWL_INFO(trans, "base HW address: %pM\n", nvm->hw_addr);

	/* Initialize general data */
	nvm->nvm_version = le16_to_cpu(rsp->general.nvm_version);
	nvm->n_hw_addrs = rsp->general.n_hw_addrs;
	if (nvm->n_hw_addrs == 0)
		IWL_WARN(trans,
			 "Firmware declares no reserved mac addresses. OTP is empty: %d\n",
			 empty_otp);

	/* Initialize MAC sku data */
	mac_flags = le32_to_cpu(rsp->mac_sku.mac_sku_flags);
	nvm->sku_cap_11ac_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_802_11AC_ENABLED);
	nvm->sku_cap_11n_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_802_11N_ENABLED);
	nvm->sku_cap_11ax_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_802_11AX_ENABLED);
	nvm->sku_cap_band_24ghz_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED);
	nvm->sku_cap_band_52ghz_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED);
	nvm->sku_cap_mimo_disabled =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_MIMO_DISABLED);

	/* Initialize PHY sku data */
	nvm->valid_tx_ant = (u8)le32_to_cpu(rsp->phy_sku.tx_chains);
	nvm->valid_rx_ant = (u8)le32_to_cpu(rsp->phy_sku.rx_chains);

	if (le32_to_cpu(rsp->regulatory.lar_enabled) &&
	    fw_has_capa(&fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_LAR_SUPPORT)) {
		nvm->lar_enabled = true;
		sbands_flags |= IWL_NVM_SBANDS_FLAGS_LAR;
	}

	rsp_v3 = (void *)rsp;
	channel_profile = v4 ? (void *)rsp->regulatory.channel_profile :
			  (void *)rsp_v3->regulatory.channel_profile;

	iwl_init_sbands(trans, nvm,
			channel_profile,
			nvm->valid_tx_ant & fw->valid_tx_ant,
			nvm->valid_rx_ant & fw->valid_rx_ant,
			sbands_flags, v4, fw);

	iwl_free_resp(&hcmd);
	return nvm;

err_free:
	kfree(nvm);
out:
	iwl_free_resp(&hcmd);
	return ERR_PTR(ret);
}
IWL_EXPORT_SYMBOL(iwl_get_nvm);
#endif  // NEEDS_PORTING
