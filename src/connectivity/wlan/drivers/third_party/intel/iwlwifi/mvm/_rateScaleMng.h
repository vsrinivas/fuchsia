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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM__RATESCALEMNG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM__RATESCALEMNG_H_

#define RS_MNG_INVALID_VAL ((U32)-1)
#define RS_MNG_RATE_MIN_FAILURE_TH 3
#define RS_MNG_RATE_MIN_SUCCESS_TH 8

#define RS_MNG_LEGACY_FAILURE_LIMIT 160
#define RS_MNG_LEGACY_SUCCESS_LIMIT 480
#define RS_MNG_LEGACY_MOD_COUNTER_LIMIT 160
#define RS_MNG_NON_LEGACY_FAILURE_LIMIT 400
#define RS_MNG_NON_LEGACY_SUCCESS_LIMIT 4500
#define RS_MNG_NON_LEGACY_MOD_COUNTER_LIMIT 1500

#define RS_MNG_UPSCALE_AGG_FRAME_COUNT 10
#define RS_MNG_UPSCALE_MAX_FREQUENCY MSEC_TO_USEC(100)
#define RS_MNG_UPSCALE_SEARCH_CYCLE_MAX_FREQ MSEC_TO_USEC(300)

#define RS_MNG_STATS_FLUSH_TIME_LIMIT SEC_TO_USEC(5)

#define RS_MNG_SR_FORCE_DECREASE 15 /* percent */
#define RS_MNG_SR_NO_DECREASE 85    /* percent */

#define GET_LOWER_SUPPORTED_RATE 0
#define GET_HIGHER_SUPPORTED_RATE 1

#define RS_MNG_RETRY_TABLE_INITIAL_RATE_NUM 3
#define RS_MNG_RETRY_TABLE_SECONDARY_RATE_NUM 1
#define RS_MNG_RETRY_TABLE_SECONDARY_RATE_20MHZ_NUM 2

#define RS_MNG_MAX_WINDOW_SIZE 256        //# tx in history window, ht/vht/he
#define RS_MNG_MAX_WINDOW_SIZE_NON_HT 64  //# tx in history window, non-ht

#define MAX_NEXT_COLUMNS 7
#define MAX_COLUMN_CHECKS 4

#define RS_STAT_THOLD 20

#define GET_OFDM_LEGACY_RATE_IDX(rate) \
    OFDM_LEGACY_RATE_IDX_TO_UNIFIED[(((rate.rate_n_flags) & RATE_MCS_CODE_MSK) >> 2)]
#define RS_MNG_PERCENT(x) (((x)*128) / 100)
#define IS_RS_MNG_COL_ID_VALID(colId) (colId < RS_MNG_COL_INVALID)

// used for indexing into the expected tpt array
#define RS_MNG_NO_AGG 0
#define RS_MNG_AGG 1
#define RS_MNG_NGI 0
#define RS_MNG_SGI 1
#define RS_MNG_GI_3_2 0
#define RS_MNG_GI_1_6 1
#define RS_MNG_GI_0_8 2
#define RS_MNG_SISO 0
#define RS_MNG_MIMO 1

#define RS_MNG_TPC_SR_INCREASE RS_MNG_PERCENT(95)
#define RS_MNG_TPC_SR_DECREASE RS_MNG_PERCENT(85)
#define RS_MNG_TPC_SR_DISABLE RS_MNG_PERCENT(10)
#define RS_MNG_TPC_AMSDU_ENABLE MSEC_TO_USEC(500)

#define RS_MNG_MAX_TID_COUNT 8
#define RS_MNG_AGG_FRAME_CNT_LIMIT 64
#define RS_MNG_AGG_DURATION_LIMIT MSEC_TO_USEC(4) /* valid 100-8000 usec */
#define RS_MNG_AGG_DURATION_LIMIT_SHORT 1200

#define RS_MNG_INVALID_RATE_IDX 0xFF

#define RS_MNG_IS_TID_VALID(tid) (tid < RS_MNG_MAX_TID_COUNT)

