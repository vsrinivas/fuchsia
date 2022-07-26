/**
 * @file mlan_cfp.c
 *
 *  @brief This file contains WLAN client mode channel, frequency and power
 *  related code
 *
 *
 *  Copyright 2009-2021 NXP
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ASIS AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*************************************************************
 * Change Log:
 *	04/16/2009: initial version
 ************************************************************/

#include "mlan.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_join.h"
#include "mlan_main.h"

/********************************************************
 *			Local Variables
 ********************************************************/

/** 100mW */
#define WLAN_TX_PWR_DEFAULT 20
/** 100mW */
#define WLAN_TX_PWR_00_DEFAULT 20
/** 100mW */
#define WLAN_TX_PWR_US_DEFAULT 20
/** 100mW */
#define WLAN_TX_PWR_JP_BG_DEFAULT 20
/** 200mW */
#define WLAN_TX_PWR_JP_A_DEFAULT 23
/** 100mW */
#define WLAN_TX_PWR_EMEA_DEFAULT 20
/** 2000mW */
#define WLAN_TX_PWR_CN_2000MW 33
/** 200mW */
#define WLAN_TX_PWR_200MW 23
/** 1000mW */
#define WLAN_TX_PWR_1000MW 30
/** 250mW */
#define WLAN_TX_PWR_250MW 24

/** Region code mapping */
typedef struct _country_code_mapping {
	/** Region */
	t_u8 country_code[COUNTRY_CODE_LEN];
	/** Code for B/G CFP table */
	t_u8 cfp_code_bg;
	/** Code for A CFP table */
	t_u8 cfp_code_a;
} country_code_mapping_t;

#define EU_CFP_CODE_BG 0x30
#define EU_CFP_CODE_A 0x30

/** Region code mapping table */
static country_code_mapping_t country_code_mapping[] = {
	{"WW", 0x00, 0x00},	/* World       */
	{"US", 0x10, 0x10},	/* US FCC      */
	{"CA", 0x10, 0x20},	/* IC Canada   */
	{"SG", 0x10, 0x10},	/* Singapore   */
	{"EU", 0x30, 0x30},	/* ETSI        */
	{"AU", 0x30, 0x30},	/* Australia   */
	{"KR", 0x30, 0x30},	/* Republic Of Korea */
	{"JP", 0xFF, 0x40},	/* Japan       */
	{"CN", 0x30, 0x50},	/* China       */
	{"BR", 0x01, 0x09},	/* Brazil      */
	{"RU", 0x30, 0x0f},	/* Russia      */
	{"IN", 0x10, 0x06},	/* India       */
	{"MY", 0x30, 0x06},	/* Malaysia    */
	{"NZ", 0x30, 0x30},	/* New Zeland  */
	{"MX", 0x10, 0x07},	/* Mexico */
};

/** Country code for ETSI */
static t_u8 eu_country_code_table[][COUNTRY_CODE_LEN] = {
	"AL", "AD", "AT", "AU", "BY", "BE", "BA", "BG", "HR", "CY", "CZ", "DK",
	"EE", "FI", "FR", "MK", "DE", "GR", "HU", "IS", "IE", "IT", "KR", "LV",
	"LI", "LT", "LU", "MT", "MD", "MC", "ME", "NL", "NO", "PL", "RO", "RU",
	"SM", "RS", "SI", "SK", "ES", "SE", "CH", "TR", "UA", "UK", "GB", "NZ"
};

/**
 * The structure for Channel-Frequency-Power table
 */
typedef struct _cfp_table {
	/** Region or Code */
	t_u8 code;
	/** Frequency/Power */
	chan_freq_power_t *cfp;
	/** No of CFP flag */
	int cfp_no;
} cfp_table_t;