#define RS_MNG_AMSDU_SR_ENABLE_THRESHOLD 120  // 94%, normalized to 128
#define RS_MNG_AMSDU_SR_DISABLE_THRESHOLD 89  // 70%, normalized to 128
#define RS_MNG_AMSDU_TL_ENABLE_THRESHOLD 20000
#define RS_MNG_AMSDU_TL_DISABLE_THRESHOLD 6000
#define RS_MNG_AMSDU_3K_THRESHOLD 350      // mbps
#define RS_MNG_AMSDU_5K_THRESHOLD 700      // mbps
#define RS_MNG_AMSDU_HE_6K_THRESHOLD 900   // mbps
#define RS_MNG_AMSDU_HE_8K_THRESHOLD 1100  // mbps
#define RS_MNG_AMSDU_FAIL_CONSEC_THRESHOLD 3
#define RS_MNG_AMSDU_FAIL_TIME_THRESHOLD SEC_TO_USEC(1)

// Disallow AMSDU on low latency TIDs (VI, VO)
#define RS_MNG_AMSDU_VALID_TIDS_MSK 0xF

/***** Data Types *****/

typedef enum _RS_MNG_ACTION_E {
    RS_MNG_ACTION_STAY = 0,
    RS_MNG_ACTION_UPSCALE = 1,
    RS_MNG_ACTION_DOWNSCALE = 2,
} RS_MNG_ACTION_E;

enum {
    PARAMS_TBL_IDX_INIT_NUM_RATES,
    PARAMS_TBL_IDX_INIT_NUM_RETRIES,
    PARAMS_TBL_IDX_SEC_NUM_RATES,
    PARAMS_TBL_IDX_SEC_NUM_RETRIES,
    PARAMS_TBL_NUM_COLS,
};

typedef struct _RS_MNG_STA_LIMITS_S {
    U32 successFramesLimit;    // successfull frames threshold for starting a search cycle.
    U32 failedFramesLimit;     // failed frames threshold for starting a search cycle.
    U32 statsFlushTimeLimit;   // time thrshold for starting a search cycle, in usec.
    U32 clearTblWindowsLimit;  // txed frames threshold for clearing table windows during
    // stay-in-col.
} RS_MNG_STA_LIMITS_S;

// TX AMSDU size
typedef enum _RS_MNG_TX_AMSDU_SIZE_E {
    RS_MNG_AMSDU_INVALID,
    RS_MNG_AMSDU_3500B,
    RS_MNG_AMSDU_5000B,
    RS_MNG_AMSDU_6500B,
    RS_MNG_AMSDU_8000B,

    RS_MNG_AMSDU_SIZE_NUM,  // keep last
} RS_MNG_TX_AMSDU_SIZE_E;

#define RS_MNG_AMSDU_SIZE_ALL (BIT(RS_MNG_AMSDU_3500B) | BIT(RS_MNG_AMSDU_5000B))

// rs_column_mode
typedef enum _RS_MNG_MODULATION_E {
    RS_MNG_MODUL_LEGACY,
    RS_MNG_MODUL_SISO,
    RS_MNG_MODUL_MIMO2,
    RS_MNG_NUM_MODULATIONS,  // keep last
    RS_MNG_MODUL_INVALID = RS_MNG_NUM_MODULATIONS,
} RS_MNG_MODULATION_E;

typedef enum _RS_MNG_GI_E {
    HT_VHT_NGI,
    HT_VHT_SGI,
    HT_VHT_LAST_GI = HT_VHT_SGI,
    HE_3_2_GI,
    HE_FIRST_GI = HE_3_2_GI,
    HE_1_6_GI,
    HE_0_8_GI,
} RS_MNG_GI_E;