/* Format { Channel, Frequency (MHz), MaxTxPower } */
/** Band : 'B/G', Region: World Wide Safe */
static chan_freq_power_t channel_freq_power_00_BG[] = {
	{1, 2412, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{2, 2417, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{3, 2422, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{4, 2427, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{5, 2432, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{6, 2437, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{7, 2442, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{8, 2447, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{9, 2452, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{10, 2457, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{11, 2462, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{12, 2467, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x1d, 0, 0}},
	{13, 2472, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x1d, 0, 0}}
};

/* Format { Channel, Frequency (MHz), MaxTxPower } */
/** Band: 'B/G', Region: USA FCC/Canada IC */
static chan_freq_power_t channel_freq_power_US_BG[] = {
	{1, 2412, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{2, 2417, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{3, 2422, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{4, 2427, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{5, 2432, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{6, 2437, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{7, 2442, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{8, 2447, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{9, 2452, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{10, 2457, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{11, 2462, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x1c, 0, 0}}
};

/** Band: 'B/G', Region: Europe ETSI/China */
static chan_freq_power_t channel_freq_power_EU_BG[] = {
	{1, 2412, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{2, 2417, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{3, 2422, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{4, 2427, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{5, 2432, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{6, 2437, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{7, 2442, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{8, 2447, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{9, 2452, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{10, 2457, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{11, 2462, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{12, 2467, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1d, 0, 0}},
	{13, 2472, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x1d, 0, 0}}
};

/** Band: 'B/G', Region: Japan */
static chan_freq_power_t channel_freq_power_JPN41_BG[] = {
	{1, 2412, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{2, 2417, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{3, 2422, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{4, 2427, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{5, 2432, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{6, 2437, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{7, 2442, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{8, 2447, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{9, 2452, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{10, 2457, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{11, 2462, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{12, 2467, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1d, 0, 0}},
	{13, 2472, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1d, 0, 0}}
};

/** Band: 'B/G', Region: Japan */
static chan_freq_power_t channel_freq_power_JPN40_BG[] = {
	{14, 2484, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1d, 0, 0}}
};

/** Band: 'B/G', Region: Japan */
static chan_freq_power_t channel_freq_power_JPNFE_BG[] = {
	{1, 2412, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{2, 2417, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{3, 2422, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{4, 2427, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{5, 2432, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{6, 2437, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{7, 2442, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{8, 2447, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{9, 2452, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{10, 2457, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{11, 2462, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{12, 2467, WLAN_TX_PWR_JP_BG_DEFAULT, MTRUE, {0x1d, 0, 0}},
	{13, 2472, WLAN_TX_PWR_JP_BG_DEFAULT, MTRUE, {0x1d, 0, 0}}
};

/** Band : 'B/G', Region: Brazil */
static chan_freq_power_t channel_freq_power_BR_BG[] = {
	{1, 2412, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{2, 2417, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{3, 2422, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{4, 2427, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{5, 2432, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{6, 2437, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{7, 2442, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{8, 2447, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{9, 2452, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{10, 2457, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{11, 2462, WLAN_TX_PWR_1000MW, MFALSE, {0x1c, 0, 0}},
	{12, 2467, WLAN_TX_PWR_1000MW, MFALSE, {0x1d, 0, 0}},
	{13, 2472, WLAN_TX_PWR_1000MW, MFALSE, {0x1d, 0, 0}}
};

/** Band : 'B/G', Region: Special */
static chan_freq_power_t channel_freq_power_SPECIAL_BG[] = {
	{1, 2412, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{2, 2417, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{3, 2422, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{4, 2427, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{5, 2432, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{6, 2437, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{7, 2442, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{8, 2447, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{9, 2452, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{10, 2457, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{11, 2462, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1c, 0, 0}},
	{12, 2467, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1d, 0, 0}},
	{13, 2472, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1d, 0, 0}},
	{14, 2484, WLAN_TX_PWR_JP_BG_DEFAULT, MFALSE, {0x1d, 0, 0}}
};

/**
 * The 2.4GHz CFP tables
 */
static cfp_table_t cfp_table_BG[] = {
	{
	 0x01,			/* Brazil */
	 channel_freq_power_BR_BG,
	 NELEMENTS(channel_freq_power_BR_BG),
	 },
	{
	 0x00,			/* World FCC */
	 channel_freq_power_00_BG,
	 NELEMENTS(channel_freq_power_00_BG),
	 },
	{
	 0x10,			/* US FCC */
	 channel_freq_power_US_BG,
	 NELEMENTS(channel_freq_power_US_BG),
	 },
	{
	 0x20,			/* CANADA IC */
	 channel_freq_power_US_BG,
	 NELEMENTS(channel_freq_power_US_BG),
	 },
	{
	 0x30,			/* EU */
	 channel_freq_power_EU_BG,
	 NELEMENTS(channel_freq_power_EU_BG),
	 },
	{
	 0x40,			/* JAPAN */
	 channel_freq_power_JPN40_BG,
	 NELEMENTS(channel_freq_power_JPN40_BG),
	 },
	{
	 0x41,			/* JAPAN */
	 channel_freq_power_JPN41_BG,
	 NELEMENTS(channel_freq_power_JPN41_BG),
	 },
	{
	 0x50,			/* China */
	 channel_freq_power_EU_BG,
	 NELEMENTS(channel_freq_power_EU_BG),
	 },
	{
	 0xfe,			/* JAPAN */
	 channel_freq_power_JPNFE_BG,
	 NELEMENTS(channel_freq_power_JPNFE_BG),
	 },
	{
	 0xff,			/* Special */
	 channel_freq_power_SPECIAL_BG,
	 NELEMENTS(channel_freq_power_SPECIAL_BG),
	 },
	/* Add new region here */
};

/** Number of the CFP tables for 2.4GHz */
#define MLAN_CFP_TABLE_SIZE_BG (NELEMENTS(cfp_table_BG))

/* Format { Channel, Frequency (MHz), MaxTxPower, DFS } */
/** Band: 'A', Region: World Wide Safe */
static chan_freq_power_t channel_freq_power_00_A[] = {
	{36, 5180, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{100, 5500, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{104, 5520, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{108, 5540, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{112, 5560, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{116, 5580, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{120, 5600, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{124, 5620, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{128, 5640, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{144, 5720, WLAN_TX_PWR_00_DEFAULT, MTRUE, {0x13, 0, 0}},
	{149, 5745, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_00_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/* Format { Channel, Frequency (MHz), MaxTxPower, DFS } */
/** Band: 'A', Region: USA FCC */
static chan_freq_power_t channel_freq_power_A[] = {
	{36, 5180, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{100, 5500, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{104, 5520, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{108, 5540, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{112, 5560, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{116, 5580, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{120, 5600, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{124, 5620, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{128, 5640, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{144, 5720, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{149, 5745, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Region: Canada IC */
static chan_freq_power_t channel_freq_power_CAN_A[] = {
	{36, 5180, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{100, 5500, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{104, 5520, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{108, 5540, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{112, 5560, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{116, 5580, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{144, 5720, WLAN_TX_PWR_US_DEFAULT, MTRUE, {0x13, 0, 0}},
	{149, 5745, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_US_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Region: Europe ETSI */
static chan_freq_power_t channel_freq_power_EU_A[] = {
	{36, 5180, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{100, 5500, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{104, 5520, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{108, 5540, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{112, 5560, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{116, 5580, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{120, 5600, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{124, 5620, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{128, 5640, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{149, 5745, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Region: Japan */
static chan_freq_power_t channel_freq_power_JPN_A[] = {
	{36, 5180, WLAN_TX_PWR_JP_A_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_JP_A_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_JP_A_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_JP_A_DEFAULT, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{100, 5500, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{104, 5520, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{108, 5540, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{112, 5560, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{116, 5580, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{120, 5600, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{124, 5620, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{128, 5640, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}},
	{144, 5720, WLAN_TX_PWR_JP_A_DEFAULT, MTRUE, {0x13, 0, 0}}
};

/** Band: 'A', Region: China */
static chan_freq_power_t channel_freq_power_CN_A[] = {
	{36, 5180, WLAN_TX_PWR_200MW, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_200MW, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_200MW, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_200MW, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_200MW, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_200MW, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_200MW, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_200MW, MTRUE, {0x13, 0, 0}},
	{149, 5745, WLAN_TX_PWR_CN_2000MW, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_CN_2000MW, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_CN_2000MW, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_CN_2000MW, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_CN_2000MW, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', NULL */
static chan_freq_power_t channel_freq_power_NULL_A[] = { };

/** Band: 'A', Region: Spain/Austria/Brazil */
static chan_freq_power_t channel_freq_power_SPN2_A[] = {
	{36, 5180, WLAN_TX_PWR_200MW, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_200MW, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_200MW, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_200MW, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_200MW, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_200MW, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_200MW, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_200MW, MTRUE, {0x13, 0, 0}}
};

/** Band: 'A', Region: Brazil */
static chan_freq_power_t channel_freq_power_BR1_A[] = {
	{100, 5500, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{104, 5520, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{108, 5540, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{112, 5560, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{116, 5580, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{120, 5600, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{124, 5620, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{128, 5640, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_250MW, MTRUE, {0x13, 0, 0}}
};

/** Band: 'A', Region: Brazil */
static chan_freq_power_t channel_freq_power_BR2_A[] = {
	{149, 5745, WLAN_TX_PWR_1000MW, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_1000MW, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_1000MW, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_1000MW, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_1000MW, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Region: Russia */
static chan_freq_power_t channel_freq_power_RU_A[] = {
	{36, 5180, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{149, 5745, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Region: Mexico */
static chan_freq_power_t channel_freq_power_MX_A[] = {
	{36, 5180, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{100, 5500, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{104, 5520, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{108, 5540, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{112, 5560, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{116, 5580, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_EMEA_DEFAULT, MTRUE, {0x13, 0, 0}},
	{149, 5745, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_EMEA_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Code: 1, Low band (5150-5250 MHz) channels */
static chan_freq_power_t channel_freq_power_low_band[] = {
	{36, 5180, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Code: 2, Lower middle band (5250-5350 MHz) channels */
static chan_freq_power_t channel_freq_power_lower_middle_band[] = {
	{52, 5260, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{56, 5280, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{60, 5300, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{64, 5320, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}}
};

/** Band: 'A', Code: 3, Upper middle band (5470-5725 MHz) channels */
static chan_freq_power_t channel_freq_power_upper_middle_band[] = {
	{100, 5500, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{104, 5520, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{108, 5540, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{112, 5560, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{116, 5580, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{120, 5600, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{124, 5620, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{128, 5640, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{132, 5660, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{136, 5680, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}},
	{140, 5700, WLAN_TX_PWR_DEFAULT, MTRUE, {0x13, 0, 0}}
};

/** Band: 'A', Code: 4, High band (5725-5850 MHz) channels */
static chan_freq_power_t channel_freq_power_high_band[] = {
	{149, 5745, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Code: 5, Low band (5150-5250 MHz) and
 *  High band (5725-5850 MHz) channels
 */
static chan_freq_power_t channel_freq_power_low_high_band[] = {
	{36, 5180, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{149, 5745, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/** Band: 'A', Code: 6, Low band (5150-5250 MHz) and
 *  mid low (5260-5320) and High band (5725-5850 MHz) channels
 */
static chan_freq_power_t channel_freq_power_low_middle_high_band[] = {
	{36, 5180, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{40, 5200, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{44, 5220, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{48, 5240, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{52, 5260, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{56, 5280, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{60, 5300, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{64, 5320, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{149, 5745, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{153, 5765, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{157, 5785, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{161, 5805, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}},
	{165, 5825, WLAN_TX_PWR_DEFAULT, MFALSE, {0x10, 0, 0}}
};

/**
 * The 5GHz CFP tables
 */
static cfp_table_t cfp_table_A[] = {
	{0x1,			/* Low band (5150-5250 MHz) channels */
	 channel_freq_power_low_band, NELEMENTS(channel_freq_power_low_band)},
	{0x2,			/* Lower middle band (5250-5350 MHz) channels */
	 channel_freq_power_lower_middle_band,
	 NELEMENTS(channel_freq_power_lower_middle_band)},
	{0x3,			/* Upper middle band (5470-5725 MHz) channels */
	 channel_freq_power_upper_middle_band,
	 NELEMENTS(channel_freq_power_upper_middle_band)},
	{0x4,			/* High band (5725-5850 MHz) channels */
	 channel_freq_power_high_band, NELEMENTS(channel_freq_power_high_band)},
	{0x5,			/* Low band (5150-5250 MHz) and
				 * High band (5725-5850 MHz) channels
				 */
	 channel_freq_power_low_high_band,
	 NELEMENTS(channel_freq_power_low_high_band)},
	{0x6,			/* Low band (5150-5250 MHz)
				 * Mid band (5260-5320) and
				 * High band (5725-5850 MHz) channels
				 */
	 channel_freq_power_low_middle_high_band,
	 NELEMENTS(channel_freq_power_low_middle_high_band)},
	{
	 0x07,			/* Mexico */
	 channel_freq_power_MX_A,
	 NELEMENTS(channel_freq_power_MX_A),
	 },

	{
	 0x09,			/* SPAIN/Austria/Brazil */
	 channel_freq_power_SPN2_A,
	 NELEMENTS(channel_freq_power_SPN2_A),
	 },
	{
	 0x0c,			/* Brazil */
	 channel_freq_power_BR1_A,
	 NELEMENTS(channel_freq_power_BR1_A),
	 },
	{
	 0x0e,			/* Brazil */
	 channel_freq_power_BR2_A,
	 NELEMENTS(channel_freq_power_BR2_A),
	 },
	{
	 0x0f,			/* Russia */
	 channel_freq_power_RU_A,
	 NELEMENTS(channel_freq_power_RU_A),
	 },
	{
	 0x00,			/* World */
	 channel_freq_power_00_A,
	 NELEMENTS(channel_freq_power_00_A),
	 },
	{
	 0x10,			/* US FCC */
	 channel_freq_power_A,
	 NELEMENTS(channel_freq_power_A),
	 },
	{
	 0x20,			/* CANADA IC */
	 channel_freq_power_CAN_A,
	 NELEMENTS(channel_freq_power_CAN_A),
	 },
	{
	 0x30,			/* EU */
	 channel_freq_power_EU_A,
	 NELEMENTS(channel_freq_power_EU_A),
	 },
	{
	 0x40,			/* JAPAN */
	 channel_freq_power_JPN_A,
	 NELEMENTS(channel_freq_power_JPN_A),
	 },
	{
	 0x41,			/* JAPAN */
	 channel_freq_power_JPN_A,
	 NELEMENTS(channel_freq_power_JPN_A),
	 },
	{
	 0x50,			/* China */
	 channel_freq_power_CN_A,
	 NELEMENTS(channel_freq_power_CN_A),
	 },
	{
	 0xfe,			/* JAPAN */
	 channel_freq_power_NULL_A,
	 NELEMENTS(channel_freq_power_NULL_A),
	 },
	{
	 0xff,			/* Special */
	 channel_freq_power_JPN_A,
	 NELEMENTS(channel_freq_power_JPN_A),
	 },
	/* Add new region here */
};

/** Number of the CFP tables for 5GHz */
#define MLAN_CFP_TABLE_SIZE_A (NELEMENTS(cfp_table_A))

enum {
	RATEID_DBPSK1Mbps,	//(0)
	RATEID_DQPSK2Mbps,	//(1)
	RATEID_CCK5_5Mbps,	//(2)
	RATEID_CCK11Mbps,	//(3)
	RATEID_CCK22Mbps,	//(4)
	RATEID_OFDM6Mbps,	//(5)
	RATEID_OFDM9Mbps,	//(6)
	RATEID_OFDM12Mbps,	//(7)
	RATEID_OFDM18Mbps,	//(8)
	RATEID_OFDM24Mbps,	//(9)
	RATEID_OFDM36Mbps,	//(10)
	RATEID_OFDM48Mbps,	//(11)
	RATEID_OFDM54Mbps,	//(12)
	RATEID_OFDM72Mbps,	//(13)
};

static const t_u8 rateUnit_500Kbps[] = {
	(10 / 5),		/* 1Mbps */
	(20 / 5),		/* 2Mbps */

	(55 / 5),		/* 5.5Mbps */
	(110 / 5),		/* 11Mbps */
	(10 / 5),		/* 22Mbps, intentionally set to 1Mbps
				 * because it's not available
				 */

	(60 / 5),		/* 6Mbps */
	(90 / 5),		/* 9Mbps */
	(120 / 5),		/* 12Mbps */
	(180 / 5),		/* 18Mbps */
	(240 / 5),		/* 24Mbps */
	(360 / 5),		/* 36Mbps */
	(480 / 5),		/* 48Mbps */
	(540 / 5),		/* 54Mbps */
	(60 / 5),		/* 72Mbps, intentionally set to 6Mbps
				 * because it's not available
				 */
};

typedef struct _rate_map {
	/** Rate, in 0.5Mbps */
	t_u32 rate;
	/** Mrvl rate id, refer to RATEID_XXX in FW */
	t_u32 id;
	/** nss: 0-nss1, 1-nss2 */
	t_u8 nss;
} rate_map;

/** If user configure to 1x1 or we found peer device only support 1x1,
 * then we need skip the nss1 part when map to Mrvl rate.
 */
static const rate_map rate_map_table_2x2[] = {
	/* LG <--> Mrvl rate idx */
	{2, 0, 0},		// RATEID_DBPSK1Mbps
	{4, 1, 0},		// RATEID_DQPSK2Mbps
	{11, 2, 0},		// RATEID_CCK5_5Mbps
	{22, 3, 0},		// RATEID_CCK11Mbps
	{44, 4, 0},		// RATEID_CCK22Mbps
	{12, 5, 0},		// RATEID_OFDM6Mbps
	{18, 6, 0},		// RATEID_OFDM9Mbps
	{24, 7, 0},		// RATEID_OFDM12Mbps
	{36, 8, 0},		// RATEID_OFDM18Mbps
	{48, 9, 0},		// RATEID_OFDM24Mbps
	{72, 10, 0},		// RATEID_OFDM36Mbps
	{96, 11, 0},		// RATEID_OFDM48Mbps
	{108, 12, 0},		// RATEID_OFDM54Mbps
	{144, 13, 0},		// RATEID_OFDM72Mbps

	/* HT bw20 <--> Mrvl rate idx - nss2 */
	{26, 22, 1},		// RATEID_MCS8_13Mbps
	{52, 23, 1},		// RATEID_MCS9_26Mbps
	{78, 24, 1},		// RATEID_MCS10_39Mbps
	{104, 25, 1},		// RATEID_MCS11_52Mbps
	{156, 26, 1},		// RATEID_MCS12_78Mbps
	{208, 27, 1},		// RATEID_MCS13_104Mbps
	{234, 28, 1},		// RATEID_MCS14_117Mbps
	{260, 29, 1},		// RATEID_MCS15_130Mbps
	/* HT bw20 <--> Mrvl rate idx - nss1 */
	{13, 14, 0},		// RATEID_MCS0_6d5Mbps
	{26, 15, 0},		// RATEID_MCS1_13Mbps
	{39, 16, 0},		// RATEID_MCS2_19d5Mbps
	{52, 17, 0},		// RATEID_MCS3_26Mbps
	{78, 18, 0},		// RATEID_MCS4_39Mbps
	{104, 19, 0},		// RATEID_MCS5_52Mbps
	{117, 20, 0},		// RATEID_MCS6_58d5Mbps
	{130, 21, 0},		// RATEID_MCS7_65Mbps

	/* HT bw40<--> Mrvl rate idx - nss2 */
	{54, 39, 1},		// RATEID_MCS8BW40_27Mbps
	{108, 40, 1},		// RATEID_MCS9BW40_54Mbps
	{162, 41, 1},		// RATEID_MCS10BW40_81Mbps
	{216, 42, 1},		// RATEID_MCS11BW40_108Mbps
	{324, 43, 1},		// RATEID_MCS12BW40_162Mbps
	{432, 44, 1},		// RATEID_MCS13BW40_216Mbps
	{486, 45, 1},		// RATEID_MCS14BW40_243Mbps
	{540, 46, 1},		// RATEID_MCS15BW40_270Mbps
	/* HT bw40<--> Mrvl rate idx - nss1 */
	{12, 30, 0},		// RATEID_MCS32BW40_6Mbps
	{27, 31, 0},		// RATEID_MCS0BW40_13d5Mbps
	{54, 32, 0},		// RATEID_MCS1BW40_27Mbps
	{81, 33, 0},		// RATEID_MCS2BW40_40d5Mbps
	{108, 34, 0},		// RATEID_MCS3BW40_54Mbps
	{162, 35, 0},		// RATEID_MCS4BW40_81Mbps
	{216, 36, 0},		// RATEID_MCS5BW40_108Mbps
	{243, 37, 0},		// RATEID_MCS6BW40_121d5Mbps
	{270, 38, 0},		// RATEID_MCS7BW40_135Mbps

	/* VHT bw20<--> Mrvl rate idx - nss2 */
	{26, 57, 1},		// RATEID_VHT_MCS0_2SS_BW20   13    Mbps
	{52, 58, 1},		// RATEID_VHT_MCS1_2SS_BW20   26    Mbps
	{78, 59, 1},		// RATEID_VHT_MCS2_2SS_BW20   39    Mbps
	{104, 60, 1},		// RATEID_VHT_MCS3_2SS_BW20   52    Mbps
	{156, 61, 1},		// RATEID_VHT_MCS4_2SS_BW20   78    Mbps
	{208, 62, 1},		// RATEID_VHT_MCS5_2SS_BW20   104   Mbps
	{234, 63, 1},		// RATEID_VHT_MCS6_2SS_BW20   117   Mbps
	{260, 64, 1},		// RATEID_VHT_MCS7_2SS_BW20   130   Mbps
	{312, 65, 1},		// RATEID_VHT_MCS8_2SS_BW20   156   Mbps
	{0, 66, 1},		// RATEID_VHT_MCS9_2SS_BW20   173.3 Mbps(INVALID)
	/* VHT bw20<--> Mrvl rate idx - nss1 */
	{13, 47, 0},		// RATEID_VHT_MCS0_1SS_BW20   6.5  Mbps
	{26, 48, 0},		// RATEID_VHT_MCS1_1SS_BW20   13   Mbps
	{39, 49, 0},		// RATEID_VHT_MCS2_1SS_BW20   19.5 Mbps
	{52, 50, 0},		// RATEID_VHT_MCS3_1SS_BW20   26   Mbps
	{78, 51, 0},		// RATEID_VHT_MCS4_1SS_BW20   39   Mbps
	{104, 52, 0},		// RATEID_VHT_MCS5_1SS_BW20   52   Mbps
	{117, 53, 0},		// RATEID_VHT_MCS6_1SS_BW20   58.5 Mbps
	{130, 54, 0},		// RATEID_VHT_MCS7_1SS_BW20   65   Mbps
	{156, 55, 0},		// RATEID_VHT_MCS8_1SS_BW20   78   Mbps
	{0, 56, 0},		// RATEID_VHT_MCS9_1SS_BW20   86.7 Mbps(INVALID)

	/* VHT bw40<--> Mrvl rate idx - nss2 */
	{54, 77, 1},		// RATEID_VHT_MCS0_2SS_BW40   27  Mbps
	{108, 78, 1},		// RATEID_VHT_MCS1_2SS_BW40   54  Mbps
	{162, 79, 1},		// RATEID_VHT_MCS2_2SS_BW40   81  Mbps
	{216, 80, 1},		// RATEID_VHT_MCS3_2SS_BW40   108 Mbps
	{324, 81, 1},		// RATEID_VHT_MCS4_2SS_BW40   162 Mbps
	{432, 82, 1},		// RATEID_VHT_MCS5_2SS_BW40   216 Mbps
	{486, 83, 1},		// RATEID_VHT_MCS6_2SS_BW40   243 Mbps
	{540, 84, 1},		// RATEID_VHT_MCS7_2SS_BW40   270 Mbps
	{648, 85, 1},		// RATEID_VHT_MCS8_2SS_BW40   324 Mbps
	{720, 86, 1},		// RATEID_VHT_MCS9_2SS_BW40   360 Mbps
	/* VHT bw40<--> Mrvl rate idx - nss1 */
	{27, 67, 0},		// RATEID_VHT_MCS0_1SS_BW40   13.5  Mbps
	{54, 68, 0},		// RATEID_VHT_MCS1_1SS_BW40   27    Mbps
	{81, 69, 0},		// RATEID_VHT_MCS2_1SS_BW40   40.5  Mbps
	{108, 70, 0},		// RATEID_VHT_MCS3_1SS_BW40   54    Mbps
	{162, 71, 0},		// RATEID_VHT_MCS4_1SS_BW40   81    Mbps
	{216, 72, 0},		// RATEID_VHT_MCS5_1SS_BW40   108   Mbps
	{243, 73, 0},		// RATEID_VHT_MCS6_1SS_BW40   121.5 Mbps
	{270, 74, 0},		// RATEID_VHT_MCS7_1SS_BW40   135   Mbps
	{324, 75, 0},		// RATEID_VHT_MCS8_1SS_BW40   162   Mbps
	{360, 76, 0},		// RATEID_VHT_MCS9_1SS_BW40   180   Mbps

	/* VHT bw80<--> Mrvl rate idx - nss2 */
	{117, 97, 1},		// RATEID_VHT_MCS0_2SS_BW80   58.5  Mbps
	{234, 98, 1},		// RATEID_VHT_MCS1_2SS_BW80   117   Mbps
	{350, 99, 1},		// RATEID_VHT_MCS2_2SS_BW80   175   Mbps
	{468, 100, 1},		// RATEID_VHT_MCS3_2SS_BW80   234   Mbps
	{702, 101, 1},		// RATEID_VHT_MCS4_2SS_BW80   351   Mbps
	{936, 102, 1},		// RATEID_VHT_MCS5_2SS_BW80   468   Mbps
	{1053, 103, 1},		// RATEID_VHT_MCS6_2SS_BW80   526.5 Mbps
	{1170, 104, 1},		// RATEID_VHT_MCS7_2SS_BW80   585   Mbps
	{1404, 105, 1},		// RATEID_VHT_MCS8_2SS_BW80   702   Mbps
	{1560, 106, 1},		// RATEID_VHT_MCS9_2SS_BW80   780   Mbps
	/* VHT bw80<--> Mrvl rate idx - nss1 */
	{58, 87, 0},		// RATEID_VHT_MCS0_1SS_BW80   29.3  Mbps,  29.3x2 could
	// correspond to 58
	{59, 87, 0},		// RATEID_VHT_MCS0_1SS_BW80   29.3  Mbps,  29.3*2 could
	// correspond to 59 too
	{117, 88, 0},		// RATEID_VHT_MCS1_1SS_BW80   58.5  Mbps
	{175, 89, 0},		// RATEID_VHT_MCS2_1SS_BW80   87.8  Mbps,  87.8x2 could
	// correspond to 175
	{176, 89, 0},		// RATEID_VHT_MCS2_1SS_BW80   87.8  Mbps,  87.8x2 could
	// correspond to 176 too
	{234, 90, 0},		// RATEID_VHT_MCS3_1SS_BW80   117   Mbps
	{351, 91, 0},		// RATEID_VHT_MCS4_1SS_BW80   175.5 Mbps
	{468, 92, 0},		// RATEID_VHT_MCS5_1SS_BW80   234   Mbps
	{526, 93, 0},		// RATEID_VHT_MCS6_1SS_BW80   263.3 Mbps,  263.3x2 could
	// correspond to 526
	{527, 93, 0},		// RATEID_VHT_MCS6_1SS_BW80   263.3 Mbps,  263.3x2 could
	// correspond to 527 too
	{585, 94, 0},		// RATEID_VHT_MCS7_1SS_BW80   292.5 Mbps
	{702, 95, 0},		// RATEID_VHT_MCS8_1SS_BW80   351   Mbps
	{780, 96, 0},		// RATEID_VHT_MCS9_1SS_BW80   390   Mbps
};

/** rate_map_table_1x1 is based on rate_map_table_2x2 and remove nss2 part.
 * For the chip who only support 1x1, Mrvl rate idx define is different with 2x2
 * in FW We need redefine a bitrate to Mrvl rate idx table for 1x1 chip.
 */
static const rate_map rate_map_table_1x1[] = {
	/* LG <--> Mrvl rate idx */
	{2, 0, 0},		// RATEID_DBPSK1Mbps
	{4, 1, 0},		// RATEID_DQPSK2Mbps
	{11, 2, 0},		// RATEID_CCK5_5Mbps
	{22, 3, 0},		// RATEID_CCK11Mbps
	{44, 4, 0},		// RATEID_CCK22Mbps
	{12, 5, 0},		// RATEID_OFDM6Mbps
	{18, 6, 0},		// RATEID_OFDM9Mbps
	{24, 7, 0},		// RATEID_OFDM12Mbps
	{36, 8, 0},		// RATEID_OFDM18Mbps
	{48, 9, 0},		// RATEID_OFDM24Mbps
	{72, 10, 0},		// RATEID_OFDM36Mbps
	{96, 11, 0},		// RATEID_OFDM48Mbps
	{108, 12, 0},		// RATEID_OFDM54Mbps
	{144, 13, 0},		// RATEID_OFDM72Mbps

	/* HT bw20 <--> Mrvl rate idx */
	{13, 14, 0},		// RATEID_MCS0_6d5Mbps
	{26, 15, 0},		// RATEID_MCS1_13Mbps
	{39, 16, 0},		// RATEID_MCS2_19d5Mbps
	{52, 17, 0},		// RATEID_MCS3_26Mbps
	{78, 18, 0},		// RATEID_MCS4_39Mbps
	{104, 19, 0},		// RATEID_MCS5_52Mbps
	{117, 20, 0},		// RATEID_MCS6_58d5Mbps
	{130, 21, 0},		// RATEID_MCS7_65Mbps

	/* HT bw40<--> Mrvl rate idx */
	{12, 22, 0},		// RATEID_MCS32BW40_6Mbps,   for 1x1 start from 22
	{27, 23, 0},		// RATEID_MCS0BW40_13d5Mbps
	{54, 24, 0},		// RATEID_MCS1BW40_27Mbps
	{81, 25, 0},		// RATEID_MCS2BW40_40d5Mbps
	{108, 26, 0},		// RATEID_MCS3BW40_54Mbps
	{162, 27, 0},		// RATEID_MCS4BW40_81Mbps
	{216, 28, 0},		// RATEID_MCS5BW40_108Mbps
	{243, 29, 0},		// RATEID_MCS6BW40_121d5Mbps
	{270, 30, 0},		// RATEID_MCS7BW40_135Mbps

	/* VHT bw20<--> Mrvl rate idx */
	{13, 31, 0},		// RATEID_VHT_MCS0_1SS_BW20   6.5  Mbps
	{26, 32, 0},		// RATEID_VHT_MCS1_1SS_BW20   13   Mbps
	{39, 33, 0},		// RATEID_VHT_MCS2_1SS_BW20   19.5 Mbps
	{52, 34, 0},		// RATEID_VHT_MCS3_1SS_BW20   26   Mbps
	{78, 35, 0},		// RATEID_VHT_MCS4_1SS_BW20   39   Mbps
	{104, 36, 0},		// RATEID_VHT_MCS5_1SS_BW20   52   Mbps
	{117, 37, 0},		// RATEID_VHT_MCS6_1SS_BW20   58.5 Mbps
	{130, 38, 0},		// RATEID_VHT_MCS7_1SS_BW20   65   Mbps
	{156, 39, 0},		// RATEID_VHT_MCS8_1SS_BW20   78   Mbps
	{0, 40, 0},		// RATEID_VHT_MCS9_1SS_BW20   86.7 Mbps(INVALID)

	/* VHT bw40<--> Mrvl rate idx */
	{27, 41, 0},		// RATEID_VHT_MCS0_1SS_BW40   13.5  Mbps
	{54, 42, 0},		// RATEID_VHT_MCS1_1SS_BW40   27    Mbps
	{81, 43, 0},		// RATEID_VHT_MCS2_1SS_BW40   40.5  Mbps
	{108, 44, 0},		// RATEID_VHT_MCS3_1SS_BW40   54    Mbps
	{162, 45, 0},		// RATEID_VHT_MCS4_1SS_BW40   81    Mbps
	{216, 46, 0},		// RATEID_VHT_MCS5_1SS_BW40   108   Mbps
	{243, 47, 0},		// RATEID_VHT_MCS6_1SS_BW40   121.5 Mbps
	{270, 48, 0},		// RATEID_VHT_MCS7_1SS_BW40   135   Mbps
	{324, 49, 0},		// RATEID_VHT_MCS8_1SS_BW40   162   Mbps
	{360, 50, 0},		// RATEID_VHT_MCS9_1SS_BW40   180   Mbps

	/* VHT bw80<--> Mrvl rate idx */
	{58, 51, 0},		// RATEID_VHT_MCS0_1SS_BW80   29.3  Mbps,  29.3x2 could
	// correspond to 58
	{59, 51, 0},		// RATEID_VHT_MCS0_1SS_BW80   29.3  Mbps,  29.3x2 could
	// correspond to 59 too
	{117, 52, 0},		// RATEID_VHT_MCS1_1SS_BW80   58.5  Mbps
	{175, 53, 0},		// RATEID_VHT_MCS2_1SS_BW80   87.8  Mbps,  87.8x2 could
	// correspond to 175
	{176, 53, 0},		// RATEID_VHT_MCS2_1SS_BW80   87.8  Mbps,  87.8x2 could
	// correspond to 176 too
	{234, 54, 0},		// RATEID_VHT_MCS3_1SS_BW80   117   Mbps
	{351, 55, 0},		// RATEID_VHT_MCS4_1SS_BW80   175.5 Mbps
	{468, 56, 0},		// RATEID_VHT_MCS5_1SS_BW80   234   Mbps
	{526, 57, 0},		// RATEID_VHT_MCS6_1SS_BW80   263.3 Mbps,  263.3x2 could
	// correspond to 526
	{527, 57, 0},		// RATEID_VHT_MCS6_1SS_BW80   263.3 Mbps,  263.3x2 could
	// correspond to 527 too
	{585, 58, 0},		// RATEID_VHT_MCS7_1SS_BW80   292.5 Mbps
	{702, 59, 0},		// RATEID_VHT_MCS8_1SS_BW80   351   Mbps
	{780, 60, 0},		// RATEID_VHT_MCS9_1SS_BW80   390   Mbps
};

/********************************************************
 *			Global Variables
 ********************************************************/
/**
 * The table to keep region code
 */

t_u16 region_code_index[MRVDRV_MAX_REGION_CODE] =
	{ 0x00, 0x10, 0x20, 0x30, 0x40,
	0x41, 0x50, 0xfe, 0xff
};

/** The table to keep CFP code for BG */
t_u16 cfp_code_index_bg[MRVDRV_MAX_CFP_CODE_BG] = { };

/** The table to keep CFP code for A */
t_u16 cfp_code_index_a[MRVDRV_MAX_CFP_CODE_A] = { 0x1, 0x2, 0x3, 0x4, 0x5 };

/**
 * The rates supported for ad-hoc B mode
 */
t_u8 AdhocRates_B[B_SUPPORTED_RATES] = { 0x82, 0x84, 0x8b, 0x96, 0 };

/**
 * The rates supported for ad-hoc G mode
 */
t_u8 AdhocRates_G[G_SUPPORTED_RATES] = { 0x8c, 0x12, 0x98, 0x24, 0xb0,
	0x48, 0x60, 0x6c, 0x00
};

/**
 * The rates supported for ad-hoc BG mode
 */
t_u8 AdhocRates_BG[BG_SUPPORTED_RATES] = { 0x82, 0x84, 0x8b, 0x96, 0x0c,
	0x12, 0x18, 0x24, 0x30, 0x48,
	0x60, 0x6c, 0x00
};

/**
 * The rates supported in A mode for ad-hoc
 */
t_u8 AdhocRates_A[A_SUPPORTED_RATES] = { 0x8c, 0x12, 0x98, 0x24, 0xb0,
	0x48, 0x60, 0x6c, 0x00
};

/**
 * The rates supported in A mode (used for BAND_A)
 */
t_u8 SupportedRates_A[A_SUPPORTED_RATES] = { 0x0c, 0x12, 0x18, 0x24, 0xb0,
	0x48, 0x60, 0x6c, 0x00
};

/**
 * The rates supported by the card
 */
static t_u16 WlanDataRates[WLAN_SUPPORTED_RATES_EXT] = {
	0x02, 0x04, 0x0B, 0x16, 0x00, 0x0C, 0x12, 0x18, 0x24, 0x30, 0x48,
	0x60, 0x6C, 0x90, 0x0D, 0x1A, 0x27, 0x34, 0x4E, 0x68, 0x75, 0x82,
	0x0C, 0x1B, 0x36, 0x51, 0x6C, 0xA2, 0xD8, 0xF3, 0x10E, 0x00
};

/**
 * The rates supported in B mode
 */
t_u8 SupportedRates_B[B_SUPPORTED_RATES] = { 0x02, 0x04, 0x0b, 0x16, 0x00 };

/**
 * The rates supported in G mode (BAND_G, BAND_G|BAND_GN)
 */
t_u8 SupportedRates_G[G_SUPPORTED_RATES] = { 0x0c, 0x12, 0x18, 0x24, 0x30,
	0x48, 0x60, 0x6c, 0x00
};

/**
 * The rates supported in BG mode (BAND_B|BAND_G, BAND_B|BAND_G|BAND_GN)
 */
t_u8 SupportedRates_BG[BG_SUPPORTED_RATES] = { 0x02, 0x04, 0x0b, 0x0c, 0x12,
	0x16, 0x18, 0x24, 0x30, 0x48,
	0x60, 0x6c, 0x00
};

/**
 * The rates supported in N mode
 */
t_u8 SupportedRates_N[N_SUPPORTED_RATES] = { 0x02, 0x04, 0 };

#define MCS_NUM_AX 12
// for MCS0/MCS1/MCS3/MCS4 have 4 additional DCM=1 value
// note: the value in the table is 2 multiplier of the actual rate
static t_u16 ax_mcs_rate_nss1[12][MCS_NUM_AX + 4] = {
	{0x90, 0x48, 0x120, 0x90, 0x1B0, 0x240, 0x120, 0x360, 0x1B0, 0x481,
	 0x511, 0x5A1, 0x6C1, 0x781, 0x871, 0x962},	/*SG 160M */
	{0x88, 0x44, 0x110, 0x88, 0x198, 0x220, 0x110, 0x330, 0x198, 0x440,
	 0x4C9, 0x551, 0x661, 0x716, 0x7F9, 0x8DC},	/*MG 160M */
	{0x7A, 0x3D, 0xF5, 0x7A, 0x16F, 0x1EA, 0xF5, 0x2DF, 0x16F, 0x3D4, 0x44E,
	 0x4C9, 0x5BE, 0x661, 0x72D, 0x7F9},	/*LG 160M */
	{0x48, 0x24, 0x90, 0x48, 0xD8, 0x120, 0x90, 0x1B0, 0xD8, 0x240, 0x288,
	 0x2D0, 0x360, 0x3C0, 0x438, 0x4B0},	/*SG 80M */
	{0x44, 0x22, 0x88, 0x44, 0xCC, 0x110, 0x88, 0x198, 0xCC, 0x220, 0x264,
	 0x2A8, 0x330, 0x38B, 0x3FC, 0x46E},	/*MG 80M */
	{0x3D, 0x1E, 0x7A, 0x3D, 0xB7, 0xF5, 0x7A, 0x16F, 0xB7, 0x1EA, 0x227,
	 0x264, 0x2DF, 0x330, 0x396, 0x3FC},	/*LG 80M */
	{0x22, 0x11, 0x44, 0x22, 0x67, 0x89, 0x44, 0xCE, 0x67, 0x113, 0x135,
	 0x158, 0x19D, 0x1CA, 0x204, 0x23D},	/*SG 40M */
	{0x20, 0x10, 0x41, 0x20, 0x61, 0x82, 0x41, 0xC3, 0x61, 0x104, 0x124,
	 0x145, 0x186, 0x1B1, 0x1E7, 0x21D},	/*MG 40M */
	{0x1D, 0xE, 0x3A, 0x1D, 0x57, 0x75, 0x3A, 0xAF, 0x57, 0xEA, 0x107,
	 0x124, 0x15F, 0x186, 0x1B6, 0x1E7},	/*LG 40M */
	{0x11, 0x8, 0x22, 0x11, 0x33, 0x44, 0x22, 0x67, 0x33, 0x89, 0x9A, 0xAC,
	 0xCE, 0xE5, 0x102, 0x11E},	/*SG 20M */
	{0x10, 0x8, 0x20, 0x10, 0x30, 0x41, 0x20, 0x61, 0x30, 0x82, 0x92, 0xA2,
	 0xC3, 0xD8, 0xF3, 0x10E},	/*MG 20M */
	{0xE, 0x7, 0x1D, 0xE, 0x2B, 0x3A, 0x1D, 0x57, 0x2B, 0x75, 0x83, 0x92,
	 0xAF, 0xC3, 0xDB, 0xF3}	/*LG 20M */
};

#if 0
// note: the value in the table is 2 multiplier of the actual rate
t_u16 ax_tone_ru_rate_nss1[9][MCS_NUM_AX + 4] = {
	{0x8, 0x4, 0xF, 0x8, 0x17, 0x1E, 0xF, 0x2D, 0x17, 0x3C, 0x44, 0x4B,
	 0x5A, 0x64, 0x71, 0x7D},	/*SG 106-tone */
	{0x7, 0x4, 0xF, 0x7, 0x16, 0x1D, 0xF, 0x2B, 0x16, 0x39, 0x40, 0x47,
	 0x55, 0x5F, 0x6B, 0x76},	/*MG 106-tone */
	{0x7, 0x3, 0xD, 0x6, 0x14, 0x1A, 0xD, 0x27, 0x14, 0x33, 0x3A, 0x40,
	 0x4D, 0x55, 0x60, 0x6B},	/*LG 106-tone */
	{0x4, 0x2, 0x7, 0x4, 0xB, 0xF, 0x7, 0x16, 0xB, 0x1D, 0x20, 0x22, 0x2B,
	 0x2F, 0x35, 0x3B},	/*SG 52-tone */
	{0x4, 0x2, 0x7, 0x4, 0xA, 0xE, 0x7, 0x14, 0xA, 0x1B, 0x1E, 0x22, 0x28,
	 0x2D, 0x32, 0x38},	/*MG 52-tone */
	{0x3, 0x2, 0x6, 0x3, 0x9, 0xC, 0x6, 0x12, 0x9, 0x18, 0x1B, 0x1E, 0x24,
	 0x28, 0x2D, 0x32},	/*LG 52-tone */
	{0x2, 0x1, 0x4, 0x2, 0x6, 0x7, 0x4, 0xB, 0x5, 0xE, 0x10, 0x12, 0x15,
	 0x18, 0x1A, 0x1D},	/*SG 26-tone */
	{0x2, 0x1, 0x4, 0x2, 0x5, 0x6, 0x4, 0xA, 0x5, 0xD, 0xF, 0x11, 0x14,
	 0x16, 0x19, 0x1C},	/*MG 26-tone */
	{0x2, 0x1, 0x3, 0x2, 0x5, 0x6, 0x3, 0x9, 0x4, 0xC, 0xE, 0xF, 0x12, 0x14,
	 0x17, 0x19}		/*LG 26-tone */
};
#endif

// note: the value in the table is 2 multiplier of the actual rate
static t_u16 ax_mcs_rate_nss2[12][MCS_NUM_AX + 4] = {
	{0x120, 0x90, 0x240, 0x120, 0x360, 0x481, 0x240, 0x61C, 0x360, 0x901,
	 0xA22, 0xB42, 0xD82, 0xF03, 0x10E3, 0x12C3},	/*SG 160M */
	{0x110, 0x88, 0x220, 0x110, 0x330, 0x440, 0x220, 0x661, 0x330, 0x881,
	 0x992, 0xAA2, 0xCAC, 0xE2D, 0xFF3, 0x11B9},	/*MG 160M */
	{0xF5, 0x7A, 0x1EA, 0xF5, 0x2DF, 0x3D4, 0x1EA, 0x5BE, 0x2DF, 0x7A8,
	 0x1134, 0x992, 0xB7C, 0xCC2, 0xE5B, 0xFF3},	/*LG 160M */
	{0x90, 0x48, 0x120, 0x90, 0x1B0, 0x240, 0x120, 0x360, 0x1B0, 0x481,
	 0x511, 0x5A1, 0x6C1, 0x781, 0x871, 0x962},	/*SG 80M */
	{0x88, 0x44, 0x110, 0x88, 0x198, 0x220, 0x110, 0x330, 0x198, 0x440,
	 0x4C9, 0x551, 0x661, 0x716, 0x7F9, 0x8DC},	/*MG 80M */
	{0x7A, 0x3D, 0xF5, 0x7A, 0x16F, 0x1EA, 0xF5, 0x2DF, 0x16F, 0x3D4, 0x44E,
	 0x4C9, 0x5BE, 0x661, 0x72D, 0x7F9},	/*LG 80M */
	{0x44, 0x22, 0x89, 0x44, 0xCE, 0x113, 0x89, 0x19D, 0xCE, 0x226, 0x26B,
	 0x2B0, 0x339, 0x395, 0x408, 0x47B},	/*SG 40M */
	{0x41, 0x20, 0x82, 0x41, 0xC3, 0x104, 0x82, 0x186, 0xC3, 0x208, 0x249,
	 0x28A, 0x30C, 0x362, 0x3CE, 0x43B},	/*MG 40M */
	{0x3A, 0x1D, 0x75, 0x3A, 0xAF, 0xEA, 0x75, 0x15F, 0xAF, 0x1D4, 0x20E,
	 0x249, 0x2BE, 0x30C, 0x36D, 0x3CF},	/*LG 40M */
	{0x22, 0x11, 0x44, 0x22, 0x67, 0x89, 0x44, 0xCE, 0x67, 0x113, 0x135,
	 0x158, 0x19D, 0x1CA, 0x204, 0x23D},	/*SG 20M */
	{0x20, 0x10, 0x41, 0x20, 0x61, 0x82, 0x41, 0xC3, 0x61, 0x104, 0x124,
	 0x145, 0x186, 0x1B1, 0x1E7, 0x21D},	/*MG 20M */
	{0x1D, 0xE, 0x3A, 0x1D, 0x57, 0x75, 0x3A, 0xAF, 0x57, 0xEA, 0x107,
	 0x124, 0x15F, 0x186, 0x1B6, 0x1E7}	/*LG 20M */
};

#if 0
// note: the value in the table is 2 multiplier of the actual rate
t_u16 ax_tone_ru_rate_nss2[9][MCS_NUM_AX + 4] = {
	{0xF, 0x8, 0x1E, 0xF, 0x2D, 0x3C, 0x1E, 0x5A, 0x2D, 0x78, 0x87, 0x96,
	 0xB4, 0xC8, 0xE1, 0xFA},	/*SG 106-tone */
	{0xE, 0x7, 0x1D, 0xE, 0x2B, 0x39, 0x1D, 0x55, 0x2B, 0x72, 0x80, 0x8E,
	 0xAA, 0xBD, 0xD5, 0xED},	/*MG 106-tone */
	{0xD, 0x7, 0x1A, 0xD, 0x27, 0x33, 0x1A, 0x4D, 0x27, 0x66, 0x73, 0x80,
	 0x99, 0xAA, 0xC0, 0xD5},	/*LG 106-tone */
	{0x7, 0x4, 0xF, 0x7, 0x16, 0x1D, 0xF, 0x2A, 0x16, 0x39, 0x40, 0x47,
	 0x55, 0x5F, 0x6A, 0x76},	/*SG 52-tone */
	{0x7, 0x4, 0xE, 0x7, 0x14, 0x1B, 0xE, 0x28, 0x14, 0x36, 0x3C, 0x43,
	 0x50, 0x59, 0x64, 0x70},	/*MG 52-tone */
	{0x6, 0x3, 0xC, 0x6, 0x12, 0x18, 0xC, 0x24, 0x12, 0x30, 0x36, 0x3C,
	 0x48, 0x50, 0x5A, 0x64},	/*LG 52-tone */
	{0x4, 0x2, 0x7, 0x4, 0xB, 0xF, 0x7, 0x16, 0xB, 0x1D, 0x20, 0x22, 0x2B,
	 0x2F, 0x35, 0x3B},	/*SG 26-tone */
	{0x4, 0x2, 0x7, 0x4, 0xA, 0xE, 0x7, 0x14, 0xA, 0x1B, 0x1E, 0x22, 0x28,
	 0x2D, 0x32, 0x38},	/*MG 26-tone */
	{0x3, 0x2, 0x6, 0x3, 0x9, 0xC, 0x6, 0x12, 0x9, 0x18, 0x1B, 0x1E, 0x24,
	 0x28, 0x2D, 0x32}	/*LG 26-tone */
};
#endif

/********************************************************
 *			Local Functions
 ********************************************************/
/**
 *  @brief Find a character in a string.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param s            A pointer to string
 *  @param c            Character to be located
 *  @param n            The length of string
 *
 *  @return        A pointer to the first occurrence of c in string, or MNULL if
 * c is not found.
 */
static void *
wlan_memchr(pmlan_adapter pmadapter, void *s, int c, int n)
{
	const t_u8 *p = (t_u8 *)s;

	ENTER();

	while (n--) {
		if ((t_u8)c == *p++) {
			LEAVE();
			return (void *)(p - 1);
		}
	}

	LEAVE();
	return MNULL;
}

/**
 *  @brief This function finds the CFP in
 *          cfp_table_BG/A based on region/code and band parameter.
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @param region     The region code
 *  @param band       The band
 *  @param cfp_no     A pointer to CFP number
 *
 *  @return           A pointer to CFP
 */
static chan_freq_power_t *
wlan_get_region_cfp_table(pmlan_adapter pmadapter,
			  t_u8 region, t_u8 band, int *cfp_no)
{
	t_u32 i;
	t_u8 cfp_bg, cfp_a;

	ENTER();

	cfp_bg = cfp_a = region;
	if (!region) {
		/* Invalid region code, use CFP code */
		cfp_bg = pmadapter->cfp_code_bg;
		cfp_a = pmadapter->cfp_code_a;
	}

	if (band & (BAND_B | BAND_G | BAND_GN | BAND_GAC)) {
		/* Return the FW cfp table for requested region code, if
		 * available. If region is not forced and the requested region
		 * code is different, simply return the corresponding
		 * pre-defined table.
		 */
		if (pmadapter->otp_region && pmadapter->cfp_otp_bg) {
			if (pmadapter->otp_region->force_reg ||
			    (cfp_bg ==
			     (t_u8)pmadapter->otp_region->region_code)) {
				*cfp_no = pmadapter->tx_power_table_bg_rows;
				LEAVE();
				return pmadapter->cfp_otp_bg;
			}
		}
		for (i = 0; i < MLAN_CFP_TABLE_SIZE_BG; i++) {
			PRINTM(MINFO, "cfp_table_BG[%d].code=%d\n", i,
			       cfp_table_BG[i].code);
			/* Check if region/code matches for BG bands */
			if (cfp_table_BG[i].code == cfp_bg) {
				/* Select by band */
				*cfp_no = cfp_table_BG[i].cfp_no;
				LEAVE();
				return cfp_table_BG[i].cfp;
			}
		}
	}
	if (band & (BAND_A | BAND_AN | BAND_AAC)) {
		/* Return the FW cfp table for requested region code */
		if (pmadapter->otp_region && pmadapter->cfp_otp_a) {
			if (pmadapter->otp_region->force_reg ||
			    (cfp_a ==
			     (t_u8)pmadapter->otp_region->region_code)) {
				*cfp_no = pmadapter->tx_power_table_a_rows;
				LEAVE();
				return pmadapter->cfp_otp_a;
			}
		}
		for (i = 0; i < MLAN_CFP_TABLE_SIZE_A; i++) {
			PRINTM(MINFO, "cfp_table_A[%d].code=%d\n", i,
			       cfp_table_A[i].code);
			/* Check if region/code matches for A bands */
			if (cfp_table_A[i].code == cfp_a) {
				/* Select by band */
				*cfp_no = cfp_table_A[i].cfp_no;
				LEAVE();
				return cfp_table_A[i].cfp;
			}
		}
	}

	if (!region)
		PRINTM(MERROR, "Error Band[0x%x] or code[BG:%#x, A:%#x]\n",
		       band, cfp_bg, cfp_a);
	else
		PRINTM(MERROR, "Error Band[0x%x] or region[%#x]\n", band,
		       region);

	LEAVE();
	return MNULL;
}

/**
 *  @brief This function copies dynamic CFP elements from one table to another.
 *         Only copy elements where channel numbers match.
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param cfp         Destination table
 *  @param num_cfp     Number of elements in dest table
 *  @param cfp_src     Source table
 *  @param num_cfp_src Number of elements in source table
 */
static t_void
wlan_cfp_copy_dynamic(pmlan_adapter pmadapter,
		      chan_freq_power_t *cfp, t_u8 num_cfp,
		      chan_freq_power_t *cfp_src, t_u8 num_cfp_src)
{
	int i, j;

	ENTER();

	if (cfp == cfp_src) {
		LEAVE();
		return;
	}

	/* first clear dest dynamic blacklisted entries */
	/* do not clear the flags */
	for (i = 0; i < num_cfp; i++) {
		cfp[i].dynamic.blacklist = MFALSE;
	}

	/* copy dynamic blacklisted entries from source where channels match */
	if (cfp_src) {
		for (i = 0; i < num_cfp; i++)
			for (j = 0; j < num_cfp_src; j++)
				if (cfp[i].channel == cfp_src[j].channel) {
					cfp[i].dynamic.blacklist =
						cfp_src[j].dynamic.blacklist;
					break;
				}
	}

	LEAVE();
}

/********************************************************
 *			Global Functions
 ********************************************************/
/**
 *  @brief This function converts region string to integer code
 *
 *  @param pmadapter        A pointer to mlan_adapter structure
 *  @param country_code     Country string
 *  @param cfp_bg           Pointer to buffer
 *  @param cfp_a            Pointer to buffer
 *
 *  @return                 MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_country_2_cfp_table_code(pmlan_adapter pmadapter,
				   t_u8 *country_code, t_u8 *cfp_bg,
				   t_u8 *cfp_a)
{
	t_u8 i;

	ENTER();

	if (pmadapter->otp_region) {
		if (!memcmp(pmadapter, pmadapter->otp_region->country_code,
			    country_code, COUNTRY_CODE_LEN - 1)) {
			if (pmadapter->cfp_otp_bg)
				*cfp_bg = pmadapter->otp_region->region_code;
			if (pmadapter->cfp_otp_a)
				*cfp_a = pmadapter->otp_region->region_code;
			LEAVE();
			return MLAN_STATUS_SUCCESS;
		}
	}
	/* Look for code in mapping table */
	for (i = 0; i < NELEMENTS(country_code_mapping); i++) {
		if (!memcmp(pmadapter, country_code_mapping[i].country_code,
			    country_code, COUNTRY_CODE_LEN - 1)) {
			*cfp_bg = country_code_mapping[i].cfp_code_bg;
			*cfp_a = country_code_mapping[i].cfp_code_a;
			LEAVE();
			return MLAN_STATUS_SUCCESS;
		}
	}

	/* If still not found, look for code in EU country code table */
	for (i = 0; i < NELEMENTS(eu_country_code_table); i++) {
		if (!memcmp(pmadapter, eu_country_code_table[i], country_code,
			    COUNTRY_CODE_LEN - 1)) {
			*cfp_bg = EU_CFP_CODE_BG;
			*cfp_a = EU_CFP_CODE_A;
			LEAVE();
			return MLAN_STATUS_SUCCESS;
		}
	}

	LEAVE();
	return MLAN_STATUS_FAILURE;
}

/**
 *  @brief This function finds if given country code is in EU table
 *
 *  @param pmadapter        A pointer to mlan_adapter structure
 *  @param country_code     Country string
 *
 *  @return                 MTRUE or MFALSE
 */
t_bool
wlan_is_etsi_country(pmlan_adapter pmadapter, t_u8 *country_code)
{
	t_u8 i;

	ENTER();

	/* Look for code in EU country code table */
	for (i = 0; i < NELEMENTS(eu_country_code_table); i++) {
		if (!memcmp(pmadapter, eu_country_code_table[i], country_code,
			    COUNTRY_CODE_LEN - 1)) {
			LEAVE();
			return MTRUE;
		}
	}

	LEAVE();
	return MFALSE;
}

/**
 *   @brief This function adjust the antenna index
 *
 *   V16_FW_API: Bit0: ant A, Bit 1:ant B, Bit0 & Bit 1: A+B
 *   8887: case1: 0 - 2.4G ant A,  1- 2.4G antB, 2-- 5G ant C
 *   case2: 0 - 2.4G ant A,  1- 2.4G antB, 0x80- 5G antA, 0x81-5G ant B
 *   @param priv	A pointer to mlan_private structure
 *   @param prx_pd	A pointer to the RxPD structure
 *
 *   @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
t_u8
wlan_adjust_antenna(pmlan_private priv, RxPD *prx_pd)
{
	t_u8 antenna = prx_pd->antenna;
#if defined(SD8887) || defined(SD8987)
	t_u32 rx_channel = (prx_pd->rx_info & RXPD_CHAN_MASK) >> 5;
#endif
	if (prx_pd->antenna == 0xff)
		return 0;
	if (priv->adapter->pcard_info->v16_fw_api) {
		if ((antenna & MBIT(0)) && (antenna & MBIT(1)))
			antenna = 2;
		else if (antenna & MBIT(1))
			antenna = 1;
		else if (antenna & MBIT(0))
			antenna = 0;
	}
#if defined(SD8887) || defined(SD8987)
#define ANTENNA_OFFSET 2
	if (MFALSE
#ifdef SD8887
	    || IS_SD8887(priv->adapter->card_type)
#endif
#ifdef SD8987
	    || IS_SD8987(priv->adapter->card_type)
#endif
		) {
		if ((priv->adapter->antinfo & ANT_DIVERSITY_2G) &&
		    (priv->adapter->antinfo & ANT_DIVERSITY_5G)) {
#define MAX_2G_CHAN 14
			if (rx_channel > MAX_2G_CHAN)
				antenna += ANTENNA_OFFSET;
		}
	}
#endif

	return antenna;
}

/**
 *  @brief This function adjust the rate index
 *
 *  @param priv    A pointer to mlan_private structure
 *  @param rx_rate rx rate
 *  @param rate_info rate info
 *  @return        rate index
 */
t_u16
wlan_adjust_data_rate(mlan_private *priv, t_u8 rx_rate, t_u8 rate_info)
{
	t_u16 rate_index = 0;
	t_u8 bw = 0;
	t_u8 nss = 0;
	t_bool sgi_enable = 0;
	t_u8 gi = 0;
#define MAX_MCS_NUM_AX 12

#define MAX_MCS_NUM_SUPP 16
#define MAX_MCS_NUM_AC 10
#define RATE_INDEX_MCS0 12
	bw = (rate_info & 0xC) >> 2;
	sgi_enable = (rate_info & 0x10) >> 4;
	if ((rate_info & 0x3) == 0) {
		rate_index = (rx_rate > MLAN_RATE_INDEX_OFDM0) ? rx_rate - 1 :
			rx_rate;
	} else if ((rate_info & 0x03) == 1) {
		rate_index = RATE_INDEX_MCS0 +
			MAX_MCS_NUM_SUPP * 2 * sgi_enable +
			MAX_MCS_NUM_SUPP * bw + rx_rate;
	} else if ((rate_info & 0x3) == 2) {
		if (IS_STREAM_2X2(priv->adapter->feature_control))
			nss = rx_rate >> 4;	// 0:NSS1, 1:NSS2
		rate_index = RATE_INDEX_MCS0 + MAX_MCS_NUM_SUPP * 4 +
			MAX_MCS_NUM_AC * 6 * sgi_enable +
			MAX_MCS_NUM_AC * 2 * bw + MAX_MCS_NUM_AC * nss +
			(rx_rate & 0x0f);
	} else if ((rate_info & 0x3) == 3) {
		gi = (rate_info & 0x10) >> 4 | (rate_info & 0x80) >> 6;
		if (IS_STREAM_2X2(priv->adapter->feature_control))
			nss = rx_rate >> 4;	// 0:NSS1, 1:NSS2
		rate_index = RATE_INDEX_MCS0 + MAX_MCS_NUM_SUPP * 4 +
			MAX_MCS_NUM_AC * 12 + MAX_MCS_NUM_AX * 6 * gi +
			MAX_MCS_NUM_AX * 2 * bw + MAX_MCS_NUM_AX * nss +
			(rx_rate & 0x0f);
	}
	return rate_index;
}

#ifdef STA_SUPPORT
#endif /* STA_SUPPORT */

/**
 *  @brief convert TX rate_info from v14 to v15+ FW rate_info
 *
 *  @param v14_rate_info      v14 rate info
 *
 *  @return             v15+ rate info
 */
t_u8
wlan_convert_v14_tx_rate_info(pmlan_private pmpriv, t_u8 v14_rate_info)
{
	t_u8 rate_info = 0;

	if (!pmpriv->adapter->pcard_info->v14_fw_api) {
		PRINTM(MERROR, "%s: Not convert for this is not V14 FW\n",
		       __func__);
		return v14_rate_info;
	}

	rate_info = v14_rate_info & 0x01;
	/* band */
	rate_info |= (v14_rate_info & MBIT(1)) << 1;
	/* short GI */
	rate_info |= (v14_rate_info & MBIT(2)) << 2;
	return rate_info;
}

/**
 *  @brief convert RX rate_info from v14 to v15+ FW rate_info
 *
 *  @param v14_rate_info      v14 rate info
 *
 *  @return             v15+ rate info
 */
t_u8
wlan_convert_v14_rx_rate_info(pmlan_private pmpriv, t_u8 v14_rate_info)
{
	t_u8 rate_info = 0;
	t_u8 mode = 0;
	t_u8 bw = 0;
	t_u8 sgi = 0;

	if (!pmpriv->adapter->pcard_info->v14_fw_api) {
		PRINTM(MERROR, "%s: Not convert for this is not V14 FW\n",
		       __func__);
		return v14_rate_info;
	}

	mode = v14_rate_info & MBIT(0);
	bw = v14_rate_info & MBIT(1);
	sgi = (v14_rate_info & 0x04) >> 2;

	rate_info = (mode & 0x01) | ((bw & 0x01) << 2) | ((sgi & 0x01) << 4);

	return rate_info;
}

/**
 *  @brief Use index to get the data rate
 *
 *  @param pmadapter        A pointer to mlan_adapter structure
 *  @param index            The index of data rate
 *  @param tx_rate_info     Tx rate info
 *  @param ext_rate_info    Extend tx rate info
 *
 *  @return                 Data rate or 0
 */
t_u32
wlan_index_to_data_rate(pmlan_adapter pmadapter, t_u8 index,
			t_u8 tx_rate_info, t_u8 ext_rate_info)
{
#define MCS_NUM_SUPP 16
	t_u16 mcs_rate[4][MCS_NUM_SUPP] = {
		{0x1b, 0x36, 0x51, 0x6c, 0xa2, 0xd8, 0xf3, 0x10e, 0x36, 0x6c,
		 0xa2, 0xd8, 0x144, 0x1b0, 0x1e6, 0x21c},	/*LG 40M */
		{0x1e, 0x3c, 0x5a, 0x78, 0xb4, 0xf0, 0x10e, 0x12c, 0x3c, 0x78,
		 0xb4, 0xf0, 0x168, 0x1e0, 0x21c, 0x258},	/*SG 40M */
		{0x0d, 0x1a, 0x27, 0x34, 0x4e, 0x68, 0x75, 0x82, 0x1a, 0x34,
		 0x4e, 0x68, 0x9c, 0xd0, 0xea, 0x104},	/*LG 20M */
		{0x0e, 0x1c, 0x2b, 0x39, 0x56, 0x73, 0x82, 0x90, 0x1c, 0x39,
		 0x56, 0x73, 0xad, 0xe7, 0x104, 0x120}
	};			/*SG 20M */

#define MCS_NUM_AC 10
	/* NSS 1. note: the value in the table is 2 multiplier of the actual
	 * rate in other words, it is in the unit of 500 Kbs
	 */
	t_u16 ac_mcs_rate_nss1[8][MCS_NUM_AC] = {
		{0x75, 0xEA, 0x15F, 0x1D4, 0x2BE, 0x3A8, 0x41D, 0x492, 0x57C,
		 0x618},	/* LG 160M */
		{0x82, 0x104, 0x186, 0x208, 0x30C, 0x410, 0x492, 0x514, 0x618,
		 0x6C6},	/* SG 160M */
		{0x3B, 0x75, 0xB0, 0xEA, 0x15F, 0x1D4, 0x20F, 0x249, 0x2BE,
		 0x30C},	/* LG 80M */
		{0x41, 0x82, 0xC3, 0x104, 0x186, 0x208, 0x249, 0x28A, 0x30C,
		 0x363},	/* SG 80M */
		{0x1B, 0x36, 0x51, 0x6C, 0xA2, 0xD8, 0xF3, 0x10E, 0x144,
		 0x168},	/* LG 40M */
		{0x1E, 0x3C, 0x5A, 0x78, 0xB4, 0xF0, 0x10E, 0x12C, 0x168,
		 0x190},	/* SG 40M */
		{0xD, 0x1A, 0x27, 0x34, 0x4E, 0x68, 0x75, 0x82, 0x9C,
		 0x00},		/* LG 20M */
		{0xF, 0x1D, 0x2C, 0x3A, 0x57, 0x74, 0x82, 0x91, 0xAE,
		 0x00},		/* SG 20M */
	};
	/* NSS 2. note: the value in the table is 2 multiplier of the actual
	 * rate
	 */
	t_u16 ac_mcs_rate_nss2[8][MCS_NUM_AC] = {
		{0xEA, 0x1D4, 0x2BE, 0x3A8, 0x57C, 0x750, 0x83A, 0x924, 0xAF8,
		 0xC30},	/*LG 160M */
		{0x104, 0x208, 0x30C, 0x410, 0x618, 0x820, 0x924, 0xA28, 0xC30,
		 0xD8B},	/*SG 160M */

		{0x75, 0xEA, 0x15F, 0x1D4, 0x2BE, 0x3A8, 0x41D, 0x492, 0x57C,
		 0x618},	/*LG 80M */
		{0x82, 0x104, 0x186, 0x208, 0x30C, 0x410, 0x492, 0x514, 0x618,
		 0x6C6},	/*SG 80M */
		{0x36, 0x6C, 0xA2, 0xD8, 0x144, 0x1B0, 0x1E6, 0x21C, 0x288,
		 0x2D0},	/*LG 40M */
		{0x3C, 0x78, 0xB4, 0xF0, 0x168, 0x1E0, 0x21C, 0x258, 0x2D0,
		 0x320},	/*SG 40M */
		{0x1A, 0x34, 0x4A, 0x68, 0x9C, 0xD0, 0xEA, 0x104, 0x138,
		 0x00},		/*LG 20M */
		{0x1D, 0x3A, 0x57, 0x74, 0xAE, 0xE6, 0x104, 0x121, 0x15B,
		 0x00},		/*SG 20M */
	};

	t_u32 rate = 0;
	t_u8 mcs_index = 0;
	t_u8 he_dcm = 0;
//      t_u8 he_tone = 0;
	t_u8 stbc = 0;

	t_u8 bw = 0;
	t_u8 gi = 0;

	ENTER();

	PRINTM(MINFO, "%s:index=%d, tx_rate_info=%d, ext_rate_info=%d\n",
	       __func__, index, tx_rate_info, ext_rate_info);

	if ((tx_rate_info & 0x3) == MLAN_RATE_FORMAT_VHT) {
		/* VHT rate */
		mcs_index = index & 0xF;

		if (mcs_index > 9)
			mcs_index = 9;

		/* 20M: bw=0, 40M: bw=1, 80M: bw=2, 160M: bw=3 */
		bw = (tx_rate_info & 0xC) >> 2;
		/* LGI: gi =0, SGI: gi = 1 */
		gi = (tx_rate_info & 0x10) >> 4;
		if ((index >> 4) == 1) {
			/* NSS = 2 */
			rate = ac_mcs_rate_nss2[2 * (3 - bw) + gi][mcs_index];
		} else
			/* NSS = 1 */
			rate = ac_mcs_rate_nss1[2 * (3 - bw) + gi][mcs_index];
	} else
	 if ((tx_rate_info & 0x3) == MLAN_RATE_FORMAT_HE) {
		/* VHT rate */
		mcs_index = index & 0xF;
		he_dcm = ext_rate_info & MBIT(0);

		if (mcs_index > MCS_NUM_AX - 1)
			mcs_index = MCS_NUM_AX - 1;

		/* 20M: bw=0, 40M: bw=1, 80M: bw=2, 160M: bw=3 */
		bw = (tx_rate_info & (MBIT(3) | MBIT(2))) >> 2;
		/* BIT7:BIT4 0:0= 0.8us,0:1= 0.8us, 1:0=1.6us, 1:1=3.2us or
		 * 0.8us
		 */
		gi = (tx_rate_info & MBIT(4)) >> 4 |
			(tx_rate_info & MBIT(7)) >> 6;
		/* STBC: BIT5 in tx rate info */
		stbc = (tx_rate_info & MBIT(5)) >> 5;

		if (gi > 3) {
			PRINTM(MERROR, "Invalid gi value");
			return 0;
		}

		if ((gi == 3) && stbc && he_dcm) {
			gi = 0;
			stbc = 0;
			he_dcm = 0;
		}
		/* map to gi 0:0.8us,1:1.6us 2:3.2us */
		if (gi > 0)
			gi = gi - 1;

//#ifdef ENABLE_802_11AX
		// TODO: hardcode he_tone here, wait for FW value ready.
//              he_tone = 4;

		//he_tone = (ext_rate_info & 0xE) >> 1;
//#endif

		if ((index >> 4) == 1) {
			switch (mcs_index) {
			case 0:
			case 1:
// #if 0
// if (he_tone < 3) {
//      rate = ax_tone_ru_rate_nss2[3*(2-he_tone)+gi][mcs_index*2 + he_dcm];
// } else {
// #endif
				rate = ax_mcs_rate_nss2[3 * (3 - bw) + gi]
					[mcs_index * 2 + he_dcm];
				break;
			case 2:
// #if 0
// if (he_tone < 3) {
//      rate = ax_tone_ru_rate_nss2[3*(2-he_tone)+gi][mcs_index*2];
// } else {
// #endif
				rate = ax_mcs_rate_nss2[3 * (3 - bw) + gi]
					[mcs_index * 2];
				break;
			case 3:
			case 4:
// #if 0
// if (he_tone < 3) {
//      rate = ax_tone_ru_rate_nss2[3*(2-he_tone)+gi][mcs_index*2 - 1 + he_dcm];
// } else {
// #endif
				rate = ax_mcs_rate_nss2[3 * (3 - bw) + gi]
					[mcs_index * 2 - 1 + he_dcm];
				break;

			default:
// #if 0
// if (he_tone < 3) {
//      rate = ax_tone_ru_rate_nss2[3*(2-he_tone)+gi][mcs_index + 4];
// } else {
// #endif
				rate = ax_mcs_rate_nss2[3 * (3 - bw) + gi]
					[mcs_index + 4];
				break;
			}
		} else {
			switch (mcs_index) {
			case 0:
			case 1:
// #if 0
// if (he_tone < 3) {
//      rate = ax_tone_ru_rate_nss1[3*(2-he_tone)+gi][mcs_index*2 + he_dcm];
// } else {
// #endif
				rate = ax_mcs_rate_nss1[3 * (3 - bw) + gi]
					[mcs_index * 2 + he_dcm];
				break;
			case 2:
// #if 0
// if (he_tone < 3) {
//      rate = ax_tone_ru_rate_nss1[3*(2-he_tone)+gi][mcs_index*2];
// } else {
// #endif
				rate = ax_mcs_rate_nss1[3 * (3 - bw) + gi]
					[mcs_index * 2];
				break;
			case 3:
			case 4:
// #if 0
// if (he_tone < 3) {
//      rate = ax_tone_ru_rate_nss1[3*(2-he_tone)+gi][mcs_index*2 - 1 + he_dcm];
// } else {
// #endif
				rate = ax_mcs_rate_nss1[3 * (3 - bw) + gi]
					[mcs_index * 2 - 1 + he_dcm];
				break;

			default:
// #if 0
// if (he_tone < 3) {
//      rate = ax_tone_ru_rate_nss1[3*(2-he_tone)+gi][mcs_index + 4];
// } else {
// #endif
				rate = ax_mcs_rate_nss1[3 * (3 - bw) + gi]
					[mcs_index + 4];
				break;
			}
		}
	} else if ((tx_rate_info & 0x3) == MLAN_RATE_FORMAT_HT) {
		/* HT rate */
		/* 20M: bw=0, 40M: bw=1 */
		bw = (tx_rate_info & 0xC) >> 2;
		/* LGI: gi =0, SGI: gi = 1 */
		gi = (tx_rate_info & 0x10) >> 4;
		if (index == MLAN_RATE_BITMAP_MCS0) {
			if (gi == 1)
				rate = 0x0D;	/* MCS 32 SGI rate */
			else
				rate = 0x0C;	/* MCS 32 LGI rate */
		} else if (index < MCS_NUM_SUPP) {
			if (bw <= 1)
				rate = mcs_rate[2 * (1 - bw) + gi][index];
			else
				rate = WlanDataRates[0];
		} else
			rate = WlanDataRates[0];
	} else {
		/* 11n non HT rates */
		if (index >= WLAN_SUPPORTED_RATES_EXT)
			index = 0;
		rate = WlanDataRates[index];
	}
	LEAVE();
	return rate;
}

/**
 *  @brief Use rate to get the index
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param rate         Data rate
 *
 *  @return                     Index or 0
 */
t_u8
wlan_data_rate_to_index(pmlan_adapter pmadapter, t_u32 rate)
{
	t_u16 *ptr;

	ENTER();
	if (rate) {
		ptr = wlan_memchr(pmadapter, WlanDataRates, (t_u8)rate,
				  sizeof(WlanDataRates));
		if (ptr) {
			LEAVE();
			return (t_u8)(ptr - WlanDataRates);
		}
	}
	LEAVE();
	return 0;
}

/**
 *  @brief Get active data rates
 *
 *  @param pmpriv           A pointer to mlan_private structure
 *  @param bss_mode         The specified BSS mode (Infra/IBSS)
 *  @param config_bands     The specified band configuration
 *  @param rates            The buf to return the active rates
 *
 *  @return                 The number of Rates
 */
t_u32
wlan_get_active_data_rates(mlan_private *pmpriv, t_u32 bss_mode,
			   t_u16 config_bands, WLAN_802_11_RATES rates)
{
	t_u32 k;

	ENTER();

	if (pmpriv->media_connected != MTRUE) {
		k = wlan_get_supported_rates(pmpriv, bss_mode, config_bands,
					     rates);
	} else {
		k = wlan_copy_rates(rates, 0,
				    pmpriv->curr_bss_params.data_rates,
				    pmpriv->curr_bss_params.num_of_rates);
	}

	LEAVE();
	return k;
}

#ifdef STA_SUPPORT
/**
 *  @brief This function search through all the regions cfp table to find the
 * channel, if the channel is found then gets the MIN txpower of the channel
 *            present in all the regions.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param channel      Channel number.
 *
 *  @return             The Tx power
 */
t_u8
wlan_get_txpwr_of_chan_from_cfp(mlan_private *pmpriv, t_u8 channel)
{
	t_u8 i = 0;
	t_u8 j = 0;
	t_u8 tx_power = 0;
	t_u32 cfp_no;
	chan_freq_power_t *cfp = MNULL;
	chan_freq_power_t *cfp_a = MNULL;
	t_u32 cfp_no_a;

	ENTER();

	for (i = 0; i < MLAN_CFP_TABLE_SIZE_BG; i++) {
		/* Get CFP */
		cfp = cfp_table_BG[i].cfp;
		cfp_no = cfp_table_BG[i].cfp_no;
		/* Find matching channel and get Tx power */
		for (j = 0; j < cfp_no; j++) {
			if ((cfp + j)->channel == channel) {
				if (tx_power != 0)
					tx_power = MIN(tx_power,
						       (cfp + j)->max_tx_power);
				else
					tx_power =
						(t_u8)(cfp + j)->max_tx_power;
				break;
			}
		}
	}

	for (i = 0; i < MLAN_CFP_TABLE_SIZE_A; i++) {
		/* Get CFP */
		cfp_a = cfp_table_A[i].cfp;
		cfp_no_a = cfp_table_A[i].cfp_no;
		for (j = 0; j < cfp_no_a; j++) {
			if ((cfp_a + j)->channel == channel) {
				if (tx_power != 0)
					tx_power =
						MIN(tx_power,
						    (cfp_a + j)->max_tx_power);
				else
					tx_power = (t_u8)((cfp_a +
							   j)->max_tx_power);
				break;
			}
		}
	}

	LEAVE();
	return tx_power;
}

/**
 *  @brief Get the channel frequency power info for a specific channel
 *
 *  @param pmadapter            A pointer to mlan_adapter structure
 *  @param band                 It can be BAND_A, BAND_G or BAND_B
 *  @param channel              The channel to search for
 *  @param region_channel       A pointer to region_chan_t structure
 *
 *  @return                     A pointer to chan_freq_power_t structure or
 * MNULL if not found.
 */

chan_freq_power_t *
wlan_get_cfp_by_band_and_channel(pmlan_adapter pmadapter, t_u8 band,
				 t_u16 channel, region_chan_t *region_channel)
{
	region_chan_t *rc;
	chan_freq_power_t *cfp = MNULL;
	int i, j;

	ENTER();

	for (j = 0; !cfp && (j < MAX_REGION_CHANNEL_NUM); j++) {
		rc = &region_channel[j];

		if (!rc->valid || !rc->pcfp)
			continue;
		switch (rc->band) {
		case BAND_A:
			switch (band) {
			case BAND_AN:
			case BAND_A | BAND_AN:
			case BAND_A | BAND_AN | BAND_AAC:
				/* Fall Through */
			case BAND_A:	/* Matching BAND_A */
				break;

			default:
				continue;
			}
			break;
		case BAND_B:
		case BAND_G:
			switch (band) {
			case BAND_GN:
			case BAND_B | BAND_G | BAND_GN:
			case BAND_G | BAND_GN:
			case BAND_GN | BAND_GAC:
			case BAND_B | BAND_G | BAND_GN | BAND_GAC:
			case BAND_G | BAND_GN | BAND_GAC:
			case BAND_B | BAND_G:
				/* Fall Through */
			case BAND_B:	/* Matching BAND_B/G */
				/* Fall Through */
			case BAND_G:
				/* Fall Through */
			case 0:
				break;
			default:
				continue;
			}
			break;
		default:
			continue;
		}
		if (channel == FIRST_VALID_CHANNEL)
			cfp = &rc->pcfp[0];
		else {
			for (i = 0; i < rc->num_cfp; i++) {
				if (rc->pcfp[i].channel == channel) {
					cfp = &rc->pcfp[i];
					break;
				}
			}
		}
	}

	if (!cfp && channel)
		PRINTM(MCMND,
		       "%s: can not find cfp by band %d & channel %d\n",
		       __func__, band, channel);

	LEAVE();
	return cfp;
}

/**
 *  @brief Find the channel frequency power info for a specific channel
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param band         It can be BAND_A, BAND_G or BAND_B
 *  @param channel      The channel to search for
 *
 *  @return             A pointer to chan_freq_power_t structure or MNULL if not
 * found.
 */
chan_freq_power_t *
wlan_find_cfp_by_band_and_channel(mlan_adapter *pmadapter,
				  t_u8 band, t_u16 channel)
{
	chan_freq_power_t *cfp = MNULL;

	ENTER();

	/* Any station(s) with 11D enabled */
	if (wlan_count_priv_cond(pmadapter, wlan_11d_is_enabled,
				 wlan_is_station) > 0)
		cfp = wlan_get_cfp_by_band_and_channel(pmadapter, band, channel,
						       pmadapter->
						       universal_channel);
	else
		cfp = wlan_get_cfp_by_band_and_channel(pmadapter, band, channel,
						       pmadapter->
						       region_channel);

	LEAVE();
	return cfp;
}

/**
 *  @brief Find the channel frequency power info for a specific frequency
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param band         It can be BAND_A, BAND_G or BAND_B
 *  @param freq         The frequency to search for
 *
 *  @return         Pointer to chan_freq_power_t structure; MNULL if not found
 */
chan_freq_power_t *
wlan_find_cfp_by_band_and_freq(mlan_adapter *pmadapter, t_u8 band, t_u32 freq)
{
	chan_freq_power_t *cfp = MNULL;
	region_chan_t *rc;
	int i, j;

	ENTER();

	for (j = 0; !cfp && (j < MAX_REGION_CHANNEL_NUM); j++) {
		rc = &pmadapter->region_channel[j];

		/* Any station(s) with 11D enabled */
		if (wlan_count_priv_cond(pmadapter, wlan_11d_is_enabled,
					 wlan_is_station) > 0)
			rc = &pmadapter->universal_channel[j];

		if (!rc->valid || !rc->pcfp)
			continue;
		switch (rc->band) {
		case BAND_A:
			switch (band) {
			case BAND_AN:
			case BAND_A | BAND_AN:
			case BAND_A | BAND_AN | BAND_AAC:
				/* Fall Through */
			case BAND_A:	/* Matching BAND_A */
				break;
			default:
				continue;
			}
			break;
		case BAND_B:
		case BAND_G:
			switch (band) {
			case BAND_GN:
			case BAND_B | BAND_G | BAND_GN:
			case BAND_G | BAND_GN:
			case BAND_GN | BAND_GAC:
			case BAND_B | BAND_G | BAND_GN | BAND_GAC:
			case BAND_G | BAND_GN | BAND_GAC:
			case BAND_B | BAND_G:
				/* Fall Through */
			case BAND_B:
				/* Fall Through */
			case BAND_G:
				/* Fall Through */
			case 0:
				break;
			default:
				continue;
			}
			break;
		default:
			continue;
		}
		for (i = 0; i < rc->num_cfp; i++) {
			if (rc->pcfp[i].freq == freq) {
				cfp = &rc->pcfp[i];
				break;
			}
		}
	}

	if (!cfp && freq)
		PRINTM(MERROR,
		       "%s: cannot find cfp by band %d & freq %d\n", __func__,
		       band, freq);

	LEAVE();
	return cfp;
}
#endif /* STA_SUPPORT */

/**
 *  @brief Check if Rate Auto
 *
 *  @param pmpriv               A pointer to mlan_private structure
 *
 *  @return                     MTRUE or MFALSE
 */
t_u8
wlan_is_rate_auto(mlan_private *pmpriv)
{
	t_u32 i;
	int rate_num = 0;

	ENTER();

	for (i = 0; i < NELEMENTS(pmpriv->bitmap_rates); i++)
		if (pmpriv->bitmap_rates[i])
			rate_num++;

	LEAVE();
	if (rate_num > 1)
		return MTRUE;
	else
		return MFALSE;
}

/**
 *  @brief Covert Rate Bitmap to Rate index
 *
 *  @param pmadapter    Pointer to mlan_adapter structure
 *  @param rate_bitmap  Pointer to rate bitmap
 *  @param size         Size of the bitmap array
 *
 *  @return             Rate index
 */
int
wlan_get_rate_index(pmlan_adapter pmadapter, t_u16 *rate_bitmap, int size)
{
	int i;

	ENTER();

	for (i = 0; i < size * 8; i++) {
		if (rate_bitmap[i / 16] & (1 << (i % 16))) {
			LEAVE();
			return i;
		}
	}

	LEAVE();
	return -1;
}

/**
 *  @brief Get supported data rates
 *
 *  @param pmpriv           A pointer to mlan_private structure
 *  @param bss_mode         The specified BSS mode (Infra/IBSS)
 *  @param config_bands     The specified band configuration
 *  @param rates            The buf to return the supported rates
 *
 *  @return                 The number of Rates
 */
t_u32
wlan_get_supported_rates(mlan_private *pmpriv, t_u32 bss_mode,
			 t_u16 config_bands, WLAN_802_11_RATES rates)
{
	t_u32 k = 0;

	ENTER();

	if (bss_mode == MLAN_BSS_MODE_INFRA) {
		/* Infra. mode */
		switch (config_bands) {
		case (t_u8)BAND_B:
			PRINTM(MINFO, "Infra Band=%d SupportedRates_B\n",
			       config_bands);
			k = wlan_copy_rates(rates, k, SupportedRates_B,
					    sizeof(SupportedRates_B));
			break;
		case (t_u8)BAND_G:
		case BAND_G | BAND_GN:
		case BAND_G | BAND_GN | BAND_GAC:
		case BAND_G | BAND_GN | BAND_GAC | BAND_GAX:
			PRINTM(MINFO, "Infra band=%d SupportedRates_G\n",
			       config_bands);
			k = wlan_copy_rates(rates, k, SupportedRates_G,
					    sizeof(SupportedRates_G));
			break;
		case BAND_B | BAND_G:
		case BAND_A | BAND_B | BAND_G:
		case BAND_A | BAND_B:
		case BAND_A | BAND_B | BAND_G | BAND_GN:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN | BAND_AAC:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN | BAND_AAC | BAND_GAC:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN | BAND_AAC | BAND_AAX:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN | BAND_AAC | BAND_GAC | BAND_AAX | BAND_GAX:
		case BAND_B | BAND_G | BAND_GN:
		case BAND_B | BAND_G | BAND_GN | BAND_GAC:
		case BAND_B | BAND_G | BAND_GN | BAND_GAC | BAND_GAX:
			PRINTM(MINFO, "Infra band=%d SupportedRates_BG\n",
			       config_bands);
#ifdef WIFI_DIRECT_SUPPORT
			if (pmpriv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT)
				k = wlan_copy_rates(rates, k, SupportedRates_G,
						    sizeof(SupportedRates_G));
			else
				k = wlan_copy_rates(rates, k, SupportedRates_BG,
						    sizeof(SupportedRates_BG));
#else
			k = wlan_copy_rates(rates, k, SupportedRates_BG,
					    sizeof(SupportedRates_BG));
#endif
			break;
		case BAND_A:
		case BAND_A | BAND_G:
			PRINTM(MINFO, "Infra band=%d SupportedRates_A\n",
			       config_bands);
			k = wlan_copy_rates(rates, k, SupportedRates_A,
					    sizeof(SupportedRates_A));
			break;
		case BAND_AN:
		case BAND_A | BAND_AN:
		case BAND_A | BAND_G | BAND_AN | BAND_GN:
		case BAND_A | BAND_AN | BAND_AAC:
		case BAND_A | BAND_G | BAND_AN | BAND_GN | BAND_AAC:
		case BAND_A | BAND_AN | BAND_AAC | BAND_AAX:
		case BAND_A | BAND_G | BAND_AN | BAND_GN | BAND_AAC | BAND_AAX:
			PRINTM(MINFO, "Infra band=%d SupportedRates_A\n",
			       config_bands);
			k = wlan_copy_rates(rates, k, SupportedRates_A,
					    sizeof(SupportedRates_A));
			break;
		case BAND_GN:
		case BAND_GN | BAND_GAC:
		case BAND_GN | BAND_GAC | BAND_GAX:
			PRINTM(MINFO, "Infra band=%d SupportedRates_N\n",
			       config_bands);
			k = wlan_copy_rates(rates, k, SupportedRates_N,
					    sizeof(SupportedRates_N));
			break;
		}
	} else {
		/* Ad-hoc mode */
		switch (config_bands) {
		case (t_u8)BAND_B:
			PRINTM(MINFO, "Band: Adhoc B\n");
			k = wlan_copy_rates(rates, k, AdhocRates_B,
					    sizeof(AdhocRates_B));
			break;
		case (t_u8)BAND_G:
			PRINTM(MINFO, "Band: Adhoc G only\n");
			k = wlan_copy_rates(rates, k, AdhocRates_G,
					    sizeof(AdhocRates_G));
			break;
		case BAND_B | BAND_G:
			PRINTM(MINFO, "Band: Adhoc BG\n");
			k = wlan_copy_rates(rates, k, AdhocRates_BG,
					    sizeof(AdhocRates_BG));
			break;
		case BAND_A:
		case BAND_A | BAND_AN | BAND_AAC:
		case BAND_A | BAND_AN | BAND_AAC | BAND_AAX:

			PRINTM(MINFO, "Band: Adhoc A\n");
			k = wlan_copy_rates(rates, k, AdhocRates_A,
					    sizeof(AdhocRates_A));
			break;
		}
	}

	LEAVE();
	return k;
}

#define COUNTRY_ID_US 0
#define COUNTRY_ID_JP 1
#define COUNTRY_ID_CN 2
#define COUNTRY_ID_EU 3
typedef struct _oper_bw_chan {
	/*non-global operating class */
	t_u8 oper_class;
	/*global operating class */
	t_u8 global_oper_class;
	/*bandwidth 0-20M 1-40M 2-80M 3-160M */
	t_u8 bandwidth;
	/*channel list */
	t_u8 channel_list[13];
} oper_bw_chan;

/** oper class table for US*/
static oper_bw_chan oper_bw_chan_us[] = {
	/** non-Global oper class, global oper class, bandwidth, channel list*/
	{1, 115, 0, {36, 40, 44, 48}},
	{2, 118, 0, {52, 56, 60, 64}},
	{3, 124, 0, {149, 153, 157, 161}},
	{4,
	 121,
	 0,
	 {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144}},
	{5, 125, 0, {149, 153, 157, 161, 165}},
	{12, 81, 0, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
	{22, 116, 1, {36, 44}},
	{23, 119, 1, {52, 60}},
	{24, 122, 1, {100, 108, 116, 124, 132, 140}},
	{25, 126, 1, {149, 157}},
	{26, 126, 1, {149, 157}},
	{27, 117, 1, {40, 48}},
	{28, 120, 1, {56, 64}},
	{29, 123, 1, {104, 112, 120, 128, 136, 144}},
	{30, 127, 1, {153, 161}},
	{31, 127, 1, {153, 161}},
	{32, 83, 1, {1, 2, 3, 4, 5, 6, 7}},
	{33, 84, 1, {5, 6, 7, 8, 9, 10, 11}},
	{128, 128, 2, {42, 58, 106, 122, 138, 155}},
	{129, 129, 3, {50, 114}},
	{130, 130, 2, {42, 58, 106, 122, 138, 155}},
};

/** oper class table for EU*/
static oper_bw_chan oper_bw_chan_eu[] = {
	/** non-global oper class,global oper class, bandwidth, channel list*/
	{1, 115, 0, {36, 40, 44, 48}},
	{2, 118, 0, {52, 56, 60, 64}},
	{3, 121, 0, {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}},
	{4, 81, 0, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
	{5, 116, 1, {36, 44}},
	{6, 119, 1, {52, 60}},
	{7, 122, 1, {100, 108, 116, 124, 132}},
	{8, 117, 1, {40, 48}},
	{9, 120, 1, {56, 64}},
	{10, 123, 1, {104, 112, 120, 128, 136}},
	{11, 83, 1, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
	{12, 84, 1, {5, 6, 7, 8, 9, 10, 11, 12, 13}},
	{17, 125, 0, {149, 153, 157, 161, 165, 169}},
	{128, 128, 2, {42, 58, 106, 122, 138, 155}},
	{129, 129, 3, {50, 114}},
	{130, 130, 2, {42, 58, 106, 122, 138, 155}},
};

/** oper class table for Japan*/
static oper_bw_chan oper_bw_chan_jp[] = {
	/** non-Global oper class,global oper class, bandwidth, channel list*/
	{1, 115, 0, {34, 38, 42, 46, 36, 40, 44, 48}},
	{30, 81, 0, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
	{31, 82, 0, {14}},
	{32, 118, 0, {52, 56, 60, 64}},
	{33, 118, 0, {52, 56, 60, 64}},
	{34, 121, 0, {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}},
	{35, 121, 0, {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}},
	{36, 116, 1, {36, 44}},
	{37, 119, 1, {52, 60}},
	{38, 119, 1, {52, 60}},
	{39, 122, 1, {100, 108, 116, 124, 132}},
	{40, 122, 1, {100, 108, 116, 124, 132}},
	{41, 117, 1, {40, 48}},
	{42, 120, 1, {56, 64}},
	{43, 120, 1, {56, 64}},
	{44, 123, 1, {104, 112, 120, 128, 136}},
	{45, 123, 1, {104, 112, 120, 128, 136}},
	{56, 83, 1, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
	{57, 84, 1, {5, 6, 7, 8, 9, 10, 11, 12, 13}},
	{58, 121, 0, {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}},
	{128, 128, 2, {42, 58, 106, 122, 138, 155}},
	{129, 129, 3, {50, 114}},
	{130, 130, 2, {42, 58, 106, 122, 138, 155}},
};

/** oper class table for China*/
static oper_bw_chan oper_bw_chan_cn[] = {
	/** non-Global oper class,global oper class, bandwidth, channel list*/
	{1, 115, 0, {36, 40, 44, 48}},
	{2, 118, 0, {52, 56, 60, 64}},
	{3, 125, 0, {149, 153, 157, 161, 165}},
	{4, 116, 1, {36, 44}},
	{5, 119, 1, {52, 60}},
	{6, 126, 1, {149, 157}},
	{7, 81, 0, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
	{8, 83, 0, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
	{9, 84, 1, {5, 6, 7, 8, 9, 10, 11, 12, 13}},
	{128, 128, 2, {42, 58, 106, 122, 138, 155}},
	{129, 129, 3, {50, 114}},
	{130, 130, 2, {42, 58, 106, 122, 138, 155}},
};

/**
 *  @brief Get non-global operaing class table according to country
 *
 *  @param pmpriv             A pointer to mlan_private structure
 *  @param arraysize          A pointer to table size
 *
 *  @return                   A pointer to oper_bw_chan
 */
static oper_bw_chan *
wlan_get_nonglobal_operclass_table(mlan_private *pmpriv, int *arraysize)
{
	t_u8 country_code[][COUNTRY_CODE_LEN] = { "US", "JP", "CN" };
	int country_id = 0;
	oper_bw_chan *poper_bw_chan = MNULL;

	ENTER();

	for (country_id = 0; country_id < 3; country_id++)
		if (!memcmp(pmpriv->adapter, pmpriv->adapter->country_code,
			    country_code[country_id], COUNTRY_CODE_LEN - 1))
			break;
	if (country_id >= 3)
		country_id = COUNTRY_ID_US;	/*Set default to US */
	if (wlan_is_etsi_country(pmpriv->adapter,
				 pmpriv->adapter->country_code))
		country_id = COUNTRY_ID_EU; /** Country in EU */

	switch (country_id) {
	case COUNTRY_ID_US:
		poper_bw_chan = oper_bw_chan_us;
		*arraysize = sizeof(oper_bw_chan_us);
		break;
	case COUNTRY_ID_JP:
		poper_bw_chan = oper_bw_chan_jp;
		*arraysize = sizeof(oper_bw_chan_jp);
		break;
	case COUNTRY_ID_CN:
		poper_bw_chan = oper_bw_chan_cn;
		*arraysize = sizeof(oper_bw_chan_cn);
		break;
	case COUNTRY_ID_EU:
		poper_bw_chan = oper_bw_chan_eu;
		*arraysize = sizeof(oper_bw_chan_eu);
		break;
	default:
		PRINTM(MERROR, "Country not support!\n");
		break;
	}

	LEAVE();
	return poper_bw_chan;
}

/**
 *  @brief Check validation of given channel and oper class
 *
 *  @param pmpriv             A pointer to mlan_private structure
 *  @param channel            Channel number
 *  @param oper_class         operating class
 *
 *  @return                   MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_check_operclass_validation(mlan_private *pmpriv, t_u8 channel,
				t_u8 oper_class)
{
	int arraysize = 0, i = 0, channum = 0;
	oper_bw_chan *poper_bw_chan = MNULL;
	t_u8 center_freq_idx = 0;
	t_u8 center_freqs[] = { 42, 50, 58, 106, 114, 122, 138, 155 };

	ENTER();

	for (i = 0; i < (int)sizeof(center_freqs); i++) {
		if (channel == center_freqs[i]) {
			PRINTM(MERROR, "Invalid channel number %d!\n", channel);
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}
	if (oper_class <= 0 || oper_class > 130) {
		PRINTM(MERROR, "Invalid operating class!\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	if (oper_class >= 128) {
		center_freq_idx =
			wlan_get_center_freq_idx(pmpriv, BAND_AAC, channel,
						 CHANNEL_BW_80MHZ);
		channel = center_freq_idx;
	}
	poper_bw_chan = wlan_get_nonglobal_operclass_table(pmpriv, &arraysize);

	if (!poper_bw_chan) {
		PRINTM(MCMND, "Operating class table do not find!\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	for (i = 0; i < (int)(arraysize / sizeof(oper_bw_chan)); i++) {
		if (poper_bw_chan[i].oper_class == oper_class ||
		    poper_bw_chan[i].global_oper_class == oper_class) {
			for (channum = 0;
			     channum <
			     (int)sizeof(poper_bw_chan[i].channel_list);
			     channum++) {
				if (poper_bw_chan[i].channel_list[channum] &&
				    poper_bw_chan[i].channel_list[channum] ==
				    channel) {
					LEAVE();
					return MLAN_STATUS_SUCCESS;
				}
			}
		}
	}

	PRINTM(MCMND, "Operating class %d do not match channel %d!\n",
	       oper_class, channel);
	LEAVE();
	return MLAN_STATUS_FAILURE;
}

/**
 *  @brief Get current operating class from channel and bandwidth
 *
 *  @param pmpriv             A pointer to mlan_private structure
 *  @param channel            Channel number
 *  @param bw                 Bandwidth
 *  @param oper_class         A pointer to current operating class
 *
 *  @return                   MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_get_curr_oper_class(mlan_private *pmpriv, t_u8 channel,
			 t_u8 bw, t_u8 *oper_class)
{
	oper_bw_chan *poper_bw_chan = MNULL;
	t_u8 center_freq_idx = 0;
	t_u8 center_freqs[] = { 42, 50, 58, 106, 114, 122, 138, 155 };
	int i = 0, arraysize = 0, channum = 0;

	ENTER();

	poper_bw_chan = wlan_get_nonglobal_operclass_table(pmpriv, &arraysize);

	if (!poper_bw_chan) {
		PRINTM(MCMND, "Operating class table do not find!\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	for (i = 0; i < (int)sizeof(center_freqs); i++) {
		if (channel == center_freqs[i]) {
			PRINTM(MERROR, "Invalid channel number %d!\n", channel);
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}
	if (bw == BW_80MHZ) {
		center_freq_idx =
			wlan_get_center_freq_idx(pmpriv, BAND_AAC, channel,
						 CHANNEL_BW_80MHZ);
		channel = center_freq_idx;
	}

	for (i = 0; i < (int)(arraysize / sizeof(oper_bw_chan)); i++) {
		if (poper_bw_chan[i].bandwidth == bw) {
			for (channum = 0;
			     channum <
			     (int)(sizeof(poper_bw_chan[i].channel_list));
			     channum++) {
				if (poper_bw_chan[i].channel_list[channum] &&
				    poper_bw_chan[i].channel_list[channum] ==
				    channel) {
					*oper_class =
						poper_bw_chan[i].oper_class;
					return MLAN_STATUS_SUCCESS;
				}
			}
		}
	}

	PRINTM(MCMND, "Operating class not find!\n");
	LEAVE();
	return MLAN_STATUS_FAILURE;
}

/**
 *  @brief Add Supported operating classes IE
 *
 *  @param pmpriv             A pointer to mlan_private structure
 *  @param pptlv_out          A pointer to TLV to fill in
 *  @param curr_oper_class    Current operating class
 *
 *  @return                   Length
 */
int
wlan_add_supported_oper_class_ie(mlan_private *pmpriv,
				 t_u8 **pptlv_out, t_u8 curr_oper_class)
{
	t_u8 oper_class_us[] = { 1,
		2,
		3,
		4,
		5,
		12,
		22,
		23,
		24,
		25,
		26,
		27,
		28,
		29,
		30,
		31,
		32,
		33,
		128,
		129,
		130
	};
	t_u8 oper_class_eu[] = { 1,
		2,
		3,
		4,
		5,
		6,
		7,
		8,
		9,
		10,
		11,
		12,
		17,
		128,
		129,
		130
	};
	t_u8 oper_class_jp[] = { 1,
		30,
		31,
		32,
		33,
		34,
		35,
		36,
		37,
		38,
		39,
		40,
		41,
		42,
		43,
		44,
		45,
		56,
		57,
		58,
		128,
		129,
		130
	};
	t_u8 oper_class_cn[] = { 1,
		2,
		3,
		4,
		5,
		6,
		7,
		8,
		9,
		128,
		129,
		130
	};
	t_u8 country_code[][COUNTRY_CODE_LEN] = { "US", "JP", "CN" };
	int country_id = 0, ret = 0;
	MrvlIETypes_SuppOperClass_t *poper_class = MNULL;

	ENTER();

	for (country_id = 0; country_id < 3; country_id++)
		if (!memcmp(pmpriv->adapter, pmpriv->adapter->country_code,
			    country_code[country_id], COUNTRY_CODE_LEN - 1))
			break;
	if (country_id >= 3)
		country_id = COUNTRY_ID_US;	/*Set default to US */
	if (wlan_is_etsi_country(pmpriv->adapter,
				 pmpriv->adapter->country_code))
		country_id = COUNTRY_ID_EU; /** Country in EU */
	poper_class = (MrvlIETypes_SuppOperClass_t *) * pptlv_out;
	memset(pmpriv->adapter, poper_class, 0,
	       sizeof(MrvlIETypes_SuppOperClass_t));
	poper_class->header.type = wlan_cpu_to_le16(REGULATORY_CLASS);
	if (country_id == COUNTRY_ID_US) {
		poper_class->header.len = sizeof(oper_class_us);
		memcpy_ext(pmpriv->adapter, &poper_class->oper_class,
			   oper_class_us, sizeof(oper_class_us),
			   poper_class->header.len);
	} else if (country_id == COUNTRY_ID_JP) {
		poper_class->header.len = sizeof(oper_class_jp);
		memcpy_ext(pmpriv->adapter, &poper_class->oper_class,
			   oper_class_jp, sizeof(oper_class_jp),
			   poper_class->header.len);
	} else if (country_id == COUNTRY_ID_CN) {
		poper_class->header.len = sizeof(oper_class_cn);
		memcpy_ext(pmpriv->adapter, &poper_class->oper_class,
			   oper_class_cn, sizeof(oper_class_cn),
			   poper_class->header.len);
	} else if (country_id == COUNTRY_ID_EU) {
		poper_class->header.len = sizeof(oper_class_eu);
		memcpy_ext(pmpriv->adapter, &poper_class->oper_class,
			   oper_class_eu, sizeof(oper_class_eu),
			   poper_class->header.len);
	}
	poper_class->current_oper_class = curr_oper_class;
	poper_class->header.len += sizeof(poper_class->current_oper_class);
	DBG_HEXDUMP(MCMD_D, "Operating class", (t_u8 *)poper_class,
		    sizeof(MrvlIEtypesHeader_t) + poper_class->header.len);
	ret = sizeof(MrvlIEtypesHeader_t) + poper_class->header.len;
	*pptlv_out += ret;
	poper_class->header.len = wlan_cpu_to_le16(poper_class->header.len);

	LEAVE();
	return ret;
}

/**
 *  @brief This function sets region table.
 *
 *  @param pmpriv  A pointer to mlan_private structure
 *  @param region  The region code
 *  @param band    The band
 *
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_set_regiontable(mlan_private *pmpriv, t_u8 region, t_u8 band)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	int i = 0, j;
	chan_freq_power_t *cfp;
	int cfp_no;
	region_chan_t region_chan_old[MAX_REGION_CHANNEL_NUM];
	t_u8 cfp_code_bg = region;
	t_u8 cfp_code_a = region;

	ENTER();

	memcpy_ext(pmadapter, region_chan_old, pmadapter->region_channel,
		   sizeof(pmadapter->region_channel), sizeof(region_chan_old));
	memset(pmadapter, pmadapter->region_channel, 0,
	       sizeof(pmadapter->region_channel));

	if (band & (BAND_B | BAND_G | BAND_GN)) {
		if (pmadapter->cfp_code_bg)
			cfp_code_bg = pmadapter->cfp_code_bg;
		PRINTM(MCMND, "%s: 2.4G 0x%x\n", __func__, cfp_code_bg);
		cfp = wlan_get_region_cfp_table(pmadapter, cfp_code_bg,
						BAND_G | BAND_B | BAND_GN,
						&cfp_no);
		if (cfp) {
			pmadapter->region_channel[i].num_cfp = (t_u8)cfp_no;
			pmadapter->region_channel[i].pcfp = cfp;
		} else {
			PRINTM(MERROR, "wrong region code %#x in Band B-G\n",
			       region);
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pmadapter->region_channel[i].valid = MTRUE;
		pmadapter->region_channel[i].region = region;
		if (band & BAND_GN)
			pmadapter->region_channel[i].band = BAND_G;
		else
			pmadapter->region_channel[i].band =
				(band & BAND_G) ? BAND_G : BAND_B;

		for (j = 0; j < MAX_REGION_CHANNEL_NUM; j++) {
			if (region_chan_old[j].band & (BAND_B | BAND_G))
				break;
		}

		if ((j < MAX_REGION_CHANNEL_NUM) &&
		    (region_chan_old[j].valid == MTRUE)) {
			wlan_cfp_copy_dynamic(pmadapter, cfp, cfp_no,
					      region_chan_old[j].pcfp,
					      region_chan_old[j].num_cfp);
		} else if (cfp) {
			wlan_cfp_copy_dynamic(pmadapter, cfp, cfp_no, MNULL, 0);
		}
		i++;
	}
	if (band & (BAND_A | BAND_AN | BAND_AAC)) {
		if (pmadapter->cfp_code_bg)
			cfp_code_a = pmadapter->cfp_code_a;
		PRINTM(MCMND, "%s: 5G 0x%x\n", __func__, cfp_code_a);
		cfp = wlan_get_region_cfp_table(pmadapter, cfp_code_a, BAND_A,
						&cfp_no);
		if (cfp) {
			pmadapter->region_channel[i].num_cfp = (t_u8)cfp_no;
			pmadapter->region_channel[i].pcfp = cfp;
		} else {
			PRINTM(MERROR, "wrong region code %#x in Band A\n",
			       region);
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pmadapter->region_channel[i].valid = MTRUE;
		pmadapter->region_channel[i].region = region;
		pmadapter->region_channel[i].band = BAND_A;

		for (j = 0; j < MAX_REGION_CHANNEL_NUM; j++) {
			if (region_chan_old[j].band & BAND_A)
				break;
		}
		if ((j < MAX_REGION_CHANNEL_NUM) && region_chan_old[j].valid) {
			wlan_cfp_copy_dynamic(pmadapter, cfp, cfp_no,
					      region_chan_old[j].pcfp,
					      region_chan_old[j].num_cfp);
		} else if (cfp) {
			wlan_cfp_copy_dynamic(pmadapter, cfp, cfp_no, MNULL, 0);
		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Get if radar detection is enabled or not on a certain channel
 *
 *  @param priv    Private driver information structure
 *  @param chnl Channel to determine radar detection requirements
 *
 *  @return
 *    - MTRUE if radar detection is required
 *    - MFALSE otherwise
 */
t_bool
wlan_get_cfp_radar_detect(mlan_private *priv, t_u8 chnl)
{
	int i, j;
	t_bool required = MFALSE;
	chan_freq_power_t *pcfp = MNULL;

	ENTER();

	/*get the cfp table first */
	for (i = 0; i < MAX_REGION_CHANNEL_NUM; i++) {
		if (priv->adapter->region_channel[i].band == BAND_A) {
			pcfp = priv->adapter->region_channel[i].pcfp;
			break;
		}
	}

	if (!pcfp) {
		/* This means operation in BAND-A is not support, we can
		 * just return false here, it's harmless
		 */
		goto done;
	}

	/*get the radar detection requirements according to chan num */
	for (j = 0; j < priv->adapter->region_channel[i].num_cfp; j++) {
		if (pcfp[j].channel == chnl) {
			required = pcfp[j].passive_scan_or_radar_detect;
			break;
		}
	}

done:
	LEAVE();
	return required;
}

/**
 *  @brief Get if scan type is passive or not on a certain channel for b/g band
 *
 *  @param priv    Private driver information structure
 *  @param chnl Channel to determine scan type
 *
 *  @return
 *    - MTRUE if scan type is passive
 *    - MFALSE otherwise
 */

t_bool
wlan_bg_scan_type_is_passive(mlan_private *priv, t_u8 chnl)
{
	int i, j;
	t_bool passive = MFALSE;
	chan_freq_power_t *pcfp = MNULL;

	ENTER();

	/*get the cfp table first */
	for (i = 0; i < MAX_REGION_CHANNEL_NUM; i++) {
		if (priv->adapter->region_channel[i].band & (BAND_B | BAND_G)) {
			pcfp = priv->adapter->region_channel[i].pcfp;
			break;
		}
	}

	if (!pcfp) {
		/*This means operation in BAND-B or BAND_G is not support, we
		 * can just return false here
		 */
		goto done;
	}

	/*get the bg scan type according to chan num */
	for (j = 0; j < priv->adapter->region_channel[i].num_cfp; j++) {
		if (pcfp[j].channel == chnl) {
			passive = pcfp[j].passive_scan_or_radar_detect;
			break;
		}
	}

done:
	LEAVE();
	return passive;
}

/**
 *  @brief Get if a channel is NO_IR (passive) or not
 *
 *  @param priv     Private driver information structure
 *  @param band     Band to check
 *  @param chan     Channel to check
 *
 *  @return
 *    - MTRUE if channel is passive
 *    - MFALSE otherwise
 */

t_bool
wlan_is_chan_passive(mlan_private *priv, t_u8 band, t_u8 chan)
{
	int i, j;
	t_bool passive = MFALSE;
	chan_freq_power_t *pcfp = MNULL;

	ENTER();

	/* get the cfp table first */
	for (i = 0; i < MAX_REGION_CHANNEL_NUM; i++) {
		if (priv->adapter->region_channel[i].band & band) {
			pcfp = priv->adapter->region_channel[i].pcfp;
			break;
		}
	}

	if (pcfp) {
		/* check table according to chan num */
		for (j = 0; j < priv->adapter->region_channel[i].num_cfp; j++) {
			if (pcfp[j].channel == chan) {
				if (pcfp[j].dynamic.flags & NXP_CHANNEL_PASSIVE)
					passive = MTRUE;
				break;
			}
		}
	}

	LEAVE();
	return passive;
}

/**
 *  @brief Get if a channel is disabled or not
 *
 *  @param priv     Private driver information structure
 *  @param band     Band to check
 *  @param chan     Channel to check
 *
 *  @return
 *    - MTRUE if channel is disabled
 *    - MFALSE otherwise
 */

t_bool
wlan_is_chan_disabled(mlan_private *priv, t_u8 band, t_u8 chan)
{
	int i, j;
	t_bool disabled = MFALSE;
	chan_freq_power_t *pcfp = MNULL;

	ENTER();

	/* get the cfp table first */
	for (i = 0; i < MAX_REGION_CHANNEL_NUM; i++) {
		if (priv->adapter->region_channel[i].band & band) {
			pcfp = priv->adapter->region_channel[i].pcfp;
			break;
		}
	}

	if (pcfp) {
		/* check table according to chan num */
		for (j = 0; j < priv->adapter->region_channel[i].num_cfp; j++) {
			if (pcfp[j].channel == chan) {
				if (pcfp[j].dynamic.flags &
				    NXP_CHANNEL_DISABLED)
					disabled = MTRUE;
				break;
			}
		}
	}

	LEAVE();
	return disabled;
}

/**
 *  @brief Get if a channel is blacklisted or not
 *
 *  @param priv     Private driver information structure
 *  @param band     Band to check
 *  @param chan     Channel to check
 *
 *  @return
 *    - MTRUE if channel is blacklisted
 *    - MFALSE otherwise
 */

t_bool
wlan_is_chan_blacklisted(mlan_private *priv, t_u8 band, t_u8 chan)
{
	int i, j;
	t_bool blacklist = MFALSE;
	chan_freq_power_t *pcfp = MNULL;

	ENTER();

	/*get the cfp table first */
	for (i = 0; i < MAX_REGION_CHANNEL_NUM; i++) {
		if (priv->adapter->region_channel[i].band & band) {
			pcfp = priv->adapter->region_channel[i].pcfp;
			break;
		}
	}

	if (pcfp) {
		/*check table according to chan num */
		for (j = 0; j < priv->adapter->region_channel[i].num_cfp; j++) {
			if (pcfp[j].channel == chan) {
				blacklist = pcfp[j].dynamic.blacklist;
				break;
			}
		}
	}

	LEAVE();
	return blacklist;
}

/**
 *  @brief Set a channel as blacklisted or not
 *
 *  @param priv     Private driver information structure
 *  @param band     Band to check
 *  @param chan     Channel to check
 *  @param bl       Blacklist if MTRUE
 *
 *  @return
 *    - MTRUE if channel setting is updated
 *    - MFALSE otherwise
 */

t_bool
wlan_set_chan_blacklist(mlan_private *priv, t_u8 band, t_u8 chan, t_bool bl)
{
	int i, j;
	t_bool set_bl = MFALSE;
	chan_freq_power_t *pcfp = MNULL;

	ENTER();

	/*get the cfp table first */
	for (i = 0; i < MAX_REGION_CHANNEL_NUM; i++) {
		if (priv->adapter->region_channel[i].band & band) {
			pcfp = priv->adapter->region_channel[i].pcfp;
			break;
		}
	}

	if (pcfp) {
		/*check table according to chan num */
		for (j = 0; j < priv->adapter->region_channel[i].num_cfp; j++) {
			if (pcfp[j].channel == chan) {
				pcfp[j].dynamic.blacklist = bl;
				set_bl = MTRUE;
				break;
			}
		}
	}

	LEAVE();
	return set_bl;
}

/**
 *  @brief Convert rateid in IEEE format to MRVL format
 *
 *  @param priv     Private driver information structure
 *  @param IeeeMacRate  Rate in terms of IEEE format
 *  @param pmbuf     A pointer to packet buffer
 *
 *  @return
 *    Rate ID in terms of MRVL format
 */
t_u8
wlan_ieee_rateid_to_mrvl_rateid(mlan_private *priv, t_u16 IeeeMacRate,
				t_u8 *dst_mac)
{
	/* Set default rate ID to RATEID_DBPSK1Mbps */
	t_u8 mrvlRATEID = 0;
	const rate_map *rate_tbl = rate_map_table_1x1;
	t_u32 cnt = sizeof(rate_map_table_1x1) / sizeof(rate_map);
	t_u8 skip_nss2 = MTRUE;
	t_u32 i = 0;
	IEEEtypes_HTCap_t *htcap = MNULL;
	t_u8 tx_mcs_supp = GET_TXMCSSUPP(priv->usr_dev_mcs_support);
#ifdef UAP_SUPPORT
	psta_node sta_ptr = MNULL;
#endif

	ENTER();

	if (priv->adapter->hw_dev_mcs_support == HT_STREAM_MODE_2X2) {
		rate_tbl = rate_map_table_2x2;
		cnt = sizeof(rate_map_table_2x2) / sizeof(rate_map);
	}
#ifdef UAP_SUPPORT
	if (priv->bss_role == MLAN_BSS_ROLE_UAP) {
		if (!dst_mac) {
			LEAVE();
			return mrvlRATEID;
		}
		sta_ptr =
			(sta_node *)util_peek_list(priv->adapter->pmoal_handle,
						   &priv->sta_list,
						   priv->adapter->callbacks.
						   moal_spin_lock,
						   priv->adapter->callbacks.
						   moal_spin_unlock);
		if (!sta_ptr) {
			LEAVE();
			return mrvlRATEID;
		}
		while (sta_ptr != (sta_node *)&priv->sta_list) {
			if (memcmp(priv->adapter, dst_mac, sta_ptr->mac_addr,
				   MLAN_MAC_ADDR_LENGTH)) {
				htcap = &(sta_ptr->HTcap);
				break;
			}
			sta_ptr = sta_ptr->pnext;
		}
	}
#endif
#ifdef STA_SUPPORT
	if (priv->bss_role == MLAN_BSS_ROLE_STA)
		htcap = priv->curr_bss_params.bss_descriptor.pht_cap;
#endif
	if (htcap) {
		/* If user configure tx to 2x2 and peer device rx is 2x2 */
		if (tx_mcs_supp >= 2 && htcap->ht_cap.supported_mcs_set[1])
			skip_nss2 = MFALSE;
	}

	for (i = 0; i < cnt; i++) {
		if (rate_tbl[i].nss && skip_nss2)
			continue;
		if (rate_tbl[i].rate == IeeeMacRate) {
			mrvlRATEID = rate_tbl[i].id;
			break;
		}
	}

	return mrvlRATEID;
}

/**
 *  @brief Convert rateid in MRVL format to IEEE format
 *
 *  @param IeeeMacRate  Rate in terms of MRVL format
 *
 *  @return
 *    Rate ID in terms of IEEE format
 */
t_u8
wlan_mrvl_rateid_to_ieee_rateid(t_u8 rate)
{
	return rateUnit_500Kbps[rate];
}

/**
 *  @brief	 sort cfp otp table
 *
 *  @param pmapdater	a pointer to mlan_adapter structure
 *
 *  @return
 *    None
 */
static void
wlan_sort_cfp_otp_table(mlan_adapter *pmadapter)
{
	t_u8 c, d;
	chan_freq_power_t *ch1;
	chan_freq_power_t *ch2;
	chan_freq_power_t swap;

	if (pmadapter->tx_power_table_a_rows <= 1)
		return;
	for (c = 0; c < pmadapter->tx_power_table_a_rows - 1; c++) {
		for (d = 0; d < pmadapter->tx_power_table_a_rows - c - 1; d++) {
			ch1 = (chan_freq_power_t *)(pmadapter->cfp_otp_a + d);
			ch2 = (chan_freq_power_t *)(pmadapter->cfp_otp_a + d +
						    1);
			if (ch1->channel > ch2->channel) {
				memcpy_ext(pmadapter, &swap, ch1,
					   sizeof(chan_freq_power_t),
					   sizeof(chan_freq_power_t));
				memcpy_ext(pmadapter, ch1, ch2,
					   sizeof(chan_freq_power_t),
					   sizeof(chan_freq_power_t));
				memcpy_ext(pmadapter, ch2, &swap,
					   sizeof(chan_freq_power_t),
					   sizeof(chan_freq_power_t));
			}
		}
	}
}

/**
 *  @brief	Update CFP tables and power tables from FW
 *
 *  @param priv		Private driver information structure
 *  @param buf		Pointer to the buffer holding TLV data
 *					from 0x242 command response.
 *  @param buf_left	bufsize
 *
 *  @return
 *    None
 */
void
wlan_add_fw_cfp_tables(pmlan_private pmpriv, t_u8 *buf, t_u16 buf_left)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	MrvlIEtypesHeader_t *head;
	t_u16 tlv;
	t_u16 tlv_buf_len;
	t_u16 tlv_buf_left;
	t_u16 i;
	int k = 0, rows, cols;
	t_u16 max_tx_pwr_bg = WLAN_TX_PWR_DEFAULT;
	t_u16 max_tx_pwr_a = WLAN_TX_PWR_DEFAULT;
	t_u8 *tlv_buf;
	t_u8 *data;
	t_u8 *tmp;
	mlan_status ret;

	ENTER();

	if (!buf) {
		PRINTM(MERROR, "CFP table update failed!\n");
		goto out;
	}
	if (pmadapter->otp_region) {
		memset(pmadapter, pmadapter->region_channel, 0,
		       sizeof(pmadapter->region_channel));
		wlan_free_fw_cfp_tables(pmadapter);
	}
	pmadapter->tx_power_table_bg_rows = FW_CFP_TABLE_MAX_ROWS_BG;
	pmadapter->tx_power_table_bg_cols = FW_CFP_TABLE_MAX_COLS_BG;
	pmadapter->tx_power_table_a_rows = FW_CFP_TABLE_MAX_ROWS_A;
	pmadapter->tx_power_table_a_cols = FW_CFP_TABLE_MAX_COLS_A;
	tlv_buf = (t_u8 *)buf;
	tlv_buf_left = buf_left;

	while (tlv_buf_left >= sizeof(*head)) {
		head = (MrvlIEtypesHeader_t *)tlv_buf;
		tlv = wlan_le16_to_cpu(head->type);
		tlv_buf_len = wlan_le16_to_cpu(head->len);

		if (tlv_buf_left < (sizeof(*head) + tlv_buf_len))
			break;
		data = (t_u8 *)head + sizeof(*head);

		switch (tlv) {
		case TLV_TYPE_REGION_INFO:
			/* Skip adding fw region info if it already exists or
			 * if this TLV has no set data
			 */
			if (*data == 0)
				break;
			if (pmadapter->otp_region)
				break;

			ret = pcb->moal_malloc(pmadapter->pmoal_handle,
					       sizeof(otp_region_info_t),
					       MLAN_MEM_DEF,
					       (t_u8 **)&pmadapter->otp_region);
			if (ret != MLAN_STATUS_SUCCESS ||
			    !pmadapter->otp_region) {
				PRINTM(MERROR,
				       "Memory allocation for the otp region info struct failed!\n");
				break;
			}
			/* Save region info values from OTP in the otp_region
			 * structure
			 */
			memcpy_ext(pmadapter, pmadapter->otp_region, data,
				   sizeof(otp_region_info_t),
				   sizeof(otp_region_info_t));
			data += sizeof(otp_region_info_t);
			/* Get pre-defined cfp tables corresponding to the
			 * region code in OTP
			 */
			for (i = 0; i < MLAN_CFP_TABLE_SIZE_BG; i++) {
				if (cfp_table_BG[i].code ==
				    pmadapter->otp_region->region_code) {
					max_tx_pwr_bg = (cfp_table_BG[i].cfp)
						->max_tx_power;
					break;
				}
			}
			for (i = 0; i < MLAN_CFP_TABLE_SIZE_A; i++) {
				if (cfp_table_A[i].code ==
				    pmadapter->otp_region->region_code) {
					max_tx_pwr_a = (cfp_table_A[i].cfp)
						->max_tx_power;
					break;
				}
			}
			/* Update the region code and the country code in
			 * pmadapter
			 */
			pmadapter->region_code =
				pmadapter->otp_region->region_code;
			pmadapter->country_code[0] =
				pmadapter->otp_region->country_code[0];
			pmadapter->country_code[1] =
				pmadapter->otp_region->country_code[1];
			pmadapter->country_code[2] = '\0';
			pmadapter->domain_reg.country_code[0] =
				pmadapter->otp_region->country_code[0];
			pmadapter->domain_reg.country_code[1] =
				pmadapter->otp_region->country_code[1];
			pmadapter->domain_reg.country_code[2] = '\0';
			PRINTM(MCMND, "OTP region: region_code=%d %c%c\n",
			       pmadapter->otp_region->region_code,
			       pmadapter->country_code[0],
			       pmadapter->country_code[1]);
			pmadapter->cfp_code_bg =
				pmadapter->otp_region->region_code;
			pmadapter->cfp_code_a =
				pmadapter->otp_region->region_code;
			break;
		case TLV_TYPE_CHAN_ATTR_CFG:
			/* Skip adding fw cfp tables if they already exist or
			 * if this TLV has no set data
			 */
			if (*data == 0)
				break;
			if (pmadapter->cfp_otp_bg || pmadapter->cfp_otp_a) {
				break;
			}

			ret = pcb->moal_malloc(pmadapter->pmoal_handle,
					       pmadapter->
					       tx_power_table_bg_rows *
					       sizeof(chan_freq_power_t),
					       MLAN_MEM_DEF,
					       (t_u8 **)&pmadapter->cfp_otp_bg);
			if (ret != MLAN_STATUS_SUCCESS ||
			    !pmadapter->cfp_otp_bg) {
				PRINTM(MERROR,
				       "Memory allocation for storing otp bg table data failed!\n");
				break;
			}
			/* Save channel usability flags from OTP data in the fw
			 * cfp bg table and set frequency and max_tx_power
			 * values
			 */
			for (i = 0; i < pmadapter->tx_power_table_bg_rows; i++) {
				(pmadapter->cfp_otp_bg + i)->channel = *data;
				if (*data == 14)
					(pmadapter->cfp_otp_bg + i)->freq =
						2484;
				else
					(pmadapter->cfp_otp_bg + i)->freq =
						2412 + 5 * (*data - 1);
				(pmadapter->cfp_otp_bg + i)->max_tx_power =
					max_tx_pwr_bg;
				data++;
				(pmadapter->cfp_otp_bg + i)->dynamic.flags =
					*data;
				if (*data & NXP_CHANNEL_DFS)
					(pmadapter->cfp_otp_bg + i)
						->passive_scan_or_radar_detect =
						MTRUE;
				PRINTM(MCMD_D,
				       "OTP Region (BG): chan=%d flags=0x%x\n",
				       (pmadapter->cfp_otp_bg + i)->channel,
				       (pmadapter->cfp_otp_bg +
					i)->dynamic.flags);
				data++;
			}
			ret = pcb->moal_malloc(pmadapter->pmoal_handle,
					       pmadapter->
					       tx_power_table_a_rows *
					       sizeof(chan_freq_power_t),
					       MLAN_MEM_DEF,
					       (t_u8 **)&pmadapter->cfp_otp_a);
			if (ret != MLAN_STATUS_SUCCESS || !pmadapter->cfp_otp_a) {
				PRINTM(MERROR,
				       "Memory allocation for storing otp a table data failed!\n");
				break;
			}
			/* Save channel usability flags from OTP data in the fw
			 * cfp a table and set frequency and max_tx_power values
			 */
			for (i = 0; i < pmadapter->tx_power_table_a_rows; i++) {
				(pmadapter->cfp_otp_a + i)->channel = *data;
				if (*data < 183)
					/* 5GHz channels */
					(pmadapter->cfp_otp_a + i)->freq =
						5035 + 5 * (*data - 7);
				else
					/* 4GHz channels */
					(pmadapter->cfp_otp_a + i)->freq =
						4915 + 5 * (*data - 183);
				(pmadapter->cfp_otp_a + i)->max_tx_power =
					max_tx_pwr_a;
				data++;
				(pmadapter->cfp_otp_a + i)->dynamic.flags =
					*data;
				if (*data & NXP_CHANNEL_DFS)
					(pmadapter->cfp_otp_a + i)
						->passive_scan_or_radar_detect =
						MTRUE;
				PRINTM(MCMD_D,
				       "OTP Region (A): chan=%d flags=0x%x\n",
				       (pmadapter->cfp_otp_a + i)->channel,
				       (pmadapter->cfp_otp_a +
					i)->dynamic.flags);

				data++;
			}
			break;
		case TLV_TYPE_POWER_TABLE:
			/* Skip adding fw power tables if this TLV has no data
			 * or if they already exists but force reg rule is set
			 * in the otp
			 */
			if (*data == 0)
				break;
			if (pmadapter->otp_region &&
			    pmadapter->otp_region->force_reg &&
			    pmadapter->tx_power_table_bg)
				break;

			/* Save the tlv data in power tables for band BG and A
			 */
			tmp = data;
			i = 0;
			while ((i <
				pmadapter->tx_power_table_bg_rows *
				pmadapter->tx_power_table_bg_cols) &&
			       (i < tlv_buf_len) && (*tmp != 36)) {
				i++;
				tmp++;
			}
			if (!pmadapter->tx_power_table_bg) {
				ret = pcb->moal_malloc(pmadapter->pmoal_handle,
						       i, MLAN_MEM_DEF,
						       (t_u8 **)&pmadapter->
						       tx_power_table_bg);
				if (ret != MLAN_STATUS_SUCCESS ||
				    !pmadapter->tx_power_table_bg) {
					PRINTM(MERROR,
					       "Memory allocation for the BG-band power table failed!\n");
					break;
				}
			}
			memcpy_ext(pmadapter, pmadapter->tx_power_table_bg,
				   data, i, i);
			pmadapter->tx_power_table_bg_size = i;
			data += i;
			i = 0;
			while ((i < pmadapter->tx_power_table_a_rows *
				pmadapter->tx_power_table_a_cols) &&
			       (i < (tlv_buf_len -
				     pmadapter->tx_power_table_bg_size))) {
				i++;
			}
			if (!pmadapter->tx_power_table_a) {
				ret = pcb->moal_malloc(pmadapter->pmoal_handle,
						       i, MLAN_MEM_DEF,
						       (t_u8 **)&pmadapter->
						       tx_power_table_a);
				if (ret != MLAN_STATUS_SUCCESS ||
				    !pmadapter->tx_power_table_a) {
					PRINTM(MERROR,
					       "Memory allocation for the A-band power table failed!\n");
					break;
				}
			}
			memcpy_ext(pmadapter, pmadapter->tx_power_table_a, data,
				   i, i);
			pmadapter->tx_power_table_a_size = i;
			break;
		case TLV_TYPE_POWER_TABLE_ATTR:
			pmadapter->tx_power_table_bg_rows =
				((power_table_attr_t *) data)->rows_2g;
			pmadapter->tx_power_table_bg_cols =
				((power_table_attr_t *) data)->cols_2g;
			pmadapter->tx_power_table_a_rows =
				((power_table_attr_t *) data)->rows_5g;
			pmadapter->tx_power_table_a_cols =
				((power_table_attr_t *) data)->cols_5g;
			PRINTM(MCMD_D, "OTP region: bg_row=%d, a_row=%d\n",
			       pmadapter->tx_power_table_bg_rows,
			       pmadapter->tx_power_table_a_rows);
			break;
		default:
			break;
		}
		tlv_buf += (sizeof(*head) + tlv_buf_len);
		tlv_buf_left -= (sizeof(*head) + tlv_buf_len);
	}
	if (!pmadapter->cfp_otp_bg || !pmadapter->tx_power_table_bg)
		goto out;
	/* Set remaining flags for BG */
	rows = pmadapter->tx_power_table_bg_rows;
	cols = pmadapter->tx_power_table_bg_cols;

	for (i = 0; i < rows; i++) {
		k = (i * cols) + 1;
		if ((pmadapter->cfp_otp_bg + i)->dynamic.flags &
		    NXP_CHANNEL_DISABLED)
			continue;

		if (pmadapter->tx_power_table_bg[k + MOD_CCK] == 0)
			(pmadapter->cfp_otp_bg + i)->dynamic.flags |=
				NXP_CHANNEL_NO_CCK;

		if (pmadapter->tx_power_table_bg[k + MOD_OFDM_PSK] == 0 &&
		    pmadapter->tx_power_table_bg[k + MOD_OFDM_QAM16] == 0 &&
		    pmadapter->tx_power_table_bg[k + MOD_OFDM_QAM64] == 0) {
			(pmadapter->cfp_otp_bg + i)->dynamic.flags |=
				NXP_CHANNEL_NO_OFDM;
		}
	}
	if (pmadapter->cfp_otp_a)
		wlan_sort_cfp_otp_table(pmadapter);
out:
	LEAVE();
}

/**
 *  @brief	This function deallocates otp cfp and power tables memory.
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 */
void
wlan_free_fw_cfp_tables(mlan_adapter *pmadapter)
{
	pmlan_callbacks pcb;

	ENTER();

	pcb = &pmadapter->callbacks;
	if (pmadapter->otp_region)
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->otp_region);
	if (pmadapter->cfp_otp_bg)
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->cfp_otp_bg);
	if (pmadapter->tx_power_table_bg)
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->tx_power_table_bg);
	pmadapter->otp_region = MNULL;
	pmadapter->cfp_otp_bg = MNULL;
	pmadapter->tx_power_table_bg = MNULL;
	pmadapter->tx_power_table_bg_size = 0;
	if (pmadapter->cfp_otp_a)
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->cfp_otp_a);
	if (pmadapter->tx_power_table_a)
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->tx_power_table_a);
	pmadapter->cfp_otp_a = MNULL;
	pmadapter->tx_power_table_a = MNULL;
	pmadapter->tx_power_table_a_size = 0;
	LEAVE();
}

/**
 *  @brief Get DFS chan list
 *
 *  @param pmadapter    Pointer to mlan_adapter
 *  @param pioctl_req   Pointer to mlan_ioctl_req
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_get_cfp_table(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_ds_misc_cfg *ds_misc_cfg = MNULL;
	mlan_status ret = MLAN_STATUS_FAILURE;
	chan_freq_power_t *cfp = MNULL;
	t_u32 cfp_no = 0;

	ENTER();
	if (pioctl_req) {
		ds_misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
		if (pioctl_req->action == MLAN_ACT_GET) {
			cfp = wlan_get_region_cfp_table(pmadapter,
							pmadapter->region_code,
							ds_misc_cfg->param.cfp.
							band, &cfp_no);
			if (cfp) {
				ds_misc_cfg->param.cfp.num_chan = cfp_no;
				memcpy_ext(pmadapter,
					   ds_misc_cfg->param.cfp.cfp_tbl, cfp,
					   cfp_no * sizeof(chan_freq_power_t),
					   cfp_no * sizeof(chan_freq_power_t));
			}
			ret = MLAN_STATUS_SUCCESS;
		}
	}
	LEAVE();
	return ret;
}

/**
 *  @brief	Get power tables and cfp tables for set region code
 *			into the IOCTL request buffer
 *
 *  @param pmadapter	Private mlan adapter structure
 *  @param pioctl_req	Pointer to the IOCTL request structure
 *
 *  @return	success, otherwise fail
 *
 */
mlan_status
wlan_get_cfpinfo(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	chan_freq_power_t *cfp_bg = MNULL;
	t_u32 cfp_no_bg = 0;
	chan_freq_power_t *cfp_a = MNULL;
	t_u32 cfp_no_a = 0;
	t_u8 cfp_code_a = pmadapter->region_code;
	t_u8 cfp_code_bg = pmadapter->region_code;
	t_u32 len = 0, size = 0;
	t_u8 *req_buf, *tmp;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (!pioctl_req || !pioctl_req->pbuf) {
		PRINTM(MERROR, "MLAN IOCTL information is not present!\n");
		ret = MLAN_STATUS_FAILURE;
		goto out;
	}
	/* Calculate the total response size required to return region,
	 * country codes, cfp tables and power tables
	 */
	size = sizeof(pmadapter->country_code) + sizeof(pmadapter->region_code);
	/* Add size to store region, country and environment codes */
	size += sizeof(t_u32);
	if (pmadapter->cfp_code_bg)
		cfp_code_bg = pmadapter->cfp_code_bg;

	/* Get cfp table and its size corresponding to the region code */
	cfp_bg = wlan_get_region_cfp_table(pmadapter, cfp_code_bg,
					   BAND_G | BAND_B, &cfp_no_bg);
	size += cfp_no_bg * sizeof(chan_freq_power_t);
	if (pmadapter->cfp_code_a)
		cfp_code_a = pmadapter->cfp_code_a;
	cfp_a = wlan_get_region_cfp_table(pmadapter, cfp_code_a,
					  BAND_A, &cfp_no_a);
	size += cfp_no_a * sizeof(chan_freq_power_t);
	if (pmadapter->otp_region)
		size += sizeof(pmadapter->otp_region->environment);

	/* Get power table size */
	if (pmadapter->tx_power_table_bg) {
		size += pmadapter->tx_power_table_bg_size;
		/* Add size to store table size, rows and cols */
		size += 3 * sizeof(t_u32);
	}
	if (pmadapter->tx_power_table_a) {
		size += pmadapter->tx_power_table_a_size;
		size += 3 * sizeof(t_u32);
	}
	/* Check information buffer length of MLAN IOCTL */
	if (pioctl_req->buf_len < size) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->buf_len_needed = size;
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto out;
	}
	/* Copy the total size of region code, country code and environment
	 * in first four bytes of the IOCTL request buffer and then copy
	 * codes respectively in following bytes
	 */
	req_buf = (t_u8 *)pioctl_req->pbuf;
	size = sizeof(pmadapter->country_code) + sizeof(pmadapter->region_code);
	if (pmadapter->otp_region)
		size += sizeof(pmadapter->otp_region->environment);
	tmp = (t_u8 *)&size;
	memcpy_ext(pmadapter, req_buf, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);
	memcpy_ext(pmadapter, req_buf + len, &pmadapter->region_code,
		   sizeof(pmadapter->region_code),
		   sizeof(pmadapter->region_code));
	len += sizeof(pmadapter->region_code);
	memcpy_ext(pmadapter, req_buf + len, &pmadapter->country_code,
		   sizeof(pmadapter->country_code),
		   sizeof(pmadapter->country_code));
	len += sizeof(pmadapter->country_code);
	if (pmadapter->otp_region) {
		memcpy_ext(pmadapter, req_buf + len,
			   &pmadapter->otp_region->environment,
			   sizeof(pmadapter->otp_region->environment),
			   sizeof(pmadapter->otp_region->environment));
		len += sizeof(pmadapter->otp_region->environment);
	}
	/* copy the cfp table size followed by the entire table */
	if (!cfp_bg)
		goto out;
	size = cfp_no_bg * sizeof(chan_freq_power_t);
	memcpy_ext(pmadapter, req_buf + len, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);
	memcpy_ext(pmadapter, req_buf + len, cfp_bg, size, size);
	len += size;
	if (!cfp_a)
		goto out;
	size = cfp_no_a * sizeof(chan_freq_power_t);
	memcpy_ext(pmadapter, req_buf + len, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);
	memcpy_ext(pmadapter, req_buf + len, cfp_a, size, size);
	len += size;
	/* Copy the size of the power table, number of rows, number of cols
	 * and the entire power table
	 */
	if (!pmadapter->tx_power_table_bg)
		goto out;
	size = pmadapter->tx_power_table_bg_size;
	memcpy_ext(pmadapter, req_buf + len, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);

	/* No. of rows */
	size = pmadapter->tx_power_table_bg_rows;
	memcpy_ext(pmadapter, req_buf + len, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);

	/* No. of cols */
	size = pmadapter->tx_power_table_bg_size /
		pmadapter->tx_power_table_bg_rows;
	memcpy_ext(pmadapter, req_buf + len, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);
	memcpy_ext(pmadapter, req_buf + len, pmadapter->tx_power_table_bg,
		   pmadapter->tx_power_table_bg_size,
		   pmadapter->tx_power_table_bg_size);
	len += pmadapter->tx_power_table_bg_size;
	if (!pmadapter->tx_power_table_a)
		goto out;
	size = pmadapter->tx_power_table_a_size;
	memcpy_ext(pmadapter, req_buf + len, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);

	/* No. of rows */
	size = pmadapter->tx_power_table_a_rows;
	memcpy_ext(pmadapter, req_buf + len, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);

	/* No. of cols */
	size = pmadapter->tx_power_table_a_size /
		pmadapter->tx_power_table_a_rows;
	memcpy_ext(pmadapter, req_buf + len, tmp, sizeof(size), sizeof(size));
	len += sizeof(size);
	memcpy_ext(pmadapter, req_buf + len, pmadapter->tx_power_table_a,
		   pmadapter->tx_power_table_a_size,
		   pmadapter->tx_power_table_a_size);
	len += pmadapter->tx_power_table_a_size;
out:
	if (pioctl_req)
		pioctl_req->data_read_written = len;

	LEAVE();
	return ret;
}