typedef enum _RS_NON_HT_RATES_E {
    RS_NON_HT_RATE_CCK_1M,
    RS_NON_HT_RATE_CCK_2M,
    RS_NON_HT_RATE_CCK_5_5M,
    RS_NON_HT_RATE_CCK_11M,
    RS_NON_HT_RATE_CCK_LAST = RS_NON_HT_RATE_CCK_11M,
    RS_NON_HT_RATE_OFDM_6M,
    RS_NON_HT_RATE_OFDM_9M,
    RS_NON_HT_RATE_OFDM_12M,
    RS_NON_HT_RATE_OFDM_18M,
    RS_NON_HT_RATE_OFDM_24M,
    RS_NON_HT_RATE_OFDM_36M,
    RS_NON_HT_RATE_OFDM_48M,
    RS_NON_HT_RATE_OFDM_54M,

    RS_NON_HT_RATE_OFDM_LAST = RS_NON_HT_RATE_OFDM_54M,
    RS_NON_HT_RATE_LAST = RS_NON_HT_RATE_OFDM_LAST,
    RS_NON_HT_RATE_NUM,
} RS_NON_HT_RATES_E;

typedef enum _RS_MCS_E {
    RS_MCS_0,
    RS_MCS_1,
    RS_MCS_2,
    RS_MCS_3,
    RS_MCS_4,
    RS_MCS_5,
    RS_MCS_6,
    RS_MCS_7,
    RS_MCS_HT_LAST = RS_MCS_7,
    RS_MCS_8,
    RS_MCS_20MHZ_LAST = RS_MCS_8,
    RS_MCS_9,
    RS_MCS_VHT_LAST = RS_MCS_9,
    RS_MCS_10,
    RS_MCS_11,
    RS_MCS_HE_LAST = RS_MCS_11,

    RS_MCS_0_HE_ER_AND_DCM,

    RS_MCS_NUM,
} RS_MCS_E;

#define RS_MNG_MAX_RATES_NUM MAX((U08)RS_NON_HT_RATE_NUM, (U08)RS_MCS_NUM)

typedef enum _RS_MNG_STATE_E {
    RS_MNG_STATE_SEARCH_CYCLE_STARTED,
    RS_MNG_STATE_TPC_SEARCH,
    RS_MNG_STATE_STAY_IN_COLUMN,
} RS_MNG_STATE_E;

typedef enum _RS_MNG_COLUMN_DESC_E {
    RS_MNG_COL_NON_HT_ANT_A = 0,
    RS_MNG_COL_NON_HT_ANT_B,
    RS_MNG_COL_SISO_ANT_A,
    RS_MNG_COL_FIRST_HT_VHT = RS_MNG_COL_SISO_ANT_A,
    RS_MNG_COL_SISO_ANT_B,
    RS_MNG_COL_SISO_ANT_A_SGI,
    RS_MNG_COL_SISO_ANT_B_SGI,
    RS_MNG_COL_MIMO2,
    RS_MNG_COL_MIMO2_SGI,                           // 7
    RS_MNG_COL_LAST_HT_VHT = RS_MNG_COL_MIMO2_SGI,  // 7

    RS_MNG_COL_HE_3_2_SISO_ANT_A,  // 8
    RS_MNG_COL_FIRST_HE = RS_MNG_COL_HE_3_2_SISO_ANT_A,
    RS_MNG_COL_HE_3_2_SISO_ANT_B,
    RS_MNG_COL_HE_1_6_SISO_ANT_A,
    RS_MNG_COL_HE_1_6_SISO_ANT_B,
    RS_MNG_COL_HE_0_8_SISO_ANT_A,
    RS_MNG_COL_HE_0_8_SISO_ANT_B,
    RS_MNG_COL_HE_3_2_MIMO,
    RS_MNG_COL_HE_1_6_MIMO,
    RS_MNG_COL_HE_0_8_MIMO,                       // 16
    RS_MNG_COL_LAST_HE = RS_MNG_COL_HE_0_8_MIMO,  // 16
    RS_MNG_COL_LAST = RS_MNG_COL_LAST_HE,         // 16

    RS_MNG_COL_COUNT = RS_MNG_COL_LAST + 1,  // 17
    RS_MNG_COL_INVALID = RS_MNG_COL_COUNT,   // 17
} RS_MNG_COLUMN_DESC_E;

/***********************************/
typedef U16 TPT_BY_RATE_ARR[RS_MNG_MAX_RATES_NUM];

/**************************************************/

/**************************************************/

typedef enum _RS_MNG_RATE_SETTING_BITMAP_E {
    RS_MNG_RATE_MODE = BIT(0),
    RS_MNG_RATE_MODULATION = BIT(1),
    RS_MNG_RATE_U_IDX = BIT(2),
    RS_MNG_RATE_GI = BIT(3),
    RS_MNG_RATE_BW = BIT(4),
    RS_MNG_RATE_ANT = BIT(5),
    RS_MNG_RATE_STBC = BIT(6),
    RS_MNG_RATE_LDPC = BIT(7),
    RS_MNG_RATE_BFER = BIT(8),

    _RS_MNG_RATE_LAST = BIT(9),
    RS_MNG_RATE_SET_ALL = _RS_MNG_RATE_LAST - 1,

} RS_MNG_RATE_SETTING_BITMAP_E;

typedef struct _RS_MNG_RATE_S {
    union {
        RS_NON_HT_RATES_E nonHt;
        RS_MCS_E mcs;
        U08 idx;
    } idx;
    U16 unset;
    RATE_MCS_API_U rate;
} RS_MNG_RATE_S;

typedef struct _RS_MNG_WIN_STAT_S {
    U32 successRatio;    // per-cent * 128. RS_MNG_INVALID_VAL when invalid
    U32 successCounter;  // number of frames successful
    U32 framesCounter;   // number of frames attempted  //counter
    U32 averageTpt;      // success ratio * expected throughput. RS_MNG_INVALID_VAL when invalid
} RS_MNG_WIN_STAT_S;

typedef struct _RS_MNG_TBL_INFO_S {
    RS_MNG_RATE_S rsMngRate;
    RS_MNG_COLUMN_DESC_E column;
    RS_MNG_WIN_STAT_S win[RS_MNG_MAX_RATES_NUM];  // rates history
} RS_MNG_TBL_INFO_S;

typedef struct _RS_MNG_SEARCH_COL_DATA {
    RS_MNG_RATE_S rsMngRate;
    RS_MNG_COLUMN_DESC_E column;
    RS_MNG_WIN_STAT_S win;
    U32 expectedTpt;
} RS_MNG_SEARCH_COL_DATA;

#define RS_MNG_TPC_NUM_STEPS 5
#define RS_MNG_TPC_INACTIVE RS_MNG_TPC_NUM_STEPS  // TPC algo is running, but currently not
// reducing power
#define RS_MNG_TPC_DISABLED (RS_MNG_TPC_NUM_STEPS + 1)  // TPC is disallowed for some reason
// (e.g. non-optimal rate, disabled by
// debug, amsdus not active long enough etc)
#define RS_MNG_TPC_STEP_SIZE 3  // dB
typedef struct _RS_MNG_TPC_TBL_S {
    RS_MNG_WIN_STAT_S windows[RS_MNG_TPC_NUM_STEPS];
    bool testing;
    U08 currStep;  // index into the window array, or RS_MNG_TPC_<INACTIVE|DISABLED>
} RS_MNG_TPC_TBL_S;
// struct umac_lq_sta
typedef struct _RS_MNG_STA_INFO_S {
    TLC_MNG_CONFIG_PARAMS_CMD_API_S config;
    bool enabled;
    RS_MNG_TBL_INFO_S rateTblInfo;
    RS_MNG_SEARCH_COL_DATA searchColData;  // When trying a new column, holds info on that column
    U08 searchBetterTbl;                   // 1: currently trying alternate mode
    U08 ignoreNextTlcNotif;  // The next notification recieved from lmac is irrelevant.
    // Could happen if aggregations are opened in the middle of a
    // search cycle.
    U08 tryingRateUpscale;           // TRUE if now trying to upscale the rate.
    U32 lastRateUpscaleTimeJiffies;  // system time of last rate upscale attempt.
    U32 totalFramesFailed;           // total failed frames, any/all rates //total_failed
    U32 totalFramesSuccess;
    U16 framesSinceLastRun;  // number of frames sent since the last time rateScalePerform
    // ran.
    U32 lastSearchCycleEndTimeJiffies;  // time since end of last search cycle
    U32 txedFrames;  // number of txed frames while stay in column, before clearing
    // the all the stat windows in the current table.
    U32 visitedColumns;  // bitmask of TX columns that were tested during this search cycle
    U32 searchBw;        // holds a new bandwidth to try before ending a search cycle,
    // or an invalid value if no bandwidth change should be tested.
    RS_MNG_STATE_E rsMngState;
    RS_MNG_COLUMN_DESC_E
    stableColumn;  // id of the column used during the last STAY_IN_COLUMN state

    U16 staBuffSize;  // receipient's reordering buffer size, as received in ADDBA response - min
    // for all TIDs

    bool amsduSupport;
    RS_MNG_TX_AMSDU_SIZE_E amsduEnabledSize;
    U32 trafficLoad;
    U08 amsduBlacklist;
    U32 lastTrafficLoadStatJiffies;
    U32 failSafeCounter;
    bool isUpscaleSearchCycle;  // TRUE if last search cycle started because of passing success
    // frame limit.
    U32 lastEnableJiffies;  // timestamp of the last TX AMSDU enablement

    RATE_MCS_API_U lastNotifiedRate;

    RS_MNG_TPC_TBL_S tpcTable;

    // The params below this line should not be reset when reconfiguring an enabled station
    U08 amsduInAmpdu;  // bitmask of tids with AMSDU in AMPDU supported
    U16 aggDurationLimit;
    U32 fixedRate;  // 0 if not using fixed rate
    bool longAggEnabled;

    struct iwl_mvm* mvm;
    struct ieee80211_sta* sta;
    struct iwl_mvm_sta* mvmsta;
} RS_MNG_STA_INFO_S;

typedef struct _RS_MNG_COL_ELEM_S RS_MNG_COL_ELEM_S;
typedef bool (*ALLOW_COL_FUNC_F)(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                                 const RS_MNG_COL_ELEM_S* nextCol);

struct _RS_MNG_COL_ELEM_S {
    RS_MNG_MODULATION_E mode;
    U08 ant;
    RS_MNG_GI_E gi;
    RS_MNG_COLUMN_DESC_E nextCols[MAX_NEXT_COLUMNS];
    ALLOW_COL_FUNC_F checks[MAX_COLUMN_CHECKS];
};

static INLINE U08 rsMngGetDualAntMsk(void) {
    return TLC_MNG_CHAIN_A_MSK | TLC_MNG_CHAIN_B_MSK;
}

static INLINE U08 _rsMngGetSingleAntMsk(U08 chainsEnabled, uint8_t non_shared_ant, uint8_t valid_tx_ant) {
    BUILD_BUG_ON(TLC_MNG_CHAIN_A_MSK != ANT_A);
    BUILD_BUG_ON(TLC_MNG_CHAIN_B_MSK != ANT_B);
    // Since TLC offload only supports 2 chains, if the non-shared antenna isn't enabled,
    // chainsEnabled must have exactly one chain enabled.
    return (U08)(valid_tx_ant != rsMngGetDualAntMsk()
                     ? valid_tx_ant
                     : (non_shared_ant & chainsEnabled ? non_shared_ant : chainsEnabled));
}

#define rsMngGetSingleAntMsk(chainsEnabled)                                    \
    (_rsMngGetSingleAntMsk((chainsEnabled), staInfo->mvm->cfg->non_shared_ant, \
                           iwl_mvm_get_valid_tx_ant(staInfo->mvm)))

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM__RATESCALEMNG_H_
