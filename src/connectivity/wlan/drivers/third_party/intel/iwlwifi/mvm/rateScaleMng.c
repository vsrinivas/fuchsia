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
#include "_rateScaleMng.h"

#define BOOLEAN bool

#define SHIFT_AND_MASK(val, mask, pos) (((val) >> (pos)) & ((mask) >> (pos)))
#define SEC_TO_USEC(x) ((x)*USEC_PER_SEC)
#define MSEC_TO_USEC(x) ((x)*USEC_PER_MSEC)
#define _memclr(p, s) (memset((p), 0, (s)))
#define DBG_PRINTF(...)
#define NON_SHARED_ANT_RFIC_ID (staInfo->mvm->cfg->non_shared_ant == ANT_A ? 0 : 1)
#define BT_COEX_SHARED_ANT_ID (staInfo->mvm->cfg->non_shared_ant ^ ANT_AB)
#define PWR_IS_SLEEP_ALLOWED (!staInfo->mvm->ps_disabled)

#define MSB2ORD msb2ord
#define LSB2ORD lsb2ord

static inline unsigned long msb2ord(unsigned long x) { return find_last_bit(&x, BITS_PER_LONG); }

static inline unsigned long lsb2ord(unsigned long x) { return find_first_bit(&x, BITS_PER_LONG); }

// TODO - move to coex.c
static bool btCoexManagerIsAntAvailable(struct iwl_mvm* mvm, uint8_t ant) {
  if (mvm->cfg->bt_shared_single_ant) {
    return true;
  }

  if (!(ant & ~mvm->cfg->non_shared_ant)) {
    return true;
  }

  return le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) < BT_HIGH_TRAFFIC;
}

static bool btCoexManagerBtOwnsAnt(struct iwl_mvm* mvm) {
  return le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) >= BT_HIGH_TRAFFIC;
}

typedef struct _TLC_STAT_COMMON_API_S {
  /* @brief number of packets sent
   * txed[0] - rate index 0
   * txed[1] - rate index != 0
   */
  U16 txed[2];
  /**
   * @brief number of packets we got acknowledgment for packet sent
   * acked[0] - rate index 0
   * acked[1] - rate index != 0
   */
  U16 acked[2];
  /* Number of frames we got BA response for (regardless of success) */
  U16 trafficLoad;
  U16 baTxed;
  U16 baAcked;
} TLC_STAT_COMMON_API_S;

static const U08 RS_NON_HT_RATE_TO_API_RATE[] = {
    [RS_NON_HT_RATE_CCK_1M] = R_1M,     [RS_NON_HT_RATE_CCK_2M] = R_2M,
    [RS_NON_HT_RATE_CCK_5_5M] = R_5_5M, [RS_NON_HT_RATE_CCK_11M] = R_11M,
    [RS_NON_HT_RATE_OFDM_6M] = R_6M,    [RS_NON_HT_RATE_OFDM_9M] = R_9M,
    [RS_NON_HT_RATE_OFDM_12M] = R_12M,  [RS_NON_HT_RATE_OFDM_18M] = R_18M,
    [RS_NON_HT_RATE_OFDM_24M] = R_24M,  [RS_NON_HT_RATE_OFDM_36M] = R_36M,
    [RS_NON_HT_RATE_OFDM_48M] = R_48M,  [RS_NON_HT_RATE_OFDM_54M] = R_54M,
};

// This array converts VHT rate configuration to phy rate
// See ieee80211-2016 spec, section 21.4 tabled for reference values
// Note the values are in bps, instead of mbps (i.e. multiplied by 2^20)
// The table access is rsMngVhtRateToBps[BW][MSC].
static const U32 rsMngVhtRateToBps[][10] = {
    [CHANNEL_WIDTH20] = {6815744, 13631488, 20447232, 27262976, 40894464, 54525952, 61341696,
                         68157440, 81788928, 90701824},
    [CHANNEL_WIDTH40] = {14155776, 28311552, 42467328, 56623104, 84934656, 113246208, 127401984,
                         141557760, 169869312, 188743680},
    [CHANNEL_WIDTH80] = {30723277 /*rounded*/, 61341696, 92064973 /*rounded*/, 122683392, 184025088,
                         245366784, 276090061 /*rounded*/, 306708480, 368050176, 408944640},
    [CHANNEL_WIDTH160] = {61341696, 122683392, 184025088, 245366784, 368050176, 490733568,
                          552075264, 613416960, 736100352, 817889280},
};

// The array cell index is the MCS. e.g. - cell 0 - MCS0 6M. etc.
static const U08 downColMcsToLegacy[] = {
    [RS_MCS_0_HE_ER_AND_DCM] = RS_NON_HT_RATE_OFDM_6M,
    [RS_MCS_0] = RS_NON_HT_RATE_OFDM_6M,
    [RS_MCS_1] = RS_NON_HT_RATE_OFDM_12M,
    [RS_MCS_2] = RS_NON_HT_RATE_OFDM_18M,
    [RS_MCS_3] = RS_NON_HT_RATE_OFDM_24M,
    [RS_MCS_4] = RS_NON_HT_RATE_OFDM_36M,
    [RS_MCS_5] = RS_NON_HT_RATE_OFDM_48M,
    [RS_MCS_6] = RS_NON_HT_RATE_OFDM_54M,
    [RS_MCS_7] = RS_NON_HT_RATE_OFDM_54M,
    [RS_MCS_8] = RS_NON_HT_RATE_OFDM_54M,
    [RS_MCS_9] = RS_NON_HT_RATE_OFDM_54M,
    [RS_MCS_10] = RS_NON_HT_RATE_OFDM_54M,
    [RS_MCS_11] = RS_NON_HT_RATE_OFDM_54M,
};

typedef struct _RS_MNG_DYN_BW_STAY {
  RS_MCS_E lowestStayMcs;
  RS_MCS_E highestStayMcs;
} RS_MNG_DYN_BW_STAY;

#define RS_MNG_DYN_BW_STAY_MCS(_bw, _nss1min, _nss1max, _nss2min, _nss2max)              \
  [CHANNEL_WIDTH##                                                                       \
      _bw] = {{.lowestStayMcs = RS_MCS_##_nss1min, .highestStayMcs = RS_MCS_##_nss1max}, \
              {.lowestStayMcs = RS_MCS_##_nss2min, .highestStayMcs = RS_MCS_##_nss2max}}

// thresholds above/below which bandwidth will be increase/decreased
static RS_MNG_DYN_BW_STAY g_rsMngDynBwStayMcs[][2] = {
    RS_MNG_DYN_BW_STAY_MCS(20, 0, 1, 0, 1), RS_MNG_DYN_BW_STAY_MCS(40, 2, 2, 2, 2),
    RS_MNG_DYN_BW_STAY_MCS(80, 5, 8, 4, 7), RS_MNG_DYN_BW_STAY_MCS(160, 7, 9, 6, 9)};

/***********************************************************************/
/*
 * The following tables contain the expected throughput metrics for all rates
 *
 * 1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54, 60 MBits
 *
 * where invalid entries are zeros.
 *
 * CCK rates are only valid in legacy table and will only be used in G
 * (2.4 GHz) band.
 */

static const TPT_BY_RATE_ARR expectedTptNonHt = {
    [RS_NON_HT_RATE_CCK_1M] = 7,     [RS_NON_HT_RATE_CCK_2M] = 12,
    [RS_NON_HT_RATE_CCK_5_5M] = 33,  [RS_NON_HT_RATE_CCK_11M] = 54,
    [RS_NON_HT_RATE_OFDM_6M] = 38,   [RS_NON_HT_RATE_OFDM_9M] = 58,
    [RS_NON_HT_RATE_OFDM_12M] = 76,  [RS_NON_HT_RATE_OFDM_18M] = 108,
    [RS_NON_HT_RATE_OFDM_24M] = 135, [RS_NON_HT_RATE_OFDM_36M] = 176,
    [RS_NON_HT_RATE_OFDM_48M] = 208, [RS_NON_HT_RATE_OFDM_54M] = 221,
};

static const TPT_BY_RATE_ARR expectedTptHtVht[2][MAX_CHANNEL_BW_INDX][2][2] = {
    [RS_MNG_NO_AGG] = {
        [CHANNEL_WIDTH20] = {
            [RS_MNG_NGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 49, [RS_MCS_1] = 86, [RS_MCS_2] = 115, [RS_MCS_3] = 140,
                    [RS_MCS_4] = 178, [RS_MCS_5] = 204, [RS_MCS_6] = 215, [RS_MCS_7] = 223,
                    [RS_MCS_8] = 240,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 84, [RS_MCS_1] = 136, [RS_MCS_2] = 173, [RS_MCS_3] = 200,
                    [RS_MCS_4] = 238, [RS_MCS_5] = 260, [RS_MCS_6] = 267, [RS_MCS_7] = 275,
                    [RS_MCS_8] = 289,
                },
            },
            [RS_MNG_SGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 53, [RS_MCS_1] = 93, [RS_MCS_2] = 124, [RS_MCS_3] = 149,
                    [RS_MCS_4] = 188, [RS_MCS_5] = 214, [RS_MCS_6] = 225, [RS_MCS_7] = 233,
                    [RS_MCS_8] = 249,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 91, [RS_MCS_1] = 144, [RS_MCS_2] = 182, [RS_MCS_3] = 210,
                    [RS_MCS_4] = 246, [RS_MCS_5] = 267, [RS_MCS_6] = 274, [RS_MCS_7] = 282,
                    [RS_MCS_8] = 295,
                },
            },
        },
        [CHANNEL_WIDTH40] = {
            [RS_MNG_NGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 87, [RS_MCS_1] = 140, [RS_MCS_2] = 177, [RS_MCS_3] = 207,
                    [RS_MCS_4] = 245, [RS_MCS_5] = 265, [RS_MCS_6] = 273, [RS_MCS_7] = 281,
                    [RS_MCS_8] = 296, [RS_MCS_9] = 299,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 135, [RS_MCS_1] = 197, [RS_MCS_2] = 234, [RS_MCS_3] = 259,
                    [RS_MCS_4] = 292, [RS_MCS_5] = 304, [RS_MCS_6] = 311, [RS_MCS_7] = 314,
                    [RS_MCS_8] = 321, [RS_MCS_9] = 331,
                },
            },
            [RS_MNG_SGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 94, [RS_MCS_1] = 149, [RS_MCS_2] = 187, [RS_MCS_3] = 216,
                    [RS_MCS_4] = 253, [RS_MCS_5] = 273, [RS_MCS_6] = 280, [RS_MCS_7] = 288,
                    [RS_MCS_8] = 302, [RS_MCS_9] = 305,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 144, [RS_MCS_1] = 205, [RS_MCS_2] = 242, [RS_MCS_3] = 266,
                    [RS_MCS_4] = 297, [RS_MCS_5] = 309, [RS_MCS_6] = 315, [RS_MCS_7] = 318,
                    [RS_MCS_8] = 325, [RS_MCS_9] = 335,
                },
            },
        },
        [CHANNEL_WIDTH80] = {
            [RS_MNG_NGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 142, [RS_MCS_1] = 205, [RS_MCS_2] = 240, [RS_MCS_3] = 267,
                    [RS_MCS_4] = 299, [RS_MCS_5] = 312, [RS_MCS_6] = 319, [RS_MCS_7] = 323,
                    [RS_MCS_8] = 330, [RS_MCS_9] = 334,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 195, [RS_MCS_1] = 251, [RS_MCS_2] = 283, [RS_MCS_3] = 303,
                    [RS_MCS_4] = 325, [RS_MCS_5] = 333, [RS_MCS_6] = 337, [RS_MCS_7] = 337,
                    [RS_MCS_8] = 341, [RS_MCS_9] = 345,
                },
            },
            [RS_MNG_SGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 151, [RS_MCS_1] = 213, [RS_MCS_2] = 248, [RS_MCS_3] = 274,
                    [RS_MCS_4] = 305, [RS_MCS_5] = 317, [RS_MCS_6] = 323, [RS_MCS_7] = 327,
                    [RS_MCS_8] = 334, [RS_MCS_9] = 337,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 203, [RS_MCS_1] = 258, [RS_MCS_2] = 288, [RS_MCS_3] = 308,
                    [RS_MCS_4] = 328, [RS_MCS_5] = 335, [RS_MCS_6] = 339, [RS_MCS_7] = 339,
                    [RS_MCS_8] = 343, [RS_MCS_9] = 346,
                },
            },
        },
        [CHANNEL_WIDTH160] = {
            [RS_MNG_NGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 197, [RS_MCS_1] = 254, [RS_MCS_2] = 287, [RS_MCS_3] = 307,
                    [RS_MCS_4] = 330, [RS_MCS_5] = 338, [RS_MCS_6] = 342, [RS_MCS_7] = 342,
                    [RS_MCS_8] = 346, [RS_MCS_9] = 350,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 239, [RS_MCS_1] = 286, [RS_MCS_2] = 311, [RS_MCS_3] = 327,
                    [RS_MCS_4] = 341, [RS_MCS_5] = 345, [RS_MCS_6] = 349, [RS_MCS_7] = 349,
                    [RS_MCS_8] = 349, [RS_MCS_9] = 353,
                },
            },
            [RS_MNG_SGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 205, [RS_MCS_1] = 261, [RS_MCS_2] = 292, [RS_MCS_3] = 312,
                    [RS_MCS_4] = 334, [RS_MCS_5] = 341, [RS_MCS_6] = 345, [RS_MCS_7] = 345,
                    [RS_MCS_8] = 348, [RS_MCS_9] = 352,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 245, [RS_MCS_1] = 290, [RS_MCS_2] = 314, [RS_MCS_3] = 330,
                    [RS_MCS_4] = 343, [RS_MCS_5] = 346, [RS_MCS_6] = 350, [RS_MCS_7] = 350,
                    [RS_MCS_8] = 350, [RS_MCS_9] = 354,
                },
            },
        },
    },
    [RS_MNG_AGG] = {
        [CHANNEL_WIDTH20] = {
            [RS_MNG_NGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 53, [RS_MCS_1] = 106, [RS_MCS_2] = 159, [RS_MCS_3] = 213,
                    [RS_MCS_4] = 319, [RS_MCS_5] = 426, [RS_MCS_6] = 481, [RS_MCS_7] = 534,
                    [RS_MCS_8] = 641,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 105, [RS_MCS_1] = 211, [RS_MCS_2] = 318, [RS_MCS_3] = 425,
                    [RS_MCS_4] = 640, [RS_MCS_5] = 846, [RS_MCS_6] = 951, [RS_MCS_7] = 1060,
                    [RS_MCS_8] = 1266,
                },
            },
            [RS_MNG_SGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 58, [RS_MCS_1] = 116, [RS_MCS_2] = 177, [RS_MCS_3] = 237,
                    [RS_MCS_4] = 356, [RS_MCS_5] = 474, [RS_MCS_6] = 534, [RS_MCS_7] = 593,
                    [RS_MCS_8] = 712,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 116, [RS_MCS_1] = 235, [RS_MCS_2] = 354, [RS_MCS_3] = 472,
                    [RS_MCS_4] = 712, [RS_MCS_5] = 942, [RS_MCS_6] = 1058, [RS_MCS_7] = 1175,
                    [RS_MCS_8] = 1401,
                },
            },
        },
        [CHANNEL_WIDTH40] = {
            [RS_MNG_NGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 109, [RS_MCS_1] = 219, [RS_MCS_2] = 331, [RS_MCS_3] = 442,
                    [RS_MCS_4] = 665, [RS_MCS_5] = 882, [RS_MCS_6] = 991, [RS_MCS_7] = 1103,
                    [RS_MCS_8] = 1316, [RS_MCS_9] = 1450,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 222, [RS_MCS_1] = 445, [RS_MCS_2] = 669, [RS_MCS_3] = 892,
                    [RS_MCS_4] = 1334, [RS_MCS_5] = 1725, [RS_MCS_6] = 1907, [RS_MCS_7] = 2085,
                    [RS_MCS_8] = 2422, [RS_MCS_9] = 2637,
                },
            },
            [RS_MNG_SGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 120, [RS_MCS_1] = 244, [RS_MCS_2] = 368, [RS_MCS_3] = 492,
                    [RS_MCS_4] = 740, [RS_MCS_5] = 978, [RS_MCS_6] = 1101, [RS_MCS_7] = 1222,
                    [RS_MCS_8] = 1456, [RS_MCS_9] = 1601,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 246, [RS_MCS_1] = 494, [RS_MCS_2] = 742, [RS_MCS_3] = 991,
                    [RS_MCS_4] = 1470, [RS_MCS_5] = 1888, [RS_MCS_6] = 2085, [RS_MCS_7] = 2276,
                    [RS_MCS_8] = 2636, [RS_MCS_9] = 2865,
                },
            },
        },
        [CHANNEL_WIDTH80] = {
            [RS_MNG_NGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 237, [RS_MCS_1] = 478, [RS_MCS_2] = 718, [RS_MCS_3] = 959,
                    [RS_MCS_4] = 1427, [RS_MCS_5] = 1849, [RS_MCS_6] = 2043, [RS_MCS_7] = 2232,
                    [RS_MCS_8] = 2589, [RS_MCS_9] = 2813,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 475, [RS_MCS_1] = 953, [RS_MCS_2] = 1420, [RS_MCS_3] = 1843,
                    [RS_MCS_4] = 2584, [RS_MCS_5] = 3229, [RS_MCS_6] = 3522, [RS_MCS_7] = 3799,
                    [RS_MCS_8] = 4303, [RS_MCS_9] = 4603,
                },
            },
            [RS_MNG_SGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 263, [RS_MCS_1] = 531, [RS_MCS_2] = 797, [RS_MCS_3] = 1065,
                    [RS_MCS_4] = 1577, [RS_MCS_5] = 2022, [RS_MCS_6] = 2231, [RS_MCS_7] = 2434,
                    [RS_MCS_8] = 2814, [RS_MCS_9] = 3052,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 528, [RS_MCS_1] = 1058, [RS_MCS_2] = 1569, [RS_MCS_3] = 2016,
                    [RS_MCS_4] = 2808, [RS_MCS_5] = 3490, [RS_MCS_6] = 3798, [RS_MCS_7] = 4087,
                    [RS_MCS_8] = 4610, [RS_MCS_9] = 4919,
                },
            },
        },
        [CHANNEL_WIDTH160] = {
            [RS_MNG_NGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 476, [RS_MCS_1] = 954, [RS_MCS_2] = 1422, [RS_MCS_3] = 1846,
                    [RS_MCS_4] = 2589, [RS_MCS_5] = 3237, [RS_MCS_6] = 3531, [RS_MCS_7] = 3810,
                    [RS_MCS_8] = 4317, [RS_MCS_9] = 4619,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 948, [RS_MCS_1] = 1833, [RS_MCS_2] = 2569, [RS_MCS_3] = 3221,
                    [RS_MCS_4] = 4303, [RS_MCS_5] = 5162, [RS_MCS_6] = 5530, [RS_MCS_7] = 5856,
                    [RS_MCS_8] = 6449, [RS_MCS_9] = 6767,
                },
            },
            [RS_MNG_SGI] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 529, [RS_MCS_1] = 1060, [RS_MCS_2] = 1571, [RS_MCS_3] = 2019,
                    [RS_MCS_4] = 2814, [RS_MCS_5] = 3499, [RS_MCS_6] = 3808, [RS_MCS_7] = 4100,
                    [RS_MCS_8] = 4626, [RS_MCS_9] = 4937,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 1053, [RS_MCS_1] = 2004, [RS_MCS_2] = 2790, [RS_MCS_3] = 3481,
                    [RS_MCS_4] = 4610, [RS_MCS_5] = 5491, [RS_MCS_6] = 5864, [RS_MCS_7] = 6194,
                    [RS_MCS_8] = 6787, [RS_MCS_9] = 7103,
                },
            },
        },
    },
};

static const TPT_BY_RATE_ARR expectedTptHe[2][MAX_CHANNEL_BW_INDX][3][2] = {
    [RS_MNG_NO_AGG] = {
        [CHANNEL_WIDTH20] = {
            [RS_MNG_GI_3_2] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 30, [RS_MCS_1] = 61, [RS_MCS_2] = 92, [RS_MCS_3] = 122,
                    [RS_MCS_4] = 184, [RS_MCS_5] = 245, [RS_MCS_6] = 276, [RS_MCS_7] = 307,
                    [RS_MCS_8] = 368, [RS_MCS_9] = 409, [RS_MCS_10] = 460, [RS_MCS_11] = 511,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 61, [RS_MCS_1] = 122, [RS_MCS_2] = 184, [RS_MCS_3] = 245,
                    [RS_MCS_4] = 368, [RS_MCS_5] = 491, [RS_MCS_6] = 552, [RS_MCS_7] = 614,
                    [RS_MCS_8] = 737, [RS_MCS_9] = 819, [RS_MCS_10] = 921, [RS_MCS_11] = 1023,
                },
            },
            [RS_MNG_GI_1_6] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 34, [RS_MCS_1] = 68, [RS_MCS_2] = 102, [RS_MCS_3] = 136,
                    [RS_MCS_4] = 204, [RS_MCS_5] = 273, [RS_MCS_6] = 307, [RS_MCS_7] = 341,
                    [RS_MCS_8] = 409, [RS_MCS_9] = 455, [RS_MCS_10] = 511, [RS_MCS_11] = 568,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 68, [RS_MCS_1] = 136, [RS_MCS_2] = 204, [RS_MCS_3] = 273,
                    [RS_MCS_4] = 409, [RS_MCS_5] = 546, [RS_MCS_6] = 614, [RS_MCS_7] = 682,
                    [RS_MCS_8] = 819, [RS_MCS_9] = 910, [RS_MCS_10] = 1023, [RS_MCS_11] = 1137,
                },
            },
            [RS_MNG_GI_0_8] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 36, [RS_MCS_1] = 72, [RS_MCS_2] = 108, [RS_MCS_3] = 144,
                    [RS_MCS_4] = 216, [RS_MCS_5] = 289, [RS_MCS_6] = 325, [RS_MCS_7] = 361,
                    [RS_MCS_8] = 433, [RS_MCS_9] = 481, [RS_MCS_10] = 541, [RS_MCS_11] = 602,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 72, [RS_MCS_1] = 144, [RS_MCS_2] = 216, [RS_MCS_3] = 289,
                    [RS_MCS_4] = 433, [RS_MCS_5] = 578, [RS_MCS_6] = 650, [RS_MCS_7] = 722,
                    [RS_MCS_8] = 867, [RS_MCS_9] = 963, [RS_MCS_10] = 1083, [RS_MCS_11] = 1204,
                },
            },
        },
        [CHANNEL_WIDTH40] = {
            [RS_MNG_GI_3_2] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 61, [RS_MCS_1] = 122, [RS_MCS_2] = 184, [RS_MCS_3] = 245,
                    [RS_MCS_4] = 368, [RS_MCS_5] = 491, [RS_MCS_6] = 552, [RS_MCS_7] = 614,
                    [RS_MCS_8] = 737, [RS_MCS_9] = 819, [RS_MCS_10] = 921, [RS_MCS_11] = 1023,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 122, [RS_MCS_1] = 245, [RS_MCS_2] = 368, [RS_MCS_3] = 491,
                    [RS_MCS_4] = 737, [RS_MCS_5] = 982, [RS_MCS_6] = 1105, [RS_MCS_7] = 1228,
                    [RS_MCS_8] = 1474, [RS_MCS_9] = 1638, [RS_MCS_10] = 1842, [RS_MCS_11] = 2047,
                },
            },
            [RS_MNG_GI_1_6] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 68, [RS_MCS_1] = 136, [RS_MCS_2] = 204, [RS_MCS_3] = 273,
                    [RS_MCS_4] = 409, [RS_MCS_5] = 546, [RS_MCS_6] = 614, [RS_MCS_7] = 682,
                    [RS_MCS_8] = 819, [RS_MCS_9] = 910, [RS_MCS_10] = 1023, [RS_MCS_11] = 1137,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 136, [RS_MCS_1] = 273, [RS_MCS_2] = 409, [RS_MCS_3] = 546,
                    [RS_MCS_4] = 819, [RS_MCS_5] = 1092, [RS_MCS_6] = 1228, [RS_MCS_7] = 1365,
                    [RS_MCS_8] = 1638, [RS_MCS_9] = 1820, [RS_MCS_10] = 2047, [RS_MCS_11] = 2275,
                },
            },
            [RS_MNG_GI_0_8] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 72, [RS_MCS_1] = 144, [RS_MCS_2] = 216, [RS_MCS_3] = 289,
                    [RS_MCS_4] = 433, [RS_MCS_5] = 578, [RS_MCS_6] = 650, [RS_MCS_7] = 722,
                    [RS_MCS_8] = 867, [RS_MCS_9] = 963, [RS_MCS_10] = 1083, [RS_MCS_11] = 1204,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 144, [RS_MCS_1] = 289, [RS_MCS_2] = 433, [RS_MCS_3] = 578,
                    [RS_MCS_4] = 867, [RS_MCS_5] = 1156, [RS_MCS_6] = 1300, [RS_MCS_7] = 1445,
                    [RS_MCS_8] = 1734, [RS_MCS_9] = 1927, [RS_MCS_10] = 2167, [RS_MCS_11] = 2408,
                },
            },
        },
        [CHANNEL_WIDTH80] = {
            [RS_MNG_GI_3_2] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 128, [RS_MCS_1] = 257, [RS_MCS_2] = 385, [RS_MCS_3] = 514,
                    [RS_MCS_4] = 771, [RS_MCS_5] = 1029, [RS_MCS_6] = 1157, [RS_MCS_7] = 1286,
                    [RS_MCS_8] = 1543, [RS_MCS_9] = 1715, [RS_MCS_10] = 1929, [RS_MCS_11] = 2143,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 257, [RS_MCS_1] = 514, [RS_MCS_2] = 771, [RS_MCS_3] = 1029,
                    [RS_MCS_4] = 1543, [RS_MCS_5] = 2058, [RS_MCS_6] = 2315, [RS_MCS_7] = 2572,
                    [RS_MCS_8] = 3087, [RS_MCS_9] = 3430, [RS_MCS_10] = 3858, [RS_MCS_11] = 4287,
                },
            },
            [RS_MNG_GI_1_6] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 142, [RS_MCS_1] = 285, [RS_MCS_2] = 428, [RS_MCS_3] = 571,
                    [RS_MCS_4] = 857, [RS_MCS_5] = 1143, [RS_MCS_6] = 1286, [RS_MCS_7] = 1429,
                    [RS_MCS_8] = 1715, [RS_MCS_9] = 1905, [RS_MCS_10] = 2143, [RS_MCS_11] = 2381,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 285, [RS_MCS_1] = 571, [RS_MCS_2] = 857, [RS_MCS_3] = 1143,
                    [RS_MCS_4] = 1715, [RS_MCS_5] = 2286, [RS_MCS_6] = 2572, [RS_MCS_7] = 2858,
                    [RS_MCS_8] = 3430, [RS_MCS_9] = 3811, [RS_MCS_10] = 4287, [RS_MCS_11] = 4763,
                },
            },
            [RS_MNG_GI_0_8] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 151, [RS_MCS_1] = 302, [RS_MCS_2] = 453, [RS_MCS_3] = 605,
                    [RS_MCS_4] = 907, [RS_MCS_5] = 1210, [RS_MCS_6] = 1361, [RS_MCS_7] = 1513,
                    [RS_MCS_8] = 1815, [RS_MCS_9] = 2017, [RS_MCS_10] = 2269, [RS_MCS_11] = 2522,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 302, [RS_MCS_1] = 605, [RS_MCS_2] = 907, [RS_MCS_3] = 1210,
                    [RS_MCS_4] = 1815, [RS_MCS_5] = 2421, [RS_MCS_6] = 2723, [RS_MCS_7] = 3026,
                    [RS_MCS_8] = 3631, [RS_MCS_9] = 4035, [RS_MCS_10] = 4539, [RS_MCS_11] = 5044,
                },
            },
        },
        [CHANNEL_WIDTH160] = {
            [RS_MNG_GI_3_2] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 257, [RS_MCS_1] = 514, [RS_MCS_2] = 771, [RS_MCS_3] = 1029,
                    [RS_MCS_4] = 1543, [RS_MCS_5] = 2058, [RS_MCS_6] = 2315, [RS_MCS_7] = 2572,
                    [RS_MCS_8] = 3087, [RS_MCS_9] = 3430, [RS_MCS_10] = 3858, [RS_MCS_11] = 4287,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 514, [RS_MCS_1] = 1029, [RS_MCS_2] = 1543, [RS_MCS_3] = 2058,
                    [RS_MCS_4] = 3087, [RS_MCS_5] = 4116, [RS_MCS_6] = 4630, [RS_MCS_7] = 5145,
                    [RS_MCS_8] = 6174, [RS_MCS_9] = 6860, [RS_MCS_10] = 7717, [RS_MCS_11] = 8575,
                },
            },
            [RS_MNG_GI_1_6] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 285, [RS_MCS_1] = 571, [RS_MCS_2] = 857, [RS_MCS_3] = 1143,
                    [RS_MCS_4] = 1715, [RS_MCS_5] = 2286, [RS_MCS_6] = 2572, [RS_MCS_7] = 2858,
                    [RS_MCS_8] = 3430, [RS_MCS_9] = 3811, [RS_MCS_10] = 4287, [RS_MCS_11] = 4763,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 571, [RS_MCS_1] = 1143, [RS_MCS_2] = 1715, [RS_MCS_3] = 2286,
                    [RS_MCS_4] = 3430, [RS_MCS_5] = 4573, [RS_MCS_6] = 5145, [RS_MCS_7] = 5716,
                    [RS_MCS_8] = 6860, [RS_MCS_9] = 7622, [RS_MCS_10] = 8575, [RS_MCS_11] = 9527,
                },
            },
            [RS_MNG_GI_0_8] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 302, [RS_MCS_1] = 605, [RS_MCS_2] = 907, [RS_MCS_3] = 1210,
                    [RS_MCS_4] = 1815, [RS_MCS_5] = 2421, [RS_MCS_6] = 2723, [RS_MCS_7] = 3026,
                    [RS_MCS_8] = 3631, [RS_MCS_9] = 4035, [RS_MCS_10] = 4539, [RS_MCS_11] = 5044,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 605, [RS_MCS_1] = 1210, [RS_MCS_2] = 1815, [RS_MCS_3] = 2421,
                    [RS_MCS_4] = 3631, [RS_MCS_5] = 4842, [RS_MCS_6] = 5447, [RS_MCS_7] = 6052,
                    [RS_MCS_8] = 7263, [RS_MCS_9] = 8070, [RS_MCS_10] = 9079, [RS_MCS_11] = 10088,
                },
            },
        },
    },
    [RS_MNG_AGG] = {
        [CHANNEL_WIDTH20] = {
            [RS_MNG_GI_3_2] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 62, [RS_MCS_1] = 124, [RS_MCS_2] = 186, [RS_MCS_3] = 248,
                    [RS_MCS_4] = 372, [RS_MCS_5] = 497, [RS_MCS_6] = 559, [RS_MCS_7] = 621,
                    [RS_MCS_8] = 745, [RS_MCS_9] = 828, [RS_MCS_10] = 932, [RS_MCS_11] = 1035,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 124, [RS_MCS_1] = 248, [RS_MCS_2] = 372, [RS_MCS_3] = 497,
                    [RS_MCS_4] = 745, [RS_MCS_5] = 994, [RS_MCS_6] = 1118, [RS_MCS_7] = 1243,
                    [RS_MCS_8] = 1491, [RS_MCS_9] = 1657, [RS_MCS_10] = 1864, [RS_MCS_11] = 2071,
                },
            },
            [RS_MNG_GI_1_6] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 69, [RS_MCS_1] = 138, [RS_MCS_2] = 207, [RS_MCS_3] = 276,
                    [RS_MCS_4] = 414, [RS_MCS_5] = 552, [RS_MCS_6] = 621, [RS_MCS_7] = 690,
                    [RS_MCS_8] = 828, [RS_MCS_9] = 920, [RS_MCS_10] = 1035, [RS_MCS_11] = 1151,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 138, [RS_MCS_1] = 276, [RS_MCS_2] = 414, [RS_MCS_3] = 552,
                    [RS_MCS_4] = 828, [RS_MCS_5] = 1105, [RS_MCS_6] = 1243, [RS_MCS_7] = 1381,
                    [RS_MCS_8] = 1657, [RS_MCS_9] = 1841, [RS_MCS_10] = 2071, [RS_MCS_11] = 2302,
                },
            },
            [RS_MNG_GI_0_8] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 73, [RS_MCS_1] = 146, [RS_MCS_2] = 219, [RS_MCS_3] = 292,
                    [RS_MCS_4] = 438, [RS_MCS_5] = 585, [RS_MCS_6] = 658, [RS_MCS_7] = 731,
                    [RS_MCS_8] = 877, [RS_MCS_9] = 975, [RS_MCS_10] = 1096, [RS_MCS_11] = 1218,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 146, [RS_MCS_1] = 292, [RS_MCS_2] = 438, [RS_MCS_3] = 585,
                    [RS_MCS_4] = 877, [RS_MCS_5] = 1170, [RS_MCS_6] = 1316, [RS_MCS_7] = 1462,
                    [RS_MCS_8] = 1755, [RS_MCS_9] = 1950, [RS_MCS_10] = 2193, [RS_MCS_11] = 2437,
                },
            },
        },
        [CHANNEL_WIDTH40] = {
            [RS_MNG_GI_3_2] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 124, [RS_MCS_1] = 248, [RS_MCS_2] = 372, [RS_MCS_3] = 497,
                    [RS_MCS_4] = 745, [RS_MCS_5] = 994, [RS_MCS_6] = 1118, [RS_MCS_7] = 1243,
                    [RS_MCS_8] = 1491, [RS_MCS_9] = 1657, [RS_MCS_10] = 1864, [RS_MCS_11] = 2071,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 248, [RS_MCS_1] = 497, [RS_MCS_2] = 745, [RS_MCS_3] = 994,
                    [RS_MCS_4] = 1491, [RS_MCS_5] = 1989, [RS_MCS_6] = 2237, [RS_MCS_7] = 2486,
                    [RS_MCS_8] = 2983, [RS_MCS_9] = 3315, [RS_MCS_10] = 3729, [RS_MCS_11] = 4143,
                },
            },
            [RS_MNG_GI_1_6] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 138, [RS_MCS_1] = 276, [RS_MCS_2] = 414, [RS_MCS_3] = 552,
                    [RS_MCS_4] = 828, [RS_MCS_5] = 1105, [RS_MCS_6] = 1243, [RS_MCS_7] = 1381,
                    [RS_MCS_8] = 1657, [RS_MCS_9] = 1841, [RS_MCS_10] = 2071, [RS_MCS_11] = 2302,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 276, [RS_MCS_1] = 552, [RS_MCS_2] = 828, [RS_MCS_3] = 1105,
                    [RS_MCS_4] = 1657, [RS_MCS_5] = 2210, [RS_MCS_6] = 2486, [RS_MCS_7] = 2762,
                    [RS_MCS_8] = 3315, [RS_MCS_9] = 3683, [RS_MCS_10] = 4143, [RS_MCS_11] = 4604,
                },
            },
            [RS_MNG_GI_0_8] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 146, [RS_MCS_1] = 292, [RS_MCS_2] = 438, [RS_MCS_3] = 585,
                    [RS_MCS_4] = 877, [RS_MCS_5] = 1170, [RS_MCS_6] = 1316, [RS_MCS_7] = 1462,
                    [RS_MCS_8] = 1755, [RS_MCS_9] = 1950, [RS_MCS_10] = 2193, [RS_MCS_11] = 2437,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 292, [RS_MCS_1] = 585, [RS_MCS_2] = 877, [RS_MCS_3] = 1170,
                    [RS_MCS_4] = 1755, [RS_MCS_5] = 2340, [RS_MCS_6] = 2632, [RS_MCS_7] = 2925,
                    [RS_MCS_8] = 3510, [RS_MCS_9] = 3900, [RS_MCS_10] = 4387, [RS_MCS_11] = 4875,
                },
            },
        },
        [CHANNEL_WIDTH80] = {
            [RS_MNG_GI_3_2] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 260, [RS_MCS_1] = 520, [RS_MCS_2] = 780, [RS_MCS_3] = 1041,
                    [RS_MCS_4] = 1561, [RS_MCS_5] = 2082, [RS_MCS_6] = 2342, [RS_MCS_7] = 2603,
                    [RS_MCS_8] = 3123, [RS_MCS_9] = 3470, [RS_MCS_10] = 3904, [RS_MCS_11] = 4338,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 520, [RS_MCS_1] = 1041, [RS_MCS_2] = 1561, [RS_MCS_3] = 2082,
                    [RS_MCS_4] = 3123, [RS_MCS_5] = 4165, [RS_MCS_6] = 4685, [RS_MCS_7] = 5206,
                    [RS_MCS_8] = 6247, [RS_MCS_9] = 6941, [RS_MCS_10] = 7809, [RS_MCS_11] = 8677,
                },
            },
            [RS_MNG_GI_1_6] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 289, [RS_MCS_1] = 578, [RS_MCS_2] = 867, [RS_MCS_3] = 1156,
                    [RS_MCS_4] = 1735, [RS_MCS_5] = 2313, [RS_MCS_6] = 2603, [RS_MCS_7] = 2892,
                    [RS_MCS_8] = 3470, [RS_MCS_9] = 3856, [RS_MCS_10] = 4338, [RS_MCS_11] = 4820,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 578, [RS_MCS_1] = 1156, [RS_MCS_2] = 1735, [RS_MCS_3] = 2313,
                    [RS_MCS_4] = 3470, [RS_MCS_5] = 4627, [RS_MCS_6] = 5206, [RS_MCS_7] = 5784,
                    [RS_MCS_8] = 6941, [RS_MCS_9] = 7712, [RS_MCS_10] = 8677, [RS_MCS_11] = 9641,
                },
            },
            [RS_MNG_GI_0_8] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 306, [RS_MCS_1] = 612, [RS_MCS_2] = 918, [RS_MCS_3] = 1225,
                    [RS_MCS_4] = 1837, [RS_MCS_5] = 2450, [RS_MCS_6] = 2756, [RS_MCS_7] = 3062,
                    [RS_MCS_8] = 3675, [RS_MCS_9] = 4083, [RS_MCS_10] = 4593, [RS_MCS_11] = 5104,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 612, [RS_MCS_1] = 1225, [RS_MCS_2] = 1837, [RS_MCS_3] = 2450,
                    [RS_MCS_4] = 3675, [RS_MCS_5] = 4900, [RS_MCS_6] = 5512, [RS_MCS_7] = 6125,
                    [RS_MCS_8] = 7350, [RS_MCS_9] = 8166, [RS_MCS_10] = 9187, [RS_MCS_11] = 10208,
                },
            },
        },
        [CHANNEL_WIDTH160] = {
            [RS_MNG_GI_3_2] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 520, [RS_MCS_1] = 1041, [RS_MCS_2] = 1561, [RS_MCS_3] = 2082,
                    [RS_MCS_4] = 3123, [RS_MCS_5] = 4165, [RS_MCS_6] = 4685, [RS_MCS_7] = 5206,
                    [RS_MCS_8] = 6247, [RS_MCS_9] = 6941, [RS_MCS_10] = 7809, [RS_MCS_11] = 8677,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 1041, [RS_MCS_1] = 2082, [RS_MCS_2] = 3123, [RS_MCS_3] = 4165,
                    [RS_MCS_4] = 6247, [RS_MCS_5] = 8330, [RS_MCS_6] = 9371, [RS_MCS_7] = 10412,
                    [RS_MCS_8] = 12495, [RS_MCS_9] = 13883, [RS_MCS_10] = 15618, [RS_MCS_11] = 17354,
                },
            },
            [RS_MNG_GI_1_6] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 578, [RS_MCS_1] = 1156, [RS_MCS_2] = 1735, [RS_MCS_3] = 2313,
                    [RS_MCS_4] = 3470, [RS_MCS_5] = 4627, [RS_MCS_6] = 5206, [RS_MCS_7] = 5784,
                    [RS_MCS_8] = 6941, [RS_MCS_9] = 7712, [RS_MCS_10] = 8677, [RS_MCS_11] = 9641,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 1156, [RS_MCS_1] = 2313, [RS_MCS_2] = 3470, [RS_MCS_3] = 4627,
                    [RS_MCS_4] = 6941, [RS_MCS_5] = 9255, [RS_MCS_6] = 10412, [RS_MCS_7] = 11569,
                    [RS_MCS_8] = 13883, [RS_MCS_9] = 15425, [RS_MCS_10] = 17354, [RS_MCS_11] = 19282,
                },
            },
            [RS_MNG_GI_0_8] = {
                [RS_MNG_SISO] = {
                    [RS_MCS_0] = 612, [RS_MCS_1] = 1225, [RS_MCS_2] = 1837, [RS_MCS_3] = 2450,
                    [RS_MCS_4] = 3675, [RS_MCS_5] = 4900, [RS_MCS_6] = 5512, [RS_MCS_7] = 6125,
                    [RS_MCS_8] = 7350, [RS_MCS_9] = 8166, [RS_MCS_10] = 9187, [RS_MCS_11] = 10208,
                },
                [RS_MNG_MIMO] = {
                    [RS_MCS_0] = 1225, [RS_MCS_1] = 2450, [RS_MCS_2] = 3675, [RS_MCS_3] = 4900,
                    [RS_MCS_4] = 7350, [RS_MCS_5] = 9800, [RS_MCS_6] = 11025, [RS_MCS_7] = 12250,
                    [RS_MCS_8] = 14700, [RS_MCS_9] = 16333, [RS_MCS_10] = 18375, [RS_MCS_11] = 20416,
                },
            },
        },
    },
};

/*******************************************************************************/

/************************************************************************************/
/************************       Aggergation          ********************************/
/************************************************************************************/
// Default values for Aggregation Params
// info about the relevant fields can be found for LINK_QUAL_AGG_PARAMS_API_S
#define RS_MNG_AGG_DISABLE_START_TH 3

/*******************************************************************************/
static const RS_MNG_STA_LIMITS_S g_rsMngStaModLimits[] = {
    {
        // HT/VHT/HE
        .successFramesLimit = RS_MNG_NON_LEGACY_SUCCESS_LIMIT,
        .failedFramesLimit = RS_MNG_NON_LEGACY_FAILURE_LIMIT,
        .statsFlushTimeLimit = RS_MNG_STATS_FLUSH_TIME_LIMIT,
        .clearTblWindowsLimit = RS_MNG_NON_LEGACY_MOD_COUNTER_LIMIT,
    },
    {
        // non-HT
        .successFramesLimit = RS_MNG_LEGACY_SUCCESS_LIMIT,
        .failedFramesLimit = RS_MNG_LEGACY_FAILURE_LIMIT,
        .statsFlushTimeLimit = RS_MNG_STATS_FLUSH_TIME_LIMIT,
        .clearTblWindowsLimit = RS_MNG_LEGACY_MOD_COUNTER_LIMIT,
    }};

static BOOLEAN _allowColAnt(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                            const RS_MNG_COL_ELEM_S* nextCol);
static BOOLEAN _allowColMimo(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                             const RS_MNG_COL_ELEM_S* nextCol);
static BOOLEAN _allowColSiso(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                             const RS_MNG_COL_ELEM_S* nextCol);
static BOOLEAN _allowColSgi(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                            const RS_MNG_COL_ELEM_S* nextCol);
static BOOLEAN _alloCol2xLTF(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                             const RS_MNG_COL_ELEM_S* nextCol);
static BOOLEAN _allowColHe(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                           const RS_MNG_COL_ELEM_S* nextCol);
static BOOLEAN _allowColHtVht(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                              const RS_MNG_COL_ELEM_S* nextCol);

static const RS_MNG_COL_ELEM_S rsMngColumns[] = {
    [RS_MNG_COL_NON_HT_ANT_A] =
        {
            .mode = RS_MNG_MODUL_LEGACY,
            .ant = TLC_MNG_CHAIN_A_MSK,
            .nextCols = {RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_SISO_ANT_A, RS_MNG_COL_MIMO2,
                         RS_MNG_COL_HE_3_2_SISO_ANT_A, RS_MNG_COL_HE_3_2_MIMO, RS_MNG_COL_INVALID,
                         RS_MNG_COL_INVALID},
            .checks = {_allowColAnt},
        },
    [RS_MNG_COL_NON_HT_ANT_B] =
        {
            .mode = RS_MNG_MODUL_LEGACY,
            .ant = TLC_MNG_CHAIN_B_MSK,
            .nextCols = {RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_SISO_ANT_B, RS_MNG_COL_MIMO2,
                         RS_MNG_COL_HE_3_2_SISO_ANT_B, RS_MNG_COL_HE_3_2_MIMO, RS_MNG_COL_INVALID,
                         RS_MNG_COL_INVALID},
            .checks = {_allowColAnt},
        },
    [RS_MNG_COL_SISO_ANT_A] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_A_MSK,
            .nextCols = {RS_MNG_COL_SISO_ANT_B, RS_MNG_COL_MIMO2, RS_MNG_COL_SISO_ANT_A_SGI,
                         RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID,
                         RS_MNG_COL_INVALID},
            .checks = {_allowColHtVht, _allowColSiso, _allowColAnt},
        },
    [RS_MNG_COL_SISO_ANT_B] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_B_MSK,
            .nextCols = {RS_MNG_COL_SISO_ANT_A, RS_MNG_COL_MIMO2, RS_MNG_COL_SISO_ANT_B_SGI,
                         RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID,
                         RS_MNG_COL_INVALID},
            .checks = {_allowColHtVht, _allowColSiso, _allowColAnt},
        },
    [RS_MNG_COL_SISO_ANT_A_SGI] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_A_MSK,
            .gi = HT_VHT_SGI,
            .nextCols = {RS_MNG_COL_SISO_ANT_B_SGI, RS_MNG_COL_MIMO2_SGI, RS_MNG_COL_SISO_ANT_A,
                         RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID,
                         RS_MNG_COL_INVALID},
            .checks = {_allowColHtVht, _allowColSiso, _allowColAnt, _allowColSgi},
        },
    [RS_MNG_COL_SISO_ANT_B_SGI] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_B_MSK,
            .gi = HT_VHT_SGI,
            .nextCols = {RS_MNG_COL_SISO_ANT_A_SGI, RS_MNG_COL_MIMO2_SGI, RS_MNG_COL_SISO_ANT_B,
                         RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID,
                         RS_MNG_COL_INVALID},
            .checks = {_allowColHtVht, _allowColSiso, _allowColAnt, _allowColSgi},
        },
    [RS_MNG_COL_MIMO2] =
        {
            .mode = RS_MNG_MODUL_MIMO2,
            .ant = TLC_MNG_CHAIN_A_MSK | TLC_MNG_CHAIN_B_MSK,
            .nextCols =
                {RS_MNG_COL_SISO_ANT_A,  // + NON_SHARED_ANT_RFIC_ID (see _rsMngGetNextColId)
                 RS_MNG_COL_MIMO2_SGI, RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B,
                 RS_MNG_COL_INVALID, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHtVht, _allowColMimo},
        },
    [RS_MNG_COL_MIMO2_SGI] =
        {
            .mode = RS_MNG_MODUL_MIMO2,
            .ant = TLC_MNG_CHAIN_A_MSK | TLC_MNG_CHAIN_B_MSK,
            .gi = HT_VHT_SGI,
            .nextCols =
                {RS_MNG_COL_SISO_ANT_A_SGI,  // + NON_SHARED_ANT_RFIC_ID (see _rsMngGetNextColId)
                 RS_MNG_COL_MIMO2, RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B,
                 RS_MNG_COL_INVALID, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHtVht, _allowColMimo, _allowColSgi},
        },
    [RS_MNG_COL_HE_3_2_SISO_ANT_A] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_A_MSK,
            .gi = HE_3_2_GI,
            .nextCols = {RS_MNG_COL_HE_3_2_SISO_ANT_B, RS_MNG_COL_HE_3_2_MIMO,
                         RS_MNG_COL_HE_1_6_SISO_ANT_A, RS_MNG_COL_NON_HT_ANT_A,
                         RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColSiso, _allowColAnt},
        },
    [RS_MNG_COL_HE_1_6_SISO_ANT_A] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_A_MSK,
            .gi = HE_1_6_GI,
            .nextCols = {RS_MNG_COL_HE_1_6_SISO_ANT_B, RS_MNG_COL_HE_1_6_MIMO,
                         RS_MNG_COL_HE_0_8_SISO_ANT_A, RS_MNG_COL_HE_3_2_SISO_ANT_A,
                         RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColSiso, _allowColAnt, _alloCol2xLTF},
        },
    [RS_MNG_COL_HE_0_8_SISO_ANT_A] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_A_MSK,
            .gi = HE_0_8_GI,
            .nextCols = {RS_MNG_COL_HE_0_8_SISO_ANT_B, RS_MNG_COL_HE_0_8_MIMO,
                         RS_MNG_COL_HE_1_6_SISO_ANT_A, RS_MNG_COL_NON_HT_ANT_A,
                         RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColSiso, _allowColAnt, _alloCol2xLTF},
        },
    [RS_MNG_COL_HE_3_2_SISO_ANT_B] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_B_MSK,
            .gi = HE_3_2_GI,
            .nextCols = {RS_MNG_COL_HE_3_2_SISO_ANT_A, RS_MNG_COL_HE_3_2_MIMO,
                         RS_MNG_COL_HE_1_6_SISO_ANT_B, RS_MNG_COL_NON_HT_ANT_A,
                         RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColSiso, _allowColAnt},
        },
    [RS_MNG_COL_HE_1_6_SISO_ANT_B] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_B_MSK,
            .gi = HE_1_6_GI,
            .nextCols = {RS_MNG_COL_HE_1_6_SISO_ANT_A, RS_MNG_COL_HE_1_6_MIMO,
                         RS_MNG_COL_HE_0_8_SISO_ANT_B, RS_MNG_COL_HE_3_2_SISO_ANT_B,
                         RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColSiso, _allowColAnt, _alloCol2xLTF},
        },
    [RS_MNG_COL_HE_0_8_SISO_ANT_B] =
        {
            .mode = RS_MNG_MODUL_SISO,
            .ant = TLC_MNG_CHAIN_B_MSK,
            .gi = HE_0_8_GI,
            .nextCols = {RS_MNG_COL_HE_0_8_SISO_ANT_A, RS_MNG_COL_HE_0_8_MIMO,
                         RS_MNG_COL_HE_1_6_SISO_ANT_B, RS_MNG_COL_NON_HT_ANT_A,
                         RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColSiso, _allowColAnt, _alloCol2xLTF},
        },
    [RS_MNG_COL_HE_3_2_MIMO] =
        {
            .mode = RS_MNG_MODUL_MIMO2,
            .ant = TLC_MNG_CHAIN_A_MSK | TLC_MNG_CHAIN_B_MSK,
            .gi = HE_3_2_GI,
            .nextCols =
                {RS_MNG_COL_HE_3_2_SISO_ANT_A,  // + NON_SHARED_ANT_RFIC_ID (see _rsMngGetNextColId)
                 RS_MNG_COL_HE_1_6_MIMO, RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B,
                 RS_MNG_COL_INVALID, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColMimo},
        },
    [RS_MNG_COL_HE_1_6_MIMO] =
        {
            .mode = RS_MNG_MODUL_MIMO2,
            .ant = TLC_MNG_CHAIN_A_MSK | TLC_MNG_CHAIN_B_MSK,
            .gi = HE_1_6_GI,
            .nextCols =
                {RS_MNG_COL_HE_1_6_SISO_ANT_A,  // + NON_SHARED_ANT_RFIC_ID (see _rsMngGetNextColId)
                 RS_MNG_COL_HE_0_8_MIMO, RS_MNG_COL_HE_3_2_MIMO, RS_MNG_COL_NON_HT_ANT_A,
                 RS_MNG_COL_NON_HT_ANT_B, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColMimo, _alloCol2xLTF},
        },
    [RS_MNG_COL_HE_0_8_MIMO] =
        {
            .mode = RS_MNG_MODUL_MIMO2,
            .ant = TLC_MNG_CHAIN_A_MSK | TLC_MNG_CHAIN_B_MSK,
            .gi = HE_0_8_GI,
            .nextCols =
                {RS_MNG_COL_HE_0_8_SISO_ANT_A,  // + NON_SHARED_ANT_RFIC_ID (see _rsMngGetNextColId)
                 RS_MNG_COL_HE_1_6_MIMO, RS_MNG_COL_NON_HT_ANT_A, RS_MNG_COL_NON_HT_ANT_B,
                 RS_MNG_COL_INVALID, RS_MNG_COL_INVALID, RS_MNG_COL_INVALID},
            .checks = {_allowColHe, _allowColMimo, _alloCol2xLTF},
        },

};
/*******************************************************************************/

/*******************************************************************************/
/*        General Helper functions                                             */
/*******************************************************************************/

static void _rsMngRateCheckSet(const RS_MNG_RATE_S* rsMngRate,
                               RS_MNG_RATE_SETTING_BITMAP_E setting) {
  WARN_ON(rsMngRate->unset & setting);
}

static RS_MNG_MODULATION_E _rsMngRateGetModulation(const RS_MNG_RATE_S* rsMngRate) {
  RS_MNG_MODULATION_E modulation;

  if (IS_RATE_OFDM_VHT_API_M(rsMngRate->rate) || IS_RATE_OFDM_HT_API_M(rsMngRate->rate) ||
      IS_RATE_OFDM_HE_API_M(rsMngRate->rate)) {
    if (GET_MIMO_INDEX_API_M(rsMngRate->rate) == SISO_INDX) {
      modulation = RS_MNG_MODUL_SISO;
    } else {
      modulation = RS_MNG_MODUL_MIMO2;
    }
  } else {
    modulation = RS_MNG_MODUL_LEGACY;
  }

  _rsMngRateCheckSet(rsMngRate, RS_MNG_RATE_MODULATION);
  return modulation;
}

static void _rsMngRateSetModulation(RS_MNG_RATE_S* rsMngRate, RS_MNG_MODULATION_E mod) {
  if (mod == RS_MNG_MODUL_MIMO2) {
    if (IS_RATE_OFDM_VHT_API_M(rsMngRate->rate) || IS_RATE_OFDM_HE_API_M(rsMngRate->rate)) {
      rsMngRate->rate.rate_n_flags |= RATE_MCS_VHT_MIMO2;
    } else {
      rsMngRate->rate.rate_n_flags |= RATE_MCS_HT_MIMO2_MSK;
    }
  }

  if (mod == RS_MNG_MODUL_SISO) {
    if (IS_RATE_OFDM_VHT_API_M(rsMngRate->rate) || IS_RATE_OFDM_HE_API_M(rsMngRate->rate)) {
      rsMngRate->rate.rate_n_flags &= ~RATE_MCS_VHT_MIMO2;
    } else {
      rsMngRate->rate.rate_n_flags &= ~RATE_MCS_HT_MIMO2_MSK;
    }
  }

  rsMngRate->unset &= ~RS_MNG_RATE_MODULATION;
  rsMngRate->unset |= RS_MNG_RATE_STBC;
}

static TLC_MNG_MODE_E _rsMngRateGetMode(const RS_MNG_RATE_S* rsMngRate) {
  TLC_MNG_MODE_E rateMode;

  _rsMngRateCheckSet(rsMngRate, RS_MNG_RATE_MODE);

  if (IS_RATE_OFDM_HE_API_M(rsMngRate->rate)) {
    rateMode = TLC_MNG_MODE_HE;
  } else if (IS_RATE_OFDM_VHT_API_M(rsMngRate->rate)) {
    rateMode = TLC_MNG_MODE_VHT;
  } else if (IS_RATE_OFDM_HT_API_M(rsMngRate->rate)) {
    rateMode = TLC_MNG_MODE_HT;
  } else {
    rateMode = TLC_MNG_MODE_LEGACY;
  }

  return rateMode;
}

static void _rsMngRateSetMode(RS_MNG_RATE_S* rsMngRate, TLC_MNG_MODE_E mode) {
  /* This resets the rate completely */
  rsMngRate->rate.rate_n_flags = 0;
  /* We don't really use bfer, so don't mark it as reset */
  rsMngRate->unset = RS_MNG_RATE_SET_ALL & ~(RS_MNG_RATE_MODE | RS_MNG_RATE_BFER);

  switch (mode) {
    case TLC_MNG_MODE_LEGACY:
      break;
    case TLC_MNG_MODE_HT:
      rsMngRate->rate.rate_n_flags |= RATE_MCS_HT_MSK;
      break;
    case TLC_MNG_MODE_VHT:
      rsMngRate->rate.rate_n_flags |= RATE_MCS_VHT_MSK;
      break;
    case TLC_MNG_MODE_HE:
      rsMngRate->rate.rate_n_flags |= RATE_MCS_HE_MSK;
      break;
    default:
      break;
  }
}

static U32 _rsMngRateGetBw(const RS_MNG_RATE_S* rsMngRate) {
  U32 bw = GET_BW_INDEX_API_M(rsMngRate->rate);

  _rsMngRateCheckSet(rsMngRate, RS_MNG_RATE_BW);
  return bw;
}

static void _rsMngRateSetBw(RS_MNG_RATE_S* rsMngRate, U32 bw) {
  rsMngRate->rate.rate_n_flags &= ~RATE_MCS_FAT_MSK_API_D;
  rsMngRate->rate.rate_n_flags |= bw << RATE_MCS_FAT_POS;
  rsMngRate->unset &= ~RS_MNG_RATE_BW;
}

static RS_MNG_GI_E _rsMngRateGetGi(const RS_MNG_RATE_S* rsMngRate) {
  _rsMngRateCheckSet(rsMngRate, RS_MNG_RATE_GI);

  if (_rsMngRateGetMode(rsMngRate) != TLC_MNG_MODE_HE) {
    return (rsMngRate->rate.rate_n_flags & RATE_MCS_SGI_MSK) ? HT_VHT_SGI : HT_VHT_NGI;
  }

  switch (GET_OFDM_HE_GI_LTF_INDX_API_M(rsMngRate->rate)) {
    case 0:
    case 1:
      return HE_0_8_GI;
    case 2:
      return HE_1_6_GI;
    case 3:
      return HE_3_2_GI;
  }

  return HT_VHT_NGI;  // impossible
}

static void _rsMngRateSetGi(RS_MNG_RATE_S* rsMngRate, RS_MNG_GI_E gi) {
  if (_rsMngRateGetMode(rsMngRate) != TLC_MNG_MODE_HE) {
    WARN_ON(!(gi == HT_VHT_NGI || gi == HT_VHT_SGI));
    rsMngRate->rate.rate_n_flags &= ~RATE_MCS_SGI_MSK;
    rsMngRate->rate.rate_n_flags |= (gi == HT_VHT_SGI) << RATE_MCS_SGI_POS;
  } else {
    rsMngRate->rate.rate_n_flags &= ~RATE_MCS_HE_GI_LTF_MSK;
    switch (gi) {
      case HT_VHT_NGI:
      case HT_VHT_SGI:
        WARN_ON(1);
        break;
      case HE_0_8_GI:
        // 2xLTF
        rsMngRate->rate.rate_n_flags |= 1 << RATE_MCS_HE_GI_LTF_POS;
        break;
      case HE_1_6_GI:
        // 2xLTF
        rsMngRate->rate.rate_n_flags |= 2 << RATE_MCS_HE_GI_LTF_POS;
        break;
      case HE_3_2_GI:
        // 4xLTF
        rsMngRate->rate.rate_n_flags |= 3 << RATE_MCS_HE_GI_LTF_POS;
        break;
    }
  }

  rsMngRate->unset &= ~RS_MNG_RATE_GI;
}

static void _rsMngRateSetLdpc(RS_MNG_RATE_S* rsMngRate, BOOLEAN ldpc) {
  rsMngRate->rate.rate_n_flags &= ~RATE_MCS_LDPC_MSK;
  rsMngRate->rate.rate_n_flags |= (!!ldpc) << RATE_MCS_LDPC_POS;
  rsMngRate->unset &= ~RS_MNG_RATE_LDPC;
}

static BOOLEAN _rsMngRateGetStbc(const RS_MNG_RATE_S* rsMngRate) {
  _rsMngRateCheckSet(rsMngRate, RS_MNG_RATE_STBC);
  return !!(rsMngRate->rate.rate_n_flags & RATE_MCS_STBC_MSK);
}

static void _rsMngRateSetStbc(RS_MNG_RATE_S* rsMngRate, BOOLEAN stbc) {
  WARN_ON(!(!stbc || !(rsMngRate->rate.rate_n_flags & RATE_MCS_HE_DCM_MSK)));

  rsMngRate->rate.rate_n_flags &= ~RATE_MCS_STBC_MSK;
  rsMngRate->rate.rate_n_flags |= (!!stbc) << RATE_MCS_STBC_POS;
  rsMngRate->unset &= ~RS_MNG_RATE_STBC;
  rsMngRate->unset |= RS_MNG_RATE_ANT;
}

static void _rsMngRateSetBfer(RS_MNG_RATE_S* rsMngRate, BOOLEAN bfer) {
  rsMngRate->rate.rate_n_flags &= ~RATE_MCS_BF_MSK;
  rsMngRate->rate.rate_n_flags |= (!!bfer) << RATE_MCS_BF_POS;
  rsMngRate->unset &= ~RS_MNG_RATE_BFER;
}

static U08 _rsMngRateGetAnt(const RS_MNG_RATE_S* rsMngRate) {
  _rsMngRateCheckSet(rsMngRate, RS_MNG_RATE_ANT);
  return (U08)GET_ANT_CHAIN_API_M(rsMngRate->rate);
  ;
}

static void _rsMngRateSetAnt(RS_MNG_RATE_S* rsMngRate, U08 ant) {
  // compilation asserts to make sure tlc offload api and rate api agree
  BUILD_BUG_ON(!(TLC_MNG_CHAIN_A_MSK ==
                 SHIFT_AND_MASK(RATE_MCS_ANT_A_MSK, RATE_MCS_ANT_ABC_MSK, RATE_MCS_ANT_A_POS)));
  BUILD_BUG_ON(!(TLC_MNG_CHAIN_B_MSK ==
                 SHIFT_AND_MASK(RATE_MCS_ANT_B_MSK, RATE_MCS_ANT_ABC_MSK, RATE_MCS_ANT_A_POS)));

  rsMngRate->rate.rate_n_flags &= ~RATE_MCS_ANT_ABC_MSK;
  rsMngRate->rate.rate_n_flags |= ant << RATE_MCS_ANT_A_POS;

  rsMngRate->unset &= ~RS_MNG_RATE_ANT;
}

static U08 _rsMngRateGetIdx(const RS_MNG_RATE_S* rsMngRate) { return rsMngRate->idx.idx; }

static void _rsMngRateSetIdx(RS_MNG_RATE_S* rsMngRate, U08 idx) {
  rsMngRate->idx.idx = idx;

  // DCM and STBC can't coexist. Since DCM is set here, make sure stbc has been set before setting
  // the index, so the stbc setting could be overriden here without issue
  _rsMngRateCheckSet(rsMngRate, RS_MNG_RATE_STBC);

  switch (_rsMngRateGetMode(rsMngRate)) {
    case TLC_MNG_MODE_HE:
      rsMngRate->rate.rate_n_flags &= ~RATE_MCS_VHT_RATE_CODE_MSK;
      if (idx == RS_MCS_0_HE_ER_AND_DCM) {
        rsMngRate->rate.rate_n_flags |=
            RATE_MCS_HE_DCM_MSK | (RATE_MCS_HE_EXT_RANGE << RATE_MCS_VHT_HE_TYPE_POS);
        rsMngRate->rate.rate_n_flags &= ~RATE_MCS_STBC_MSK;
      } else {
        rsMngRate->rate.rate_n_flags &= ~(RATE_MCS_HE_DCM_MSK | RATE_MCS_VHT_HE_TYPE_MSK);
        rsMngRate->rate.rate_n_flags |= idx;
      }
      break;
    case TLC_MNG_MODE_VHT:
      rsMngRate->rate.rate_n_flags &= ~RATE_MCS_VHT_RATE_CODE_MSK;
      rsMngRate->rate.rate_n_flags |= idx;
      break;
    case TLC_MNG_MODE_HT:
      rsMngRate->rate.rate_n_flags &= ~RATE_MCS_HT_RATE_CODE_MSK;
      rsMngRate->rate.rate_n_flags |= idx;
      break;
    case TLC_MNG_MODE_LEGACY: {
      RS_NON_HT_RATES_E nonHtIdx = (RS_NON_HT_RATES_E)idx;

      rsMngRate->rate.rate_n_flags &= ~0xff;
      rsMngRate->rate.rate_n_flags |= RS_NON_HT_RATE_TO_API_RATE[nonHtIdx];
      if (nonHtIdx <= RS_NON_HT_RATE_CCK_LAST) {
        rsMngRate->rate.rate_n_flags |= RATE_MCS_CCK_MSK;
      } else {
        rsMngRate->rate.rate_n_flags &= ~RATE_MCS_CCK_MSK;
      }
      break;
    }
    default:
      // shouldn't happen. return now so the index remains unset and an assert will be hit when
      // trying to build the rate table with this rate
      return;
  }

  rsMngRate->unset &= ~RS_MNG_RATE_U_IDX;
}

static void _rsMngRateInvalidate(RS_MNG_RATE_S* rsMngRate) {
  rsMngRate->unset = RS_MNG_RATE_SET_ALL;
}

static U16 _rsMngGetSupportedRatesByModeAndBw(const RS_MNG_STA_INFO_S* staInfo,
                                              RS_MNG_MODULATION_E modulation,
                                              TLC_MNG_CH_WIDTH_E bw) {
  BOOLEAN isBw160;
  U32 supportedRates;

  if (modulation == RS_MNG_MODUL_LEGACY) {
    return staInfo->config.nonHt;
  }

  isBw160 = (bw == TLC_MNG_CH_WIDTH_160MHZ);
  supportedRates = (modulation == RS_MNG_MODUL_SISO ? staInfo->config.mcs[TLC_MNG_NSS_1][isBw160]
                                                    : staInfo->config.mcs[TLC_MNG_NSS_2][isBw160]);

  if (staInfo->config.bestSuppMode == TLC_MNG_MODE_VHT && bw == CHANNEL_WIDTH20) {
    // In VHT, mcs 9 is never posible at 20mhz bandwidth
    supportedRates &= ~BIT(RS_MCS_9);
  }

  return (U16)supportedRates;
}

static U16 _rsMngGetSuppRatesSameMode(const RS_MNG_STA_INFO_S* staInfo,
                                      const RS_MNG_RATE_S* rsMngRate) {
  return _rsMngGetSupportedRatesByModeAndBw(staInfo, _rsMngRateGetModulation(rsMngRate),
                                            _rsMngRateGetBw(rsMngRate));
}

static TLC_MNG_CH_WIDTH_E _rsMngGetMaxChWidth(const RS_MNG_STA_INFO_S* staInfo) {
  return (TLC_MNG_CH_WIDTH_E)staInfo->config.maxChWidth;
}

static BOOLEAN _rsMngAreAggsSupported(TLC_MNG_MODE_E bestSuppMode) {
  return bestSuppMode > TLC_MNG_MODE_LEGACY;
}

static BOOLEAN _rsMngIsDcmSupported(const RS_MNG_STA_INFO_S* staInfo, BOOLEAN isMimo) {
  if (isMimo) {
    return !!(staInfo->config.configFlags & TLC_MNG_CONFIG_FLAGS_HE_DCM_NSS_2_MSK);
  }

  return !!(staInfo->config.configFlags & TLC_MNG_CONFIG_FLAGS_HE_DCM_NSS_1_MSK);
}

static BOOLEAN _rsMngRateIsOptimal(const RS_MNG_STA_INFO_S* staInfo,
                                   const RS_MNG_RATE_S* rsMngRate) {
  U32 bw = _rsMngRateGetBw(rsMngRate);
  BOOLEAN mimoAllowed = staInfo->config.mcs[TLC_MNG_NSS_2][bw == CHANNEL_WIDTH160];

  if (_rsMngRateGetMode(rsMngRate) != staInfo->config.bestSuppMode) {
    return FALSE;
  }

  if (_rsMngRateGetIdx(rsMngRate) != MSB2ORD(_rsMngGetSuppRatesSameMode(staInfo, rsMngRate))) {
    return FALSE;
  }

  // TODO: check for best ltf/gi in HE. This condition currently means that tpc won't be enabled
  // in HE.
  if ((staInfo->config.sgiChWidthSupport & BIT(bw)) && _rsMngRateGetGi(rsMngRate) != HT_VHT_SGI) {
    return FALSE;
  }

  if (mimoAllowed && _rsMngRateGetModulation(rsMngRate) != RS_MNG_MODUL_MIMO2) {
    return FALSE;
  }

  if (bw != _rsMngGetMaxChWidth(staInfo)) {
    return FALSE;
  }

  return TRUE;
}

static U08 _rsMngGetHigherRateIdx(U08 initRateIdx, U32 supportedRatesMsk) {
  U32 tmpRateMsk;

  if (initRateIdx == RS_MCS_0_HE_ER_AND_DCM) {
    return (U08)LSB2ORD(supportedRatesMsk);
  }

  tmpRateMsk = supportedRatesMsk & (0xFFFFFFFF << (initRateIdx + 1));

  return (U08)(tmpRateMsk == 0 ? RS_MNG_INVALID_RATE_IDX : LSB2ORD(tmpRateMsk));
}

static U08 _rsMngGetLowerRateIdx(const RS_MNG_STA_INFO_S* staInfo, const RS_MNG_RATE_S* rate,
                                 U32 supportedRatesMsk) {
  U08 idx = _rsMngRateGetIdx(rate);
  U32 tmpRateMsk = (supportedRatesMsk & ((1 << idx) - 1));

  if (idx == RS_MCS_0_HE_ER_AND_DCM) {
    return RS_MNG_INVALID_RATE_IDX;
  }

  if (tmpRateMsk == 0) {
    if (_rsMngRateGetMode(rate) == TLC_MNG_MODE_HE && _rsMngRateGetBw(rate) == CHANNEL_WIDTH20 &&
        _rsMngIsDcmSupported(staInfo, _rsMngRateGetModulation(rate) == RS_MNG_MODUL_MIMO2)) {
      return RS_MCS_0_HE_ER_AND_DCM;
    }

    return RS_MNG_INVALID_RATE_IDX;
  }

  return (U08)MSB2ORD(tmpRateMsk);
}

// rs_get_adjacent_rate
// get adjacent supported rate
// suppRateDir : use GET_HIGHER_SUPPORTED_RATE or GET_LOWER_SUPPORTED_RATE
static U08 _rsMngGetAdjacentRateIdx(const RS_MNG_STA_INFO_S* staInfo, const RS_MNG_RATE_S* initRate,
                                    U08 suppRateDir) {
  U08 initRateIdx = _rsMngRateGetIdx(initRate);
  U32 supportedRatesMsk = _rsMngGetSuppRatesSameMode(staInfo, initRate);

  return (U08)(suppRateDir == GET_LOWER_SUPPORTED_RATE
                   ? _rsMngGetLowerRateIdx(staInfo, initRate, supportedRatesMsk)
                   : _rsMngGetHigherRateIdx(initRateIdx, supportedRatesMsk));
}

// TODO - check. what if bt doesn't allow?
static BOOLEAN _rsMngIsStbcSupported(const RS_MNG_STA_INFO_S* staInfo) {
  return !!(staInfo->config.configFlags & TLC_MNG_CONFIG_FLAGS_STBC_MSK);
}

static BOOLEAN _rsMngIsStbcAllowed(const RS_MNG_STA_INFO_S* staInfo, const RS_MNG_RATE_S* rate) {
  if ((iwl_mvm_get_valid_tx_ant(staInfo->mvm) & rsMngGetDualAntMsk()) != rsMngGetDualAntMsk()) {
    return FALSE;
  }
  return _rsMngIsStbcSupported(staInfo) && !(rate->rate.rate_n_flags & RATE_MCS_HE_DCM_MSK);
}

static BOOLEAN _rsMngCoexIsLongAggAllowed(const RS_MNG_STA_INFO_S* staInfo) {
  if (staInfo->config.band != NL80211_BAND_2GHZ) {
    return TRUE;
  }

  if (btCoexManagerBtOwnsAnt(staInfo->mvm)) {
    return FALSE;
  }

  return TRUE;
}

static BOOLEAN _rsMngIsLdpcAllowed(const RS_MNG_STA_INFO_S* staInfo) {
  return !!(staInfo->config.configFlags & TLC_MNG_CONFIG_FLAGS_LDPC_MSK);
}

static BOOLEAN _rsMngIsAntSupported(const RS_MNG_STA_INFO_S* staInfo, U08 ant) {
  return (ant & staInfo->config.chainsEnabled) == ant &&
         (iwl_mvm_get_valid_tx_ant(staInfo->mvm) & ant) == ant;
}
/*******************************************************************************/

/************************************************************************************/
/*                            allowColFuncs                                         */
/************************************************************************************/

static BOOLEAN _allowColAnt(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                            const RS_MNG_COL_ELEM_S* nextCol) {
  if (!_rsMngIsAntSupported(staInfo, nextCol->ant)) {
    return FALSE;
  }

  if (!_rsMngIsAntSupported(staInfo, (U08)(nextCol->ant ^ rsMngGetDualAntMsk()))) {
    // If the other antenna is disabled for some reason, this antenna is the only one allowed so
    // we must ignore possible BT-Coex restrictions. Also note that this function is only called
    // for siso columns, so nextCol->ant always has just one bit set so the xor makes sense.
    return TRUE;
  }

  if (staInfo->config.band != NL80211_BAND_2GHZ) {
    return TRUE;
  }

  if (btCoexManagerIsAntAvailable(staInfo->mvm, nextCol->ant)) {
    return TRUE;
  }

  return FALSE;
}

static U16 _rsMngGetAggTimeLimit(RS_MNG_STA_INFO_S* staInfo) {
  // Someone configured debug values, use them no matter what
  if (staInfo->aggDurationLimit != RS_MNG_AGG_DURATION_LIMIT) {
    return staInfo->aggDurationLimit;
  }

  if (_rsMngCoexIsLongAggAllowed(staInfo)) {
    staInfo->longAggEnabled = TRUE;
    return RS_MNG_AGG_DURATION_LIMIT;
  }

  staInfo->longAggEnabled = FALSE;
  return RS_MNG_AGG_DURATION_LIMIT_SHORT;
}

static BOOLEAN _allowColMimo(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                             const RS_MNG_COL_ELEM_S* nextCol) {
  BOOLEAN isBw160 = (bw == TLC_MNG_CH_WIDTH_160MHZ);

  // TODO - check if ht/vht supported? redundent
  // if no mimo rate is supported
  if (!(staInfo->config.mcs[TLC_MNG_NSS_2][isBw160])) {
    return FALSE;
  }

  if (staInfo->config.chainsEnabled != rsMngGetDualAntMsk()) {
    return FALSE;
  }

  if (iwl_mvm_get_valid_tx_ant(staInfo->mvm) != rsMngGetDualAntMsk()) {
    return FALSE;
  }

  return TRUE;
}

static BOOLEAN _allowColSiso(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                             const RS_MNG_COL_ELEM_S* nextCol) {
  BOOLEAN isBw160 = (bw == TLC_MNG_CH_WIDTH_160MHZ);

  // if there are supported SISO rates - return true. else - return false
  return (!!(staInfo->config.mcs[TLC_MNG_NSS_1][isBw160]));
}

static BOOLEAN _allowColHe(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                           const RS_MNG_COL_ELEM_S* nextCol) {
  return !!(staInfo->config.bestSuppMode == TLC_MNG_MODE_HE);
}

static BOOLEAN _allowColHtVht(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                              const RS_MNG_COL_ELEM_S* nextCol) {
  return !!(staInfo->config.bestSuppMode == TLC_MNG_MODE_HT ||
            staInfo->config.bestSuppMode == TLC_MNG_MODE_VHT);
}

static BOOLEAN _allowColSgi(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                            const RS_MNG_COL_ELEM_S* nextCol) {
  U08 sgiChWidthSupport = staInfo->config.sgiChWidthSupport;

  return !!(sgiChWidthSupport & BIT(bw));
}

static BOOLEAN _alloCol2xLTF(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                             const RS_MNG_COL_ELEM_S* nextCol) {
  return !(staInfo->config.configFlags & TLC_MNG_CONFIG_FLAGS_HE_BLOCK_2X_LTF_MSK);
}

/***************************************************************/

static BOOLEAN _rsMngTpcIsActive(const RS_MNG_STA_INFO_S* staInfo) {
  // There are 2 values for currStep that mean tpc isn't working currently - RS_MNG_TPC_INACTIVE
  // and RS_MNG_TPC_DISABLED.
  return staInfo->tpcTable.currStep < RS_MNG_TPC_NUM_STEPS;
}
static BOOLEAN _rsMngIsTestWindow(const RS_MNG_STA_INFO_S* staInfo) {
  return staInfo->tryingRateUpscale || staInfo->searchBetterTbl || staInfo->tpcTable.testing;
}

static void _rsMngFillAggParamsLQCmd(RS_MNG_STA_INFO_S* staInfo, struct iwl_lq_cmd* lqCmd) {
  lqCmd->agg_time_limit = cpu_to_le16(_rsMngGetAggTimeLimit(staInfo));
  lqCmd->agg_disable_start_th = RS_MNG_AGG_DISABLE_START_TH;

  // W/A for a HW bug that causes it to not prepare a second burst if the first one uses
  // all frames in the Fifo. W/A this by making sure there's always at least one frame left.
  lqCmd->agg_frame_cnt_limit = (U08)(staInfo->staBuffSize - 1);
}

// Get the next supported lower rate in the current column.
// return:
// the found rate index, or
// RS_MNG_INVALID_RATE_IDX if no such rate exists
static U08 _rsMngSetLowerRate(const RS_MNG_STA_INFO_S* staInfo, RS_MNG_RATE_S* rsMngRate) {
  U08 lowerSuppRateIdx = _rsMngGetAdjacentRateIdx(staInfo, rsMngRate, GET_LOWER_SUPPORTED_RATE);

  // if this is the lowest rate possible and this is not legacy rate - break;
  if (RS_MNG_INVALID_RATE_IDX != lowerSuppRateIdx) {
    _rsMngRateSetIdx(rsMngRate, lowerSuppRateIdx);
  }

  return lowerSuppRateIdx;
}

static void tlcMngNotifyAmsdu(const RS_MNG_STA_INFO_S* staInfo, U16 amsduSize, U16 tidBitmap) {
  int i;

  staInfo->mvmsta->amsdu_enabled = tidBitmap;
  staInfo->mvmsta->max_amsdu_len = amsduSize;
  staInfo->sta->max_rc_amsdu_len = staInfo->mvmsta->max_amsdu_len;

  for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
    if (staInfo->mvmsta->amsdu_enabled & BIT(i))
      staInfo->sta->max_tid_amsdu_len[i] = iwl_mvm_max_amsdu_size(staInfo->mvm, staInfo->sta, i);
    else
    /*
     * Not so elegant, but this will effectively
     * prevent AMSDU on this TID
     */
    {
      staInfo->sta->max_tid_amsdu_len[i] = 1;
    }
  }
}

static void _rsMngFillNonHtRates(const RS_MNG_STA_INFO_S* staInfo, struct iwl_lq_cmd* lqCmd, U08 i,
                                 RS_MNG_RATE_S* rsMngRate) {
  BOOLEAN togglingPossible = _rsMngIsAntSupported(staInfo, rsMngGetDualAntMsk()) &&
                             btCoexManagerIsAntAvailable(staInfo->mvm, BT_COEX_SHARED_ANT_ID);

  if (_rsMngRateGetMode(rsMngRate) != TLC_MNG_MODE_LEGACY) {
    U08 currIdx = _rsMngRateGetIdx(rsMngRate);

    _rsMngRateSetMode(rsMngRate, TLC_MNG_MODE_LEGACY);
    _rsMngRateSetModulation(rsMngRate, RS_MNG_MODUL_LEGACY);
    _rsMngRateSetBw(rsMngRate, CHANNEL_WIDTH20);
    _rsMngRateSetLdpc(rsMngRate, FALSE);
    _rsMngRateSetStbc(rsMngRate, FALSE);

    // Always start with the non-shared antenna if it's available. If there's toggling, it
    // doesn't make much difference, and if there's no toggling due to bt-coex it promises we'll
    // stay on the non-shared antenna.
    _rsMngRateSetAnt(rsMngRate, rsMngGetSingleAntMsk(staInfo->config.chainsEnabled));
    _rsMngRateSetIdx(rsMngRate, downColMcsToLegacy[currIdx]);
  } else {
    _rsMngSetLowerRate(staInfo, rsMngRate);
  }

  for (; i < LQ_MAX_RETRY_NUM; i++) {
    lqCmd->rs_table[i] = cpu_to_le32(rsMngRate->rate.rate_n_flags);

    _rsMngSetLowerRate(staInfo, rsMngRate);

    if (togglingPossible) {
      _rsMngRateSetAnt(rsMngRate, (U08)(_rsMngRateGetAnt(rsMngRate) ^ rsMngGetDualAntMsk()));
    }
  }
}

static void _rsMngBuildRatesTbl(const RS_MNG_STA_INFO_S* staInfo, struct iwl_lq_cmd* lqCmd) {
  RS_MNG_RATE_S rsMngRate;
  U08 i = 0;
  U08 j;

  memcpy(&rsMngRate, &staInfo->rateTblInfo.rsMngRate, sizeof(rsMngRate));

  if (staInfo->searchBetterTbl) {
    // When trying a new column, only the initial rate should be of that column. The rest of the
    // table is constructed from the "stable" column.
    lqCmd->rs_table[0] = cpu_to_le32(staInfo->searchColData.rsMngRate.rate.rate_n_flags);
    i++;
  } else if (staInfo->tryingRateUpscale) {
    // When trying a higher mcs, try it only once. The next retries will be from the previous
    // mcs which is known to be good (otherwise wouldn't be trying a higher one).
    lqCmd->rs_table[0] = cpu_to_le32(staInfo->rateTblInfo.rsMngRate.rate.rate_n_flags);
    i++;
    _rsMngSetLowerRate(staInfo, &rsMngRate);
  }

  // Fill RS_MNG_RETRY_TABLE_INITIAL_RATE_NUM copies of the best known stable rate
  for (j = 0; j < RS_MNG_RETRY_TABLE_INITIAL_RATE_NUM; j++) {
    lqCmd->rs_table[i + j] = cpu_to_le32(rsMngRate.rate.rate_n_flags);
  }
  i += j;

  if (!(staInfo->searchBetterTbl || staInfo->tryingRateUpscale) &&
      _rsMngSetLowerRate(staInfo, &rsMngRate) != RS_MNG_INVALID_RATE_IDX) {
    // In case the first rate is not a test rate, put here RS_MNG_RETRY_TABLE_SECONDARY_RATE_NUM
    // copies of the initial rate but mcs-1
    // Note that a tpc test window is not treated as a test rate for the purpose of construction
    // of the retry table.
    for (j = 0; j < RS_MNG_RETRY_TABLE_SECONDARY_RATE_NUM; j++) {
      lqCmd->rs_table[i + j] = cpu_to_le32(rsMngRate.rate.rate_n_flags);
    }
    i += j;

    // Now put RS_MNG_RETRY_TABLE_SECONDARY_RATE_20MHZ_NUM copies of the secondary rate with
    // 20mhz bandwidth.
    _rsMngRateSetBw(&rsMngRate, CHANNEL_WIDTH20);
    for (j = 0; j < RS_MNG_RETRY_TABLE_SECONDARY_RATE_20MHZ_NUM; j++) {
      lqCmd->rs_table[i + j] = cpu_to_le32(rsMngRate.rate.rate_n_flags);
    }
    i += j;
  }

  // Fill the rest of the retry table with non-ht rates
  _rsMngFillNonHtRates(staInfo, lqCmd, i, &rsMngRate);
}

static void _rsMngFillLQCmd(RS_MNG_STA_INFO_S* staInfo, struct iwl_lq_cmd* lqCmd) {
  int i;

  memset(lqCmd, 0, sizeof(*lqCmd));
  lqCmd->sta_id = staInfo->mvmsta->sta_id;

  if (_rsMngTpcIsActive(staInfo)) {
    // reduce Tx power in steps of 3db. Note that currStep == 0 means reduce 3db, hence the '+1'
    lqCmd->reduced_tpc = (U08)(RS_MNG_TPC_STEP_SIZE * (staInfo->tpcTable.currStep + 1));
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngFillLQCmd: reducing tx power by %d db",
               lqCmd->reduced_tpc);
  }

  _rsMngFillAggParamsLQCmd(staInfo, lqCmd);

  _rsMngBuildRatesTbl(staInfo, lqCmd);

  lqCmd->single_stream_ant_msk = rsMngGetSingleAntMsk(staInfo->config.chainsEnabled);
  lqCmd->dual_stream_ant_msk = rsMngGetDualAntMsk();

  if (_rsMngIsTestWindow(staInfo)) {
    if (IS_RATE_OFDM_HT_VHT_HE_API_M(le32_to_cpu(lqCmd->rs_table[0]))) {
      // For 11a/b/g rates, where there are no aggregations anyway, RTS protection just hurts
      // the tpt.
      lqCmd->rs_table[0] |= cpu_to_le32(RATE_MCS_RTS_REQUIRED_MSK);
      // TODO: lqCmd->agg_params.uAggFrameCntInTestWin = RS_MNG_UPSCALE_AGG_FRAME_COUNT;
    }
    // TODO: lqCmd->general_params.flags |= LINK_QUAL_FLAGS_TEST_WINDOWS_MSK;
  }

  if (staInfo->mvmsta->tx_protection) {
    lqCmd->flags |= LQ_FLAG_USE_RTS_MSK;
  }

  // When Amsdu's are enabled, enable RTS protection for all rates that use A-MPDUs, since in this
  // case there could be really long frames and this should help reduce collisions.
  if (staInfo->amsduEnabledSize != RS_MNG_AMSDU_INVALID) {
    for (i = 0; i < RS_MNG_AGG_DISABLE_START_TH; i++) {
      lqCmd->rs_table[i] |= cpu_to_le32(RATE_MCS_RTS_REQUIRED_MSK);
    }
  }
}

// rs_update_rate_tbl
static void _rsMngUpdateRateTbl(RS_MNG_STA_INFO_S* staInfo, BOOLEAN notifyHost) {
  _rsMngFillLQCmd(staInfo, &staInfo->mvmsta->lq_sta.rs_drv.lq);

  iwl_mvm_send_lq_cmd(staInfo->mvm, &staInfo->mvmsta->lq_sta.rs_drv.lq, !staInfo->enabled);
}

static void _rsMngClearWinArr(RS_MNG_WIN_STAT_S* winArr, U08 numWin) {
  U08 i;

  _memclr(winArr, (sizeof(*winArr) * numWin));

  for (i = 0; i < numWin; i++) {
    winArr[i].successRatio = RS_MNG_INVALID_VAL;
    winArr[i].averageTpt = RS_MNG_INVALID_VAL;
  }
}

static void _rsMngClearTblWindows(RS_MNG_STA_INFO_S* staInfo) {
  _rsMngClearWinArr(staInfo->rateTblInfo.win, RS_MNG_MAX_RATES_NUM);

  _rsMngClearWinArr(staInfo->tpcTable.windows, RS_MNG_TPC_NUM_STEPS);
}

static void _rsMngSetVisitedColumn(RS_MNG_STA_INFO_S* staInfo, RS_MNG_COLUMN_DESC_E colId) {
  // to make the code for setting both siso columns in case of stbc simpler, make sure that each
  // such pair of columns has only bit 0 different.
  BUILD_BUG_ON(!((RS_MNG_COL_SISO_ANT_A ^ RS_MNG_COL_SISO_ANT_B) == 1));
  BUILD_BUG_ON(!((RS_MNG_COL_SISO_ANT_A_SGI ^ RS_MNG_COL_SISO_ANT_B_SGI) == 1));
  BUILD_BUG_ON(!((RS_MNG_COL_HE_3_2_SISO_ANT_A ^ RS_MNG_COL_HE_3_2_SISO_ANT_B) == 1));
  BUILD_BUG_ON(!((RS_MNG_COL_HE_1_6_SISO_ANT_A ^ RS_MNG_COL_HE_1_6_SISO_ANT_B) == 1));
  BUILD_BUG_ON(!((RS_MNG_COL_HE_0_8_SISO_ANT_A ^ RS_MNG_COL_HE_0_8_SISO_ANT_B) == 1));

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngSetVisitedColumn: colId %d, stbc allowed %d, visited columns 0x%x", colId,
             _rsMngIsStbcSupported(staInfo), staInfo->visitedColumns);

  staInfo->visitedColumns |= BIT(colId);
  if (rsMngColumns[colId].mode == RS_MNG_MODUL_SISO && _rsMngIsStbcSupported(staInfo)) {
    staInfo->visitedColumns |= BIT(colId ^ 1);
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngSetVisitedColumn: visited columns 0x%x",
             staInfo->visitedColumns);
}

static U32 _rsMngVhtRateToPhyRate(U32 bw, RS_MCS_E mcs, RS_MNG_GI_E gi, RS_MNG_MODULATION_E nss) {
  U32 bitrate;

  if (WARN_ON(!(mcs < 10 && bw < MAX_CHANNEL_BW_INDX && nss >= RS_MNG_MODUL_SISO))) {
    return 1;
  }

  bitrate = rsMngVhtRateToBps[bw][mcs];

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngAmsduRate: bw %d, mcs %d sgi %d nss %d", bw, mcs, gi,
             nss);

  if (nss == RS_MNG_MODUL_MIMO2) {
    bitrate *= 2;
  }

  if (gi == HT_VHT_SGI) {
    bitrate = bitrate + (bitrate / 9);
  }

  return bitrate >> 20;
}

static U32 _rsMngHeRateToPhyRate(U32 bw, RS_MCS_E mcs, RS_MNG_GI_E gi, RS_MNG_MODULATION_E nss) {
#define RATIO_SCALE 2048
#define MBPS_X_10_TO_KBPS(x) (((x) << 10) / 10)
  static const U16 mcsRatios[12] = {
      34133, /* 16.666666... */
      17067, /*  8.333333... */
      11378, /*  5.555555... */
      8533,  /*  4.166666... */
      5689,  /*  2.777777... */
      4267,  /*  2.083333... */
      3923,  /*  1.851851... */
      3413,  /*  1.666666... */
      2844,  /*  1.388888... */
      2560,  /*  1.250000... */
      2276,  /*  1.111111... */
      2048,  /*  1.000000... */
  };
  static const U32 ratesPerGi[][3] = {
      // phy rate in kbps per GI for mcs 11 with 2 ss
      //                     HE_3_2_GI                  HE_1_6_GI                 HE_0_8_GI
      [CHANNEL_WIDTH20] = {MBPS_X_10_TO_KBPS(2438), MBPS_X_10_TO_KBPS(2708),
                           MBPS_X_10_TO_KBPS(2868)},
      [CHANNEL_WIDTH40] = {MBPS_X_10_TO_KBPS(4875), MBPS_X_10_TO_KBPS(5417),
                           MBPS_X_10_TO_KBPS(5735)},
      [CHANNEL_WIDTH80] = {MBPS_X_10_TO_KBPS(10208), MBPS_X_10_TO_KBPS(11343),
                           MBPS_X_10_TO_KBPS(12010)},
      [CHANNEL_WIDTH160] = {MBPS_X_10_TO_KBPS(20416), MBPS_X_10_TO_KBPS(22685),
                            MBPS_X_10_TO_KBPS(24019)},
  };
  U64 tmp;
  U32 bitrate;
  BOOLEAN isDcm = FALSE;

  if (mcs == RS_MCS_0_HE_ER_AND_DCM) {
    isDcm = TRUE;
    mcs = RS_MCS_0;
  }

  if (WARN_ON(!(mcs < 12 && bw < MAX_CHANNEL_BW_INDX && gi >= HE_FIRST_GI &&
                nss >= RS_MNG_MODUL_SISO))) {
    return 1;
  }

  bitrate = ratesPerGi[bw][gi - HE_FIRST_GI];
  tmp = bitrate;
  tmp *= RATIO_SCALE;
  tmp /= mcsRatios[mcs];
  bitrate = (U32)tmp;

  if (nss == RS_MNG_MODUL_SISO) {
    bitrate /= 2;
  }

  if (isDcm) {
    bitrate /= 2;
  }

  return bitrate >> 10;
}

static U32 _rsMngRateToPhyRate(TLC_MNG_MODE_E mode, U32 bw, RS_MCS_E mcs, RS_MNG_GI_E gi,
                               RS_MNG_MODULATION_E nss) {
  if (mode == TLC_MNG_MODE_VHT) {
    return _rsMngVhtRateToPhyRate(bw, mcs, gi, nss);
  }
  if (mode == TLC_MNG_MODE_HE) {
    return _rsMngHeRateToPhyRate(bw, mcs, gi, nss);
  }

  return 0;
}

static RS_MNG_TX_AMSDU_SIZE_E _rsMngAmsduSize(const RS_MNG_STA_INFO_S* staInfo, TLC_MNG_MODE_E mode,
                                              U32 bw, RS_MCS_E mcs, RS_MNG_GI_E gi,
                                              RS_MNG_MODULATION_E nss) {
  RS_MNG_TX_AMSDU_SIZE_E amsdu_3k, amsdu_5k, amsdu_6k, amsdu_8k;
  U32 phyRate = _rsMngRateToPhyRate(mode, bw, mcs, gi, nss);

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngAmsduRate: sta %d, phyRate %d, blacklist bitmap 0x%X",
             _rsMngStaInfoToStaId(staInfo), phyRate, staInfo->amsduBlacklist);

  amsdu_3k = RS_MNG_AMSDU_3500B;
  amsdu_5k = RS_MNG_AMSDU_5000B;

  if (staInfo->amsduBlacklist) {
    // If we disabled 3k AMSDU - don't use it at all
    if (staInfo->amsduBlacklist & BIT(RS_MNG_AMSDU_3500B)) {
      amsdu_3k = RS_MNG_AMSDU_INVALID;
    }

    // If we disabled some amsdu size, use a smaller size.
    // Note that smaller sizes that are blacklisted as well will still not be used.
    if (staInfo->amsduBlacklist & BIT(RS_MNG_AMSDU_5000B)) {
      amsdu_5k = amsdu_3k;
    }
  }

  if (mode == TLC_MNG_MODE_HE) {
    amsdu_6k = RS_MNG_AMSDU_6500B;
    amsdu_8k = RS_MNG_AMSDU_8000B;

    if (staInfo->amsduBlacklist & BIT(RS_MNG_AMSDU_6500B)) {
      amsdu_6k = amsdu_5k;
    }

    if (staInfo->amsduBlacklist & BIT(RS_MNG_AMSDU_8000B)) {
      amsdu_8k = amsdu_6k;
    }

    if (phyRate > RS_MNG_AMSDU_HE_8K_THRESHOLD) {
      return amsdu_8k;
    }

    if (phyRate > RS_MNG_AMSDU_HE_6K_THRESHOLD) {
      return amsdu_6k;
    }
  }

  if (phyRate > RS_MNG_AMSDU_5K_THRESHOLD) {
    return amsdu_5k;
  }

  if (phyRate > RS_MNG_AMSDU_3K_THRESHOLD) {
    return amsdu_3k;
  }

  return RS_MNG_AMSDU_INVALID;
}

static const TPT_BY_RATE_ARR* _rsMngGetExpectedTptTable(const RS_MNG_COL_ELEM_S* col,
                                                        TLC_MNG_CH_WIDTH_E bw, BOOLEAN isAgg) {
  U32 nss;
  U32 gi;

  if (col->mode == RS_MNG_MODUL_LEGACY) {
    return &expectedTptNonHt;
  }

  nss = col->mode == RS_MNG_MODUL_SISO ? RS_MNG_SISO : RS_MNG_MIMO;

  switch (col->gi) {
    case HT_VHT_NGI:
      gi = RS_MNG_NGI;
      break;
    case HT_VHT_SGI:
      gi = RS_MNG_SGI;
      break;
    case HE_3_2_GI:
      gi = RS_MNG_GI_3_2;
      break;
    case HE_1_6_GI:
      gi = RS_MNG_GI_1_6;
      break;
    case HE_0_8_GI:
      gi = RS_MNG_GI_0_8;
      break;
    default:
      WARN_ON(1);
      gi = 0;
  }

  DBG_PRINTF(
      UT, TLC_OFFLOAD_DBG, INFO,
      "_rsMngGetExpectedTptTable: expected Tpt table - isHE %d, isAgg %d, BW %d, GI %d, NSS %d",
      col->gi >= HE_FIRST_GI, isAgg, bw, gi, nss);

  if (col->gi < HE_FIRST_GI) {
    return &expectedTptHtVht[isAgg][bw][gi][nss];
  }

  return &expectedTptHe[isAgg][bw][gi][nss];
}

static U32 _rsMngGetExpectedTpt(const RS_MNG_STA_INFO_S* staInfo, const RS_MNG_COL_ELEM_S* col,
                                TLC_MNG_CH_WIDTH_E bw, BOOLEAN isAgg, RS_MCS_E rateIdx) {
  const TPT_BY_RATE_ARR* expectedTptTable = _rsMngGetExpectedTptTable(col, bw, isAgg);
  U32 ret;

  if (expectedTptTable == &expectedTptNonHt) {
    return (*expectedTptTable)[rateIdx];
  }
  if (rateIdx == RS_MCS_0_HE_ER_AND_DCM) {
    // rateIdx == RS_MCS_0_HE_ER_AND_DCM. DCM cuts expected tpt in half.
    // TODO: add an additional small penalty for ER
    return (*expectedTptTable)[RS_MCS_0] / 2;
  }

  ret = (*expectedTptTable)[rateIdx];

  if (staInfo->amsduSupport && staInfo->mvmsta->agg_tids && staInfo->amsduInAmpdu) {
    switch (staInfo->amsduEnabledSize) {
      case RS_MNG_AMSDU_8000B:
        DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                   "_rsMngGetExpectedTpt: adding 50%% thanks to 8k amsdu");
        ret += (ret / 2);
        break;
      case RS_MNG_AMSDU_6500B:
        DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                   "_rsMngGetExpectedTpt: adding 37.5%% thanks to 6.5k amsdu");
        ret += (ret / 4) + (ret / 8);
        break;
      case RS_MNG_AMSDU_5000B:
        DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                   "_rsMngGetExpectedTpt: adding 25%% thanks to 5k amsdu");
        ret += (ret / 4);
        break;
      case RS_MNG_AMSDU_3500B:
        DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                   "_rsMngGetExpectedTpt: adding 12.5%% thanks to 3.5k amsdu");
        ret += (ret / 8);
        break;
      default:
        break;
    }
  }

  return ret;
}

static BOOLEAN _isAvgTptCalcPossible(RS_MNG_WIN_STAT_S* win) {
  return ((win->successCounter >= RS_MNG_RATE_MIN_SUCCESS_TH) ||
          ((win->framesCounter - win->successCounter) >= RS_MNG_RATE_MIN_FAILURE_TH));
}

// rs_get_rate_action
//
// return after every if. so the 'strength' of the conditions are as follows :
//
// Downscale if:
// - the current window success ratio <= 15% || avg tpt for this window is 0
// Upscale if:
// - low and high tpt (tpt for the next/prev rates) are invalid *but* the next rate is valid
// - low_tpt < current_tpt (tpt for prev rate < avg tpt in current window ) *and*
//   next rate is valid although it's tpt isn't
// - high tpt > current tpt
// Stay if:
// - low and high tpt < current tpt
//----- but if none of the above conditions are met. for example - due to invalid high/low tpt :
// if low_tpt > current tpt or low tpt is invalid, but the lower index entry is :
// - if low is valid:
//   Stay if:
//     - the current suceess rate is >= 85% (good enough)
//     - current tpt > expected tpt at [low] (was supposed to do downscale, but current tpt is good
//     enough although the success rate isn't. so we are optomistics)
//   Downscale:
//     - both above are not met
//    - low is invalid
static RS_MNG_ACTION_E _rsMngGetScaleAction(const RS_MNG_STA_INFO_S* staInfo,
                                            const RS_MNG_WIN_STAT_S* currWin, U32 lowerRateIdx,
                                            U32 higherRateIdx) {
  const RS_MNG_TBL_INFO_S* tblInfo = &staInfo->rateTblInfo;
  U32 currTpt;
  U32 lowTpt;
  U32 highTpt;
  RS_MNG_ACTION_E action = RS_MNG_ACTION_STAY;
  enum {
    RS_MNG_SCALE_REASON_BELOW_FORCE_DECREASE,
    RS_MNG_SCALE_REASON_NO_DATA_ON_HIGHER_RATE,
    RS_MNG_SCALE_REASON_HIGHER_RATE_HAS_HIGHER_TPT,
    RS_MNG_SCALE_REASON_CURRENT_RATE_HAS_HIGHEST_TPT,
    RS_MNG_SCALE_REASON_SR_ABOVE_NO_DECREASE_THRESHOLD,
    RS_MNG_SCALE_REASON_LOWER_RATE_TPT_UNKOWN_OR_BETTER,
    RS_MNG_SCALE_REASON_DEFAULT,
  };

  currTpt = currWin->averageTpt;
  lowTpt = ((lowerRateIdx == RS_MNG_INVALID_RATE_IDX) ? RS_MNG_INVALID_VAL
                                                      : tblInfo->win[lowerRateIdx].averageTpt);
  highTpt = ((higherRateIdx == RS_MNG_INVALID_RATE_IDX) ? RS_MNG_INVALID_VAL
                                                        : tblInfo->win[higherRateIdx].averageTpt);

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngGetScaleAction:\ncurrTpt: %d, successRatio: %d,\nlowerRateIdx: %d, lowTpt: "
             "%d,\nhigherRateIdx: %d, highTpt: %d",
             currTpt, currWin->successRatio, lowerRateIdx, lowTpt, higherRateIdx, highTpt);

  // current Success ratio is insufficient or Tpt for the current window is 0 => downscale
  if ((currWin->successRatio <= RS_MNG_PERCENT(RS_MNG_SR_FORCE_DECREASE)) || (0 == currTpt)) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngGetScaleAction: DOWNSCALE due to insufficient success ratio or 0 tpt");
    action = RS_MNG_ACTION_DOWNSCALE;
    goto out;
  }

  // No Tpt data about high/low rate => upscale
  if ((RS_MNG_INVALID_VAL == lowTpt) && (RS_MNG_INVALID_VAL == highTpt) &&
      (RS_MNG_INVALID_RATE_IDX != higherRateIdx)) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngGetScaleAction: UPSCALE due to no data about higher or lower rates");
    action = RS_MNG_ACTION_UPSCALE;
    goto out;
  }

  // if there's no Tpt data about the higerRateIdx but the Tpt for the lowerRateIdx is worse then
  // the curr tpt => Upscale
  if (((RS_MNG_INVALID_VAL == highTpt) && (RS_MNG_INVALID_RATE_IDX != higherRateIdx)) &&
      ((RS_MNG_INVALID_VAL != lowTpt) && (lowTpt < currTpt))) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngGetScaleAction: UPSCALE due to no data on higher rate and lower rate has "
               "worse tpt");
    action = RS_MNG_ACTION_UPSCALE;
    goto out;
  }

  // if higherRateIdx tpt > currTpt => upscale
  if ((RS_MNG_INVALID_VAL != highTpt) && (highTpt > currTpt)) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngGetScaleAction: UPSCALE due to higher rate having higher tpt");
    action = RS_MNG_ACTION_UPSCALE;
    goto out;
  }

  // if Tpt for the higherRateIdx and for LowerRateIdx are both worse then the current Tpt => stay
  if (((RS_MNG_INVALID_VAL != highTpt) && (highTpt < currTpt)) &&
      ((RS_MNG_INVALID_VAL != lowTpt) && (lowTpt < currTpt))) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngGetScaleAction: STAY due to current rate having better tpt that both "
               "higher and lower rates");
    action = RS_MNG_ACTION_STAY;
    goto out;
  }

  // If Tpt for LowerRateIdx > currTpt, or it is unknown but lowerRateIdx is valid
  if (((RS_MNG_INVALID_VAL != lowTpt) && (lowTpt > currTpt)) ||
      ((RS_MNG_INVALID_VAL == lowTpt) && (RS_MNG_INVALID_RATE_IDX != lowerRateIdx))) {
    U32 lowerRateExpectedTpt = _rsMngGetExpectedTpt(staInfo, &rsMngColumns[tblInfo->column],
                                                    _rsMngRateGetBw(&tblInfo->rsMngRate),
                                                    !!(staInfo->mvmsta->agg_tids), lowerRateIdx);

    // if CurrWin success ratio reached the no decrease TH, or currTpt is higher then expected
    // Tpt => stay
    if ((RS_MNG_INVALID_RATE_IDX != lowerRateIdx) &&
        ((currWin->successRatio >= RS_MNG_PERCENT(RS_MNG_SR_NO_DECREASE)) ||
         (currTpt > lowerRateExpectedTpt))) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngGetScaleAction: STAY due to lower rate having expected tpt lower "
                 "than current tpt or current success ratio is above the 'no-decrease' "
                 "threshold. lower rate expected tpt: %d",
                 lowerRateExpectedTpt);
      action = RS_MNG_ACTION_STAY;
      goto out;
    }
    // curr SR is insufficient and either lowTpt is valid and > currTpt or lowerRateIdx is valid
    // => downscale
    else {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngGetScaleAction: DOWNSCALE due to low success ratio or lower rate "
                 "having higher tpt than current rate. lower rate expected tpt: %d",
                 lowerRateExpectedTpt);
      action = RS_MNG_ACTION_DOWNSCALE;
      goto out;
    }
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngGetScaleAction: STAY (default)");
  action = RS_MNG_ACTION_STAY;

out:
  return action;
}

// return: TRUE if there is a better start rate, so need to send LQ command
// newIdx: valid only if the return value is true
//        RS_MNG_INVALID_RATE_IDX - if need to keep using the current index
//        new index to use        - if there is another rate that will provide better tpt / tpc
static RS_MNG_ACTION_E _rsMngSearchBetterStartRate(const RS_MNG_STA_INFO_S* staInfo,
                                                   RS_MNG_WIN_STAT_S* currWin,
                                                   const RS_MNG_RATE_S* currRate, U08* newIdx) {
  U08 lowerSuppRateIdx;
  U08 higherSuppRateIdx;
  RS_MNG_ACTION_E scaleAction;

  lowerSuppRateIdx = _rsMngGetAdjacentRateIdx(staInfo, currRate, GET_LOWER_SUPPORTED_RATE);
  higherSuppRateIdx = _rsMngGetAdjacentRateIdx(staInfo, currRate, GET_HIGHER_SUPPORTED_RATE);

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngSearchBetterStartRate: curr rate idx: %d, lower: %d, higher: %d. supported "
             "rates mask: 0x%x",
             _rsMngRateGetIdx(currRate), lowerSuppRateIdx, higherSuppRateIdx, supportedRatesMsk);

  scaleAction = _rsMngGetScaleAction(staInfo, currWin, lowerSuppRateIdx, higherSuppRateIdx);

  // FMAC TODO - add 'fix' in case we are in MIMO and BT doesn't 'allow' MIMO ? (currently a dead
  // code in fmac)

  // Set the new rate index + tx power reduction, if needed
  switch (scaleAction) {
    case RS_MNG_ACTION_STAY:
      if (staInfo->rsMngState == RS_MNG_STATE_STAY_IN_COLUMN) {
        // TODO - add tpc support
        // rs_tpc_perform(sta, lq_sta, tbl);
      }
      break;
    case RS_MNG_ACTION_DOWNSCALE:
      if (RS_MNG_INVALID_RATE_IDX != lowerSuppRateIdx) {
        // TODO - add tpc
        *newIdx = lowerSuppRateIdx;
      }
      // else - already at the lowest possible rate -> can't downscale
      else {
        scaleAction = RS_MNG_ACTION_STAY;
      }
      break;
    case RS_MNG_ACTION_UPSCALE:
      if (RS_MNG_INVALID_RATE_IDX != higherSuppRateIdx) {
        // TODO - add tpc
        *newIdx = higherSuppRateIdx;
      }
      // else - already at the highest possible rate -> can't upscale
      else {
        scaleAction = RS_MNG_ACTION_STAY;
      }
      break;
    default:
      // TODO add assert?
      break;
  }

  return scaleAction;
}

static U08 _rsMngGetLowestSupportedRate(const RS_MNG_STA_INFO_S* staInfo,
                                        RS_MNG_MODULATION_E modulation, U32 bw,
                                        U16 supportedRates) {
  if (modulation != RS_MNG_MODUL_LEGACY && staInfo->config.bestSuppMode == TLC_MNG_MODE_HE &&
      bw == CHANNEL_WIDTH20 && _rsMngIsDcmSupported(staInfo, modulation == RS_MNG_MODUL_MIMO2)) {
    return RS_MCS_0_HE_ER_AND_DCM;
  }

  return (U08)LSB2ORD(supportedRates);
}
//
// Returns the index of the lowest rate in the given column with expected tpt higher
// than the target tpt.
//
static U08 _rsMngGetBestRate(const RS_MNG_STA_INFO_S* staInfo, const RS_MNG_TBL_INFO_S* activeTbl,
                             const RS_MNG_COL_ELEM_S* col, U32 bw, U32 targetTpt) {
  U16 supportedRates = _rsMngGetSupportedRatesByModeAndBw(staInfo, col->mode, bw);
  U08 rateIdx = _rsMngGetLowestSupportedRate(staInfo, col->mode, bw, supportedRates);
  U32 expectedTpt = 0;

  while (rateIdx != RS_MNG_INVALID_RATE_IDX) {
    expectedTpt = _rsMngGetExpectedTpt(staInfo, col, bw, !!staInfo->mvmsta->agg_tids, rateIdx);

    if (targetTpt <= expectedTpt) {
      break;
    }

    rateIdx = _rsMngGetHigherRateIdx(rateIdx, supportedRates);
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngGetBestRate: best rateIdx %d. targetTpt: %d, new expected tpt: %d", rateIdx,
             targetTpt, expectedTpt);

  return rateIdx;
}

static BOOLEAN _rsMngIsColAllowed(const RS_MNG_STA_INFO_S* staInfo, U32 bw,
                                  const RS_MNG_COL_ELEM_S* nextCol) {
  int i;

  for (i = 0; i < MAX_COLUMN_CHECKS; i++) {
    ALLOW_COL_FUNC_F allowColFunc = nextCol->checks[i];

    if (allowColFunc && !allowColFunc(staInfo, bw, nextCol)) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngIsColAllowed: Function[%d] check failed", i);
      return FALSE;
    }
  }

  return TRUE;
}

// rs_get_next_column
// return column with expected better tpt
// or invalid column if such doesn't exist
//
// flow:
// get next column according to  rs_tx_columns [index].next_columns[].
// if the next column is invalid - continue to next column in the next_columns list
// if already visited the next column - continue to next column in the next_columns list
// if the chosen antenna is not supported - continue to next column in the next_columns list
// if one of the function checks failed - continue to next column in the next_columns list
// if there is no expected throughput in the table - continue to next column in the next_columns
// list

// now we have the next column.
// get expected tpt table according to siso/mimo + BW + /GI/SGI/AGG/SGI+AGG / legacy (one table)
// compare the current tpt with max expected tpt in this table.
// if current tpt >= max.  => continue to next column
//
// else - return the found column id.
//
static RS_MNG_COLUMN_DESC_E _rsMngGetNextColId(const RS_MNG_STA_INFO_S* staInfo,
                                               const RS_MNG_TBL_INFO_S* tblInfo, U32 targetTpt,
                                               U32* bw, U08* rateIdx) {
  const RS_MNG_COL_ELEM_S* currCol = &rsMngColumns[tblInfo->column];
  const RS_MNG_COL_ELEM_S* nextCol;
  RS_MNG_COLUMN_DESC_E nextColId = RS_MNG_COL_INVALID;  // for compilation. won't be used
  int i;

  // Check that the defines' value allow to assume that we can add NON_SHARED_ANT_RFIC_ID to
  // RS_MNG_COL_SISO_ANT_A to get the shared antenna
  BUILD_BUG_ON(!(RS_MNG_COL_SISO_ANT_A + 1 == RS_MNG_COL_SISO_ANT_B));
  BUILD_BUG_ON(!(RS_MNG_COL_SISO_ANT_A_SGI + 1 == RS_MNG_COL_SISO_ANT_B_SGI));
  BUILD_BUG_ON(!(RS_MNG_COL_HE_3_2_SISO_ANT_A + 1 == RS_MNG_COL_HE_3_2_SISO_ANT_B));
  BUILD_BUG_ON(!(RS_MNG_COL_HE_1_6_SISO_ANT_A + 1 == RS_MNG_COL_HE_1_6_SISO_ANT_B));
  BUILD_BUG_ON(!(RS_MNG_COL_HE_0_8_SISO_ANT_A + 1 == RS_MNG_COL_HE_0_8_SISO_ANT_B));

  for (i = 0; i < MAX_NEXT_COLUMNS; i++) {
    nextColId = currCol->nextCols[i];

    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngGetNextColId: Checking nextCol %d (column id %d)",
               i, nextColId);

    if (RS_MNG_COL_INVALID == nextColId) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngGetNextColId: invalid column. next columns are also invalid, so break");
      break;  // if column is invalid, all the following ones will be invalid as well
    }

    if (0 == i && RS_MNG_MODUL_MIMO2 == currCol->mode) {
      nextColId += NON_SHARED_ANT_RFIC_ID;
    }

    if (staInfo->visitedColumns & BIT(nextColId)) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngGetNextColId: This column was already visited. continue");
      continue;
    }

    nextCol = &rsMngColumns[nextColId];
    // when moving to a new column, if it's a non-HT column the bw must be 20. If attempting to
    // leave non-HT, jump to the highest available width, otherwise keep the current bw (bw
    // changes are handled separately)
    *bw = (nextCol->mode == RS_MNG_MODUL_LEGACY
               ? CHANNEL_WIDTH20
               : (currCol->mode == RS_MNG_MODUL_LEGACY ? _rsMngGetMaxChWidth(staInfo)
                                                       : _rsMngRateGetBw(&tblInfo->rsMngRate)));

    if (!_rsMngIsColAllowed(staInfo, *bw, nextCol)) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngGetNextColId: column not allowed. continue");
      continue;
    }

    if ((*rateIdx = _rsMngGetBestRate(staInfo, tblInfo, nextCol, *bw, targetTpt)) ==
        RS_MNG_INVALID_RATE_IDX) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngGetNextColId: currTpt >= maxTpt of potential column. continue");
      continue;
    }

    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngGetNextColId: Found potential column %d, with chosen bw %d and potential "
               "rateIdx %d",
               nextColId, *bw, *rateIdx);

    // Found potential column.
    break;
  }

  if (MAX_NEXT_COLUMNS == i) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngGetNextColId: Couldn't find a better column, staying in this one");
    return RS_MNG_COL_INVALID;
  }

  return nextColId;
}

// rs_switch_to_column
static void _rsMngSetUpSearchColData(RS_MNG_STA_INFO_S* staInfo, RS_MNG_COLUMN_DESC_E nextColId,
                                     U32 bw, U08 rateIdx) {
  RS_MNG_SEARCH_COL_DATA* searchData = &staInfo->searchColData;
  RS_MNG_RATE_S* rsMngRate = &searchData->rsMngRate;
  const RS_MNG_COL_ELEM_S* nextCol = &rsMngColumns[nextColId];

  switch (nextCol->mode) {
    case RS_MNG_MODUL_LEGACY:
      _rsMngRateSetMode(rsMngRate, TLC_MNG_MODE_LEGACY);
      _rsMngRateSetLdpc(rsMngRate, FALSE);
      _rsMngRateSetModulation(rsMngRate, RS_MNG_MODUL_LEGACY);
      break;
    case RS_MNG_MODUL_SISO:
      _rsMngRateSetMode(rsMngRate, staInfo->config.bestSuppMode);
      _rsMngRateSetModulation(rsMngRate, RS_MNG_MODUL_SISO);
      break;
    case RS_MNG_MODUL_MIMO2:
      _rsMngRateSetMode(rsMngRate, staInfo->config.bestSuppMode);
      _rsMngRateSetModulation(rsMngRate, RS_MNG_MODUL_MIMO2);
      break;
    default:
      WARN_ON(1);
  }

  if (nextCol->mode != RS_MNG_MODUL_LEGACY) {
    _rsMngRateSetLdpc(rsMngRate, _rsMngIsLdpcAllowed(staInfo));
  }

  _rsMngRateSetBw(rsMngRate, bw);
  // Set the search rate according to the new column and station info
  _rsMngRateSetGi(rsMngRate, nextCol->gi);
  if (nextCol->mode == RS_MNG_MODUL_SISO && _rsMngIsStbcAllowed(staInfo, rsMngRate)) {
    _rsMngRateSetStbc(rsMngRate, TRUE);
    _rsMngRateSetAnt(rsMngRate, TLC_MNG_CHAIN_A_MSK | TLC_MNG_CHAIN_B_MSK);
  } else {
    _rsMngRateSetStbc(rsMngRate, FALSE);
    _rsMngRateSetAnt(rsMngRate, nextCol->ant);
  }
  _rsMngRateSetIdx(rsMngRate, rateIdx);

  _memclr(&searchData->win, sizeof(searchData->win));
  searchData->column = nextColId;
  searchData->expectedTpt = _rsMngGetExpectedTpt(staInfo, &rsMngColumns[nextColId], bw,
                                                 !!staInfo->mvmsta->agg_tids, rateIdx);
  _rsMngSetVisitedColumn(staInfo, nextColId);

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngSetUpSearchColData: starting new col at rate index %d (visited columns: 0x%X)",
             _rsMngRateGetIdx(rsMngRate), staInfo->visitedColumns);
}

static RS_MNG_COLUMN_DESC_E _rsMngGetMatchingMimoColumn(RS_MNG_COLUMN_DESC_E col) {
  switch (col) {
    case RS_MNG_COL_SISO_ANT_A:
    case RS_MNG_COL_SISO_ANT_B:
      return RS_MNG_COL_MIMO2;
    case RS_MNG_COL_SISO_ANT_A_SGI:
    case RS_MNG_COL_SISO_ANT_B_SGI:
      return RS_MNG_COL_MIMO2_SGI;
    case RS_MNG_COL_HE_3_2_SISO_ANT_A:
    case RS_MNG_COL_HE_3_2_SISO_ANT_B:
      return RS_MNG_COL_HE_3_2_MIMO;
    case RS_MNG_COL_HE_1_6_SISO_ANT_A:
    case RS_MNG_COL_HE_1_6_SISO_ANT_B:
      return RS_MNG_COL_HE_1_6_MIMO;
    case RS_MNG_COL_HE_0_8_SISO_ANT_A:
    case RS_MNG_COL_HE_0_8_SISO_ANT_B:
      return RS_MNG_COL_HE_0_8_MIMO;
    default:
      return RS_MNG_COL_INVALID;
  }
}

static BOOLEAN _rsMngSearchBetterCol(RS_MNG_STA_INFO_S* staInfo, const RS_MNG_TBL_INFO_S* tblInfo,
                                     U32 currTpt) {
  BOOLEAN ret = FALSE;
  RS_MNG_COLUMN_DESC_E nextColId;
  RS_MNG_COLUMN_DESC_E mimoCol;
  U08 currRateIdx = _rsMngRateGetIdx(&tblInfo->rsMngRate);
  U32 successRatio = tblInfo->win[currRateIdx].successRatio;
  // set target_tpt:
  // - if the current success ratio >= 85% -> keep the expected_tpt for this idx
  // - if the success ratio is too low     -> revert to last_tpt (current ?)
  U32 targetTpt = (successRatio <= RS_MNG_PERCENT(RS_MNG_SR_NO_DECREASE))
                      ? currTpt
                      : _rsMngGetExpectedTpt(staInfo, &rsMngColumns[tblInfo->column],
                                             _rsMngRateGetBw(&tblInfo->rsMngRate),
                                             !!(staInfo->mvmsta->agg_tids), currRateIdx);
  U32 bw;
  U08 rateIdx;

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngSearchBetterCol: starting search. currTpt=%d targetTpt=%d", currTpt, targetTpt);

  // return column with expected better throughput, or invalid column if such doesn't exist
  nextColId = _rsMngGetNextColId(staInfo, tblInfo, targetTpt, &bw, &rateIdx);
  if (RS_MNG_COL_INVALID != nextColId) {
    _rsMngSetUpSearchColData(staInfo, nextColId, bw, rateIdx);
    ret = TRUE;
  } else if (staInfo->searchBw != MAX_CHANNEL_BW_INDX) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngSearchBetterCol: checking if bw change could help. new bw %d",
               staInfo->searchBw);

    if ((rateIdx = _rsMngGetBestRate(staInfo, tblInfo, &rsMngColumns[staInfo->stableColumn],
                                     staInfo->searchBw, targetTpt)) != RS_MNG_INVALID_RATE_IDX) {
      nextColId = staInfo->stableColumn;
    } else if ((mimoCol = _rsMngGetMatchingMimoColumn(staInfo->stableColumn)) !=
                   RS_MNG_COL_INVALID &&
               (rateIdx = _rsMngGetBestRate(staInfo, tblInfo, &rsMngColumns[mimoCol],
                                            staInfo->searchBw, targetTpt)) !=
                   RS_MNG_INVALID_RATE_IDX) {
      nextColId = mimoCol;
    }

    if (nextColId != RS_MNG_COL_INVALID &&
        _rsMngIsColAllowed(staInfo, staInfo->searchBw, &rsMngColumns[nextColId])) {
      _rsMngSetUpSearchColData(staInfo, nextColId, staInfo->searchBw, rateIdx);
      ret = TRUE;
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngSearchBetterCol: trying col %d with the new bw",
                 nextColId);
    } else {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngSearchBetterCol: New bw can't improve tpt or isn't allowed, so not trying "
                 "it. col %d, allowed: %d",
                 nextColId,
                 nextColId == RS_MNG_COL_INVALID
                     ? 0
                     : _rsMngIsColAllowed(staInfo, staInfo->searchBw, &rsMngColumns[nextColId]));
    }
  }

  return ret;
}

static BOOLEAN _rsMngShouldStartUpscaleSearchCycle(const RS_MNG_STA_INFO_S* staInfo,
                                                   const RS_MNG_STA_LIMITS_S* staLimits,
                                                   unsigned long timeLastSearch) {
  // isUpscaleSearchCycle here is referring to the type of the previous search cycle.
  // This is here to prevent two consecutive upscale search cycles (i.e. started because of
  // passing the successFramesLimit threshold) within too short a time.
  return staInfo->totalFramesSuccess > staLimits->successFramesLimit &&
         (!staInfo->isUpscaleSearchCycle ||
          time_after(jiffies,
                     timeLastSearch + usecs_to_jiffies(RS_MNG_UPSCALE_SEARCH_CYCLE_MAX_FREQ)));
}

static BOOLEAN _rsMngShouldStartDownscaleSearchCycle(const RS_MNG_STA_INFO_S* staInfo,
                                                     const RS_MNG_STA_LIMITS_S* staLimits,
                                                     unsigned long timeLastSearch) {
  return time_after(jiffies, timeLastSearch + usecs_to_jiffies(staLimits->statsFlushTimeLimit)) ||
         staInfo->totalFramesFailed > staLimits->failedFramesLimit;
}

static BOOLEAN _rsMngShouldStartSearchCycle(const RS_MNG_STA_INFO_S* staInfo,
                                            BOOLEAN* isUpscaleSearchCycle) {
  const RS_MNG_RATE_S* rsMngRate = &staInfo->rateTblInfo.rsMngRate;
  BOOLEAN isNonHt = _rsMngRateGetMode(rsMngRate) == TLC_MNG_MODE_LEGACY;
  const RS_MNG_STA_LIMITS_S* staLimits = &g_rsMngStaModLimits[isNonHt];
  unsigned long timeLastSearch = staInfo->lastSearchCycleEndTimeJiffies;

  if (_rsMngShouldStartUpscaleSearchCycle(staInfo, staLimits, timeLastSearch)) {
    *isUpscaleSearchCycle = TRUE;
    return TRUE;
  }

  if (_rsMngShouldStartDownscaleSearchCycle(staInfo, staLimits, timeLastSearch)) {
    *isUpscaleSearchCycle = FALSE;
    return TRUE;
  }

  return FALSE;
}

static void _rsMngPrepareForBwChangeAttempt(RS_MNG_STA_INFO_S* staInfo,
                                            const RS_MNG_RATE_S* rsMngRate) {
  BOOLEAN isNonHt = _rsMngRateGetMode(rsMngRate) == TLC_MNG_MODE_LEGACY;
  RS_MCS_E mcs;
  U32 bw;
  U32 isMimo;

  staInfo->searchBw = MAX_CHANNEL_BW_INDX;
  if (isNonHt) {
    return;
  }

  mcs = _rsMngRateGetIdx(rsMngRate);
  bw = _rsMngRateGetBw(rsMngRate);
  isMimo = _rsMngRateGetModulation(rsMngRate) == RS_MNG_MODUL_MIMO2;

  if (mcs > g_rsMngDynBwStayMcs[bw][isMimo].highestStayMcs && _rsMngGetMaxChWidth(staInfo) > bw) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngPrepareForBwChangeAttempt: will try higher bandwidth (mcs %d, isMimo %d, "
               "bw %d->%d)",
               mcs, isMimo, bw, bw + 1);
    staInfo->searchBw = bw + 1;
  } else if (mcs < g_rsMngDynBwStayMcs[bw][isMimo].lowestStayMcs) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngPrepareForBwChangeAttempt: will try lower bandwidth (mcs %d, isMimo %d, "
               "bw %d->%d)",
               mcs, isMimo, bw, bw - 1);
    staInfo->searchBw = bw - 1;
  }
}

static void _rsMngSetStayInCol(RS_MNG_STA_INFO_S* staInfo) {
  staInfo->rsMngState = RS_MNG_STATE_STAY_IN_COLUMN;
  staInfo->stableColumn = staInfo->rateTblInfo.column;

  staInfo->totalFramesFailed = 0;
  staInfo->totalFramesSuccess = 0;
  staInfo->lastSearchCycleEndTimeJiffies = jiffies;
  staInfo->txedFrames = 0;
  staInfo->visitedColumns = 0;
}

static BOOLEAN _rsMngTryColumnSwitch(RS_MNG_STA_INFO_S* staInfo, U32 currTpt, BOOLEAN* updateHost) {
  const RS_MNG_TBL_INFO_S* tblInfo = &staInfo->rateTblInfo;

  if (_rsMngSearchBetterCol(staInfo, tblInfo, currTpt)) {
    staInfo->searchBetterTbl = TRUE;
    *updateHost = FALSE;

    return TRUE;
  } else {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngTryColumnSwitch: No potential column found, change state to "
               "RS_MNG_STATE_STAY_IN_COLUMN");
    _rsMngSetStayInCol(staInfo);
    *updateHost = TRUE;

    return FALSE;
  }
}

static BOOLEAN _rsMngStartSearchCycle(RS_MNG_STA_INFO_S* staInfo, U32 currTpt,
                                      BOOLEAN* updateHost) {
  RS_MNG_TBL_INFO_S* tblInfo = &staInfo->rateTblInfo;
  RS_MNG_RATE_S* rsMngRate = &staInfo->rateTblInfo.rsMngRate;

  staInfo->rsMngState = RS_MNG_STATE_SEARCH_CYCLE_STARTED;
  _rsMngSetVisitedColumn(staInfo, tblInfo->column);

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngStartSearchCycle: Moving to state SEARCH_CYCLE_STARTED, upscale search cycle "
             "%d, visited cols bitmask: 0x%X, curr rate: 0x%X",
             staInfo->isUpscaleSearchCycle, staInfo->visitedColumns, rsMngRate->rate.rate_n_flags);

  // If we're in HT/VHT/HE, we may want to test a different bandwidth during the search cycle.
  // According to requirements, this is decided based on the reason the search cycle started
  // (upscale or downscale) and the configuration and mcs when the sycle begins.
  // The new bandwidth will be tested on the column from the start of the search cycle.
  // Do that logic now.
  _rsMngPrepareForBwChangeAttempt(staInfo, rsMngRate);

  return _rsMngTryColumnSwitch(staInfo, currTpt, updateHost);
}

static void _rsMngSwitchToSearchCol(RS_MNG_STA_INFO_S* staInfo) {
  RS_MNG_TBL_INFO_S* tblInfo = &staInfo->rateTblInfo;
  RS_MNG_SEARCH_COL_DATA* searchData = &staInfo->searchColData;

  tblInfo->rsMngRate = searchData->rsMngRate;
  _rsMngClearTblWindows(staInfo);
  tblInfo->win[_rsMngRateGetIdx(&searchData->rsMngRate)] = searchData->win;
  tblInfo->column = searchData->column;
}

static BOOLEAN _rsMngTryScaleWithinColumn(RS_MNG_STA_INFO_S* staInfo, RS_MNG_WIN_STAT_S* currWin,
                                          BOOLEAN* updateHost) {
  RS_MNG_TBL_INFO_S* tblInfo = &staInfo->rateTblInfo;
  RS_MNG_ACTION_E action;
  U08 newIdx = RS_MNG_INVALID_RATE_IDX;

  action = _rsMngSearchBetterStartRate(staInfo, currWin, &tblInfo->rsMngRate, &newIdx);
  if (action == RS_MNG_ACTION_UPSCALE) {
    if (staInfo->rsMngState == RS_MNG_STATE_SEARCH_CYCLE_STARTED ||
        time_after(jiffies, staInfo->lastRateUpscaleTimeJiffies +
                                usecs_to_jiffies(RS_MNG_UPSCALE_MAX_FREQUENCY))) {
      // Rate upscaling could happen both during a search cycle and while in STAY_IN_COLUMN
      // state. When in stay in column the upscaling frequency is limited to no more than once
      // per 100ms. When in search cycle there is no limit because search cycles need to be as
      // short as possible.
      staInfo->lastRateUpscaleTimeJiffies = jiffies;
    } else {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngTryScaleWithinColumn: not upscaling because not enough time passed "
                 "since last upscale (%d usec)",
                 systemTimeGetUsecDiffTime(staInfo->lastRateUpscaleTimeJiffies));
      action = RS_MNG_ACTION_STAY;
    }
  }

  staInfo->tryingRateUpscale = (U08)(action == RS_MNG_ACTION_UPSCALE);

  if (action != RS_MNG_ACTION_STAY) {
    _rsMngRateSetIdx(&tblInfo->rsMngRate, newIdx);
    return TRUE;
  }

  return FALSE;
}

static void _rsMngLookForNextSearchRate(RS_MNG_STA_INFO_S* staInfo, RS_MNG_WIN_STAT_S* currWin,
                                        BOOLEAN* updateHost) {
  // During a search cycle first search for the optimal rate within the current column, and once
  // that is found (i.e. _rsMngTryScaleWithinColumn returns FALSE), search for a better column to
  // switch to.
  if (!_rsMngTryScaleWithinColumn(staInfo, currWin, updateHost)) {
    _rsMngTryColumnSwitch(staInfo, currWin->averageTpt, updateHost);
  }
}

typedef enum _RS_MNG_TPC_ALLOWED_REASON_E {
  RS_MNG_TPC_DISALLOWED_DEBUG_HOOK,
  RS_MNG_TPC_DISALLOWED_SLEEP_DISALLOWED,
  RS_MNG_TPC_DISALLOWED_IN_SEARCH_CYCLE,
  RS_MNG_TPC_DISALLOWED_RATE_IS_NON_HT,
  RS_MNG_TPC_DISALLOWED_RATE_IS_NOT_OPTIMAL,
  RS_MNG_TPC_DISALLOWED_TEST_RATE,
  RS_MNG_TPC_DISALLOWED_AMSDU_INACTIVE,
  RS_MNG_TPC_DISALLOWED_AMSDU_TIME_LIMIT,
  RS_MNG_TPC_ALLOWED,
} RS_MNG_TPC_ALLOWED_REASON_E;

static RS_MNG_TPC_ALLOWED_REASON_E _rsMngTpcAllowed(const RS_MNG_STA_INFO_S* staInfo,
                                                    U32* tpcAllowedData) {
  const RS_MNG_RATE_S* rsMngRate = &staInfo->rateTblInfo.rsMngRate;
  TLC_MNG_MODE_E mode;
  RS_MNG_TX_AMSDU_SIZE_E amsduSize;

  if (!PWR_IS_SLEEP_ALLOWED) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngTpcAllowed: TPC disallowed because sleep disallowed");
    return RS_MNG_TPC_DISALLOWED_SLEEP_DISALLOWED;
  }

  if (staInfo->rsMngState == RS_MNG_STATE_SEARCH_CYCLE_STARTED) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngTpcAllowed: TPC disallowed because in search cycle");
    return RS_MNG_TPC_DISALLOWED_IN_SEARCH_CYCLE;
  }

  mode = _rsMngRateGetMode(rsMngRate);
  if (mode == TLC_MNG_MODE_LEGACY) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngTpcAllowed: TPC disallowed because in non-HT rate");
    return RS_MNG_TPC_DISALLOWED_RATE_IS_NON_HT;
  }

  if (!_rsMngRateIsOptimal(staInfo, rsMngRate)) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngTpcAllowed: TPC disallowed because rate is not optimal");
    *tpcAllowedData = rsMngRate->rate.rate_n_flags;
    return RS_MNG_TPC_DISALLOWED_RATE_IS_NOT_OPTIMAL;
  }

  if (staInfo->tryingRateUpscale) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngTpcAllowed: TPC disallowed because current rate is test rate");
    return RS_MNG_TPC_DISALLOWED_TEST_RATE;
  }

  amsduSize =
      _rsMngAmsduSize(staInfo, mode, _rsMngRateGetBw(rsMngRate), _rsMngRateGetIdx(rsMngRate),
                      _rsMngRateGetGi(rsMngRate), _rsMngRateGetModulation(rsMngRate));
  if (staInfo->amsduSupport && amsduSize != RS_MNG_AMSDU_INVALID) {
    // amsdu is supported in general, and specifically for this rate (i.e. the phy rate of the
    // optimal rate is above the amsdu min phy rate threshold)
    if (staInfo->amsduEnabledSize == RS_MNG_AMSDU_INVALID) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngTpcAllowed: TPC disallowed because amsdu is not yet active");
      return RS_MNG_TPC_DISALLOWED_AMSDU_INACTIVE;
    }

    if (time_before(jiffies,
                    staInfo->lastEnableJiffies + usecs_to_jiffies(RS_MNG_TPC_AMSDU_ENABLE))) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngTpcAllowed: TPC disallowed because not enough time elapsed since "
                 "amsdu enablement. time elapsed: %u",
                 time);
      return RS_MNG_TPC_DISALLOWED_AMSDU_TIME_LIMIT;
    }
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngTpcAllowed: TPC is allowed");
  return RS_MNG_TPC_ALLOWED;
}

typedef enum _RS_MNG_TPC_ACTION {
  RS_MNG_TPC_ACTION_STAY,
  RS_MNG_TPC_ACTION_INCREASE,
  RS_MNG_TPC_ACTION_DECREASE,
  RS_MNG_TPC_ACTION_DISABLE,
} RS_MNG_TPC_ACTION;

static RS_MNG_TPC_ACTION _rsMngTpcGetAction(const RS_MNG_STA_INFO_S* staInfo) {
  const RS_MNG_TPC_TBL_S* tpcTbl = &staInfo->tpcTable;
  U32 currStepSR;
  U08 currStep = tpcTbl->currStep;
  BOOLEAN tpcInactive = currStep == RS_MNG_TPC_INACTIVE;

  if (WARN_ON(!(currStep < RS_MNG_TPC_DISABLED))) {
    return RS_MNG_TPC_ACTION_STAY;
  }

  if (!tpcInactive) {
    currStepSR = tpcTbl->windows[currStep].successRatio;
  } else {
    currStepSR =
        staInfo->rateTblInfo.win[_rsMngRateGetIdx(&staInfo->rateTblInfo.rsMngRate)].successRatio;
  }

  if (!tpcInactive) {
    // if testing is true, then we are now operating on results from a tpc_action_increase test
    // window. In this case we don't want to completely disable tpc even if the current SR is
    // relatively bad. Instead we will just decrease back to the last good step.
    if (currStepSR <= RS_MNG_TPC_SR_DISABLE && !tpcTbl->testing) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngTpcGetAction: DISABLING tpc. currStep = %d, currStepSR = %d", currStep,
                 currStepSR);
      return RS_MNG_TPC_ACTION_DISABLE;
    }
    if (currStepSR <= RS_MNG_TPC_SR_DECREASE) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngTpcGetAction: DECREASING tpc. currStep = %d, currStepSR = %d", currStep,
                 currStepSR);
      return RS_MNG_TPC_ACTION_DECREASE;
    }
  }

  if (tpcInactive || currStep < RS_MNG_TPC_NUM_STEPS - 1) {
    U08 higherStep = (U08)(tpcInactive ? 0 : currStep + 1);
    U32 higherStepSR = tpcTbl->windows[higherStep].successRatio;

    // only attempt to increase power reduction if this hasn't been tried since the last time
    // statistics for the higher step were cleared
    if (currStepSR >= RS_MNG_TPC_SR_INCREASE && higherStepSR == RS_MNG_INVALID_VAL) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngTpcGetAction: INCREASING tpc. currStep = %d, currStepSR = %d", currStep,
                 currStepSR);
      return RS_MNG_TPC_ACTION_INCREASE;
    }
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngTpcGetAction: STAY tpc. currStep = %d, currStepSR = %d", currStep, currStepSR);
  return RS_MNG_TPC_ACTION_STAY;
}

static void _rsMngTpcDoAction(RS_MNG_STA_INFO_S* staInfo, RS_MNG_TPC_ACTION action) {
  RS_MNG_TPC_TBL_S* tpcTbl = &staInfo->tpcTable;

  switch (action) {
    case RS_MNG_TPC_ACTION_INCREASE:
      if (tpcTbl->currStep == RS_MNG_TPC_INACTIVE) {
        tpcTbl->currStep = 0;
      } else {
        tpcTbl->currStep++;
      }
      tpcTbl->testing = TRUE;
      break;
    case RS_MNG_TPC_ACTION_DECREASE:
      if (tpcTbl->currStep > 0) {
        tpcTbl->currStep--;
      } else {
        tpcTbl->currStep = RS_MNG_TPC_INACTIVE;
      }
      tpcTbl->testing = FALSE;
      break;
    case RS_MNG_TPC_ACTION_DISABLE:
      tpcTbl->currStep = RS_MNG_TPC_INACTIVE;
    /* fall through */
    case RS_MNG_TPC_ACTION_STAY:
    /* fall through */
    default:
      tpcTbl->testing = FALSE;
      break;
  }
}

static void _rsMngHandleBwChange(RS_MNG_STA_INFO_S* staInfo) {
  RS_MNG_SEARCH_COL_DATA* searchData = &staInfo->searchColData;
  RS_MNG_RATE_S* prevRate = &staInfo->rateTblInfo.rsMngRate;

  // check if this was a bandwidth search
  if (_rsMngRateGetModulation(&searchData->rsMngRate) != RS_MNG_MODUL_LEGACY &&
      staInfo->stableColumn >= RS_MNG_COL_FIRST_HT_VHT &&
      _rsMngRateGetBw(&searchData->rsMngRate) != _rsMngRateGetBw(prevRate)) {
    staInfo->searchBw = MAX_CHANNEL_BW_INDX;

    // attempting a bandwidth change is always the very last configuration change attempt in a
    // search cycle. mark all columns as visited in order to make sure the search cycle will
    // end.
    staInfo->visitedColumns = (U32)-1;
  }
}

static BOOLEAN _rsMngHandleBtCoex(const RS_MNG_STA_INFO_S* staInfo, RS_MNG_RATE_S* rate,
                                  RS_MNG_COLUMN_DESC_E* col) {
  /*
   * BT-Coex only restricts use of the shared antenna if the rate is a SISO non-STBC rate. In case
   * of MIMO (or SISO with STBC) nothing needs to be done.
   * So check here only if the column uses only the shared antenna.
   */
  if (rsMngColumns[*col].ant != BT_COEX_SHARED_ANT_ID) {
    return FALSE;
  }

  if (!_rsMngRateGetStbc(rate)) {
    U08 singleAnt = rsMngGetSingleAntMsk(staInfo->config.chainsEnabled);

    /*
     * Set the antenna to the non-shared one if possible.
     * In case of SAR limitation, for example, it may be that BT is in a high activity grading
     * and would prefer that wifi use the other antenna, but due to the SAR restriction this is
     * not possible. Because rsMngGetSingleAntMsk always returns the non-shared antenna if it's
     * available (i.e. enabled by configuration and not restricted due to SAR limitation), using
     * the antenna returned by that function handles this type of case too.
     */
    if (_rsMngRateGetAnt(rate) == singleAnt) {
      return FALSE;
    }

    _rsMngRateSetAnt(rate, singleAnt);
    *col ^= 1;
    return TRUE;
  }

  /*
   * Just to avoid reaching here again the next time around, set the column to the non-shared-ant
   * one. In reality there's no difference because with STBC both antennas are used in any case.
   * The xor here does the trick thanks to the compilation asserts near _rsMngSetVisitedColumn.
   */
  *col ^= 1;

  return FALSE;
}

static void _rsMngRateScalePerform(RS_MNG_STA_INFO_S* staInfo, BOOLEAN forceUpdate) {
  RS_MNG_TBL_INFO_S* tblInfo = &staInfo->rateTblInfo;
  RS_MNG_WIN_STAT_S* currWin;
  BOOLEAN updateLmac = forceUpdate, updateHost = FALSE;
  RS_MNG_TPC_ALLOWED_REASON_E tpcAllowed;
  U32 tpcAllowedData = 0;

  if (_rsMngCoexIsLongAggAllowed(staInfo) != staInfo->longAggEnabled) {
    updateLmac = TRUE;
  }

  if (btCoexManagerBtOwnsAnt(staInfo->mvm)) {
    updateLmac |= _rsMngHandleBtCoex(staInfo, &tblInfo->rsMngRate, &tblInfo->column);

    /*
     * If these stats are on a search rate, and that rate just happens to be siso on the shared
     * antenna, change it to the non-shared one too.
     */
    if (staInfo->rsMngState == RS_MNG_STATE_SEARCH_CYCLE_STARTED && staInfo->searchBetterTbl) {
      _rsMngHandleBtCoex(staInfo, &staInfo->searchColData.rsMngRate,
                         &staInfo->searchColData.column);
    }
  }

  if (staInfo->rsMngState == RS_MNG_STATE_SEARCH_CYCLE_STARTED) {
    updateLmac = TRUE;

    if (staInfo->searchBetterTbl) {
      // Last rate sent to lmac was of a test for a new column. Check if it's any good.
      U32 stableRateTpt = tblInfo->win[_rsMngRateGetIdx(&tblInfo->rsMngRate)].averageTpt;

      staInfo->searchBetterTbl = FALSE;
      currWin = &staInfo->searchColData.win;

      _rsMngHandleBwChange(staInfo);

      if (currWin->averageTpt >= stableRateTpt) {
        // Yay! The search column is better! switch over to it.
        DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                   "_rsMngRateScalePerform: search col Tpt %d > lastTpt %d. swapping to "
                   "the search table",
                   currWin->averageTpt, stableRateTpt);

        _rsMngSwitchToSearchCol(staInfo);

        _rsMngLookForNextSearchRate(staInfo, currWin, &updateHost);
      } else {
        // The search col didn't turn out to improve tpt, so search for another new column.
        DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                   "_rsMngRateScalePerform: search col Tpt %d <= lastTpt %d. Searching for "
                   "another column",
                   currWin->averageTpt, stableRateTpt);

        _rsMngTryColumnSwitch(staInfo, stableRateTpt, &updateHost);
      }
    } else {
      // In the middle of searching for an optimal rate within a column. Continue this effort.
      currWin = &tblInfo->win[_rsMngRateGetIdx(&tblInfo->rsMngRate)];

      _rsMngLookForNextSearchRate(staInfo, currWin, &updateHost);
    }
  } else {
    // Not in a search cycle.

    if (!_rsMngTpcIsActive(staInfo)) {
      // If TPC is active (i.e. actually reducing Tx power), we don't try to scale within the
      // column, will only do tpc scaling farther down in this function.

      // If previous rate was an upscale test, need to update lmac regardless of the next rate
      // because even if it's the same rate again, need to send the lq command without the
      // test-rate bit set.
      updateLmac |= staInfo->tryingRateUpscale;

      currWin = &tblInfo->win[_rsMngRateGetIdx(&tblInfo->rsMngRate)];

      // Check if to try changing rate within the column. If so - update lmac. Otherwise -
      // check if it's time to start a new search cycle and act upon that decision
      if (_rsMngTryScaleWithinColumn(staInfo, currWin, &updateHost)) {
        updateLmac = TRUE;
      } else if (_rsMngShouldStartSearchCycle(staInfo, &staInfo->isUpscaleSearchCycle)) {
        updateLmac |= _rsMngStartSearchCycle(staInfo, currWin->averageTpt, &updateHost);
      } else {
        // Note that if the rate didn't really change the host-update function will not send
        // a notification to host regadless of the value of this boolean.
        updateHost = TRUE;
      }
    }
  }

  tpcAllowed = _rsMngTpcAllowed(staInfo, &tpcAllowedData);
  if (tpcAllowed == RS_MNG_TPC_ALLOWED) {
    RS_MNG_TPC_ACTION action;

    if (staInfo->tpcTable.currStep == RS_MNG_TPC_DISABLED) {
      // First time tpc is allowed, start a tpc search cycle (quick movement between tpc
      // steps)
      _rsMngClearWinArr(staInfo->tpcTable.windows, RS_MNG_TPC_NUM_STEPS);
      staInfo->tpcTable.currStep = RS_MNG_TPC_INACTIVE;
      staInfo->rsMngState = RS_MNG_STATE_TPC_SEARCH;

      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngRateScalePerform: starting tpc search");
    }

    action = _rsMngTpcGetAction(staInfo);

    updateLmac |= staInfo->tpcTable.testing;

    if (staInfo->rsMngState == RS_MNG_STATE_TPC_SEARCH) {
      if (action == RS_MNG_TPC_ACTION_STAY) {
        DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                   "_rsMngRateScalePerform: change state to RS_MNG_STATE_STAY_IN_COLUMN");

        staInfo->rsMngState = RS_MNG_STATE_STAY_IN_COLUMN;
        updateHost = TRUE;
      }
    } else {
      if (action == RS_MNG_TPC_ACTION_INCREASE) {
        if (time_after(jiffies, staInfo->lastRateUpscaleTimeJiffies +
                                    usecs_to_jiffies(RS_MNG_UPSCALE_MAX_FREQUENCY))) {
          staInfo->lastRateUpscaleTimeJiffies = jiffies;
        } else {
          DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                     "_rsMngTryScaleWithinColumn: overriding tpc increase. time since "
                     "last increase %dusec",
                     systemTimeGetUsecDiffTime(staInfo->lastRateUpscaleTimeJiffies));
          action = RS_MNG_TPC_ACTION_STAY;
        }
      }
    }

    _rsMngTpcDoAction(staInfo, action);
    updateLmac |= (action != RS_MNG_TPC_ACTION_STAY);
  } else if (staInfo->tpcTable.currStep != RS_MNG_TPC_DISABLED) {
    // TPC was just disallowed. Reset tpc state.

    // If TPC was active or was testing a new step, need to resend an lq with no power reduction
    // and no test-window bit.
    updateLmac |= (_rsMngTpcIsActive(staInfo) || staInfo->tpcTable.testing);

    staInfo->tpcTable.testing = FALSE;
    staInfo->tpcTable.currStep = RS_MNG_TPC_DISABLED;

    if (staInfo->rsMngState == RS_MNG_STATE_TPC_SEARCH) {
      staInfo->rsMngState = RS_MNG_STATE_STAY_IN_COLUMN;
      updateHost = TRUE;
    }
  }

  if (updateLmac) {
    _rsMngUpdateRateTbl(staInfo, staInfo->rsMngState == RS_MNG_STATE_STAY_IN_COLUMN && updateHost);
  }
}

static void rsMngInitAmsdu(RS_MNG_STA_INFO_S* staInfo) {
  if (staInfo->config.amsduSupported && staInfo->config.bestSuppMode >= TLC_MNG_MODE_VHT &&
      _rsMngGetMaxChWidth(staInfo) >= TLC_MNG_CH_WIDTH_40MHZ) {
    staInfo->amsduSupport = TRUE;
  } else {
    staInfo->amsduSupport = FALSE;
  }

  staInfo->amsduEnabledSize = RS_MNG_AMSDU_INVALID;
  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "rsMngInitAmsdu: sta %d, AMSDU support %d",
             _rsMngStaInfoToStaId(staInfo), staInfo->amsduSupport);

  staInfo->lastTrafficLoadStatJiffies = jiffies;
}

static U16 _rsMngAmsduEnumToSize(RS_MNG_TX_AMSDU_SIZE_E value) {
  switch (value) {
    case RS_MNG_AMSDU_3500B:
      return 3500;
    case RS_MNG_AMSDU_5000B:
      return 5000;
    case RS_MNG_AMSDU_6500B:
      return 6500;
    case RS_MNG_AMSDU_8000B:
      return 8000;
    default:
      return 0;
  }
}

static void _rsMngNotifAmsdu(RS_MNG_STA_INFO_S* staInfo, U32 successRatio, U32 trafficLoadPerSec) {
  U16 bitmap = 0;
  U16 size = 0;

  if (staInfo->amsduEnabledSize != RS_MNG_AMSDU_INVALID) {
    // Check which TIDs are not low latency and have an AMSDU in AMPDU session.
    // We activate AMSDU only for those.
    bitmap = (U16)(RS_MNG_AMSDU_VALID_TIDS_MSK & staInfo->mvmsta->agg_tids & staInfo->amsduInAmpdu);
    size = _rsMngAmsduEnumToSize(staInfo->amsduEnabledSize);
    if (size > staInfo->config.maxMpduLen) {
      size = staInfo->config.maxMpduLen;
    }
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngNotifAmsdu: sta %d AMSDU enabled ? %u. bitmap %x, size %d",
             _rsMngStaInfoToStaId(staInfo), staInfo->amsduEnabledSize != RS_MNG_AMSDU_INVALID,
             bitmap, size);

  tlcMngNotifyAmsdu(staInfo, size, bitmap);
}

// Activate fail safe mechanism:
// We may have interoperability issues, where the peer cannot receive correctly
// AMSDUs, in which case we will constantly try to enable AMSDU, and will fall
// back to disabling it.
// In case we consecutively toggle to disable within a FAIL_TIME_THRESHOLD from
// enablement FAIL_CONSEC_THRESHOLD times - blacklist this AMSDU size permanently.
static void _rsMngAmsduFailSafe(RS_MNG_STA_INFO_S* staInfo, unsigned long currentTime) {
  // Since traffic load 1 sec window is not exactly one sec, check to see if we
  // are within its window by comparing the update of the stat timestamp with
  // the enable event.
  // If the "1 sec" window has passed, then the timestamp was updated.
  if (time_before(currentTime, staInfo->lastEnableJiffies +
                                   usecs_to_jiffies(RS_MNG_AMSDU_FAIL_TIME_THRESHOLD)) ||
      staInfo->lastEnableJiffies == staInfo->lastTrafficLoadStatJiffies) {
    staInfo->failSafeCounter++;
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngAmsduFailSafe: Sta %d IOP counter incremented to %d",
               _rsMngStaInfoToStaId(staInfo), staInfo->failSafeCounter);
  } else {
    staInfo->failSafeCounter = 0;
  }

  if (staInfo->failSafeCounter >= RS_MNG_AMSDU_FAIL_CONSEC_THRESHOLD) {
    // disable AMSDU permanently for this AMSDU size
    staInfo->amsduBlacklist |= BIT(staInfo->amsduEnabledSize);
    staInfo->failSafeCounter = 0;
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngAmsduFailSafe: sta %d, blacklisted size %d",
               _rsMngStaInfoToStaId(staInfo), staInfo->amsduEnabledSize);

    // if all disabled - save some future processing
    if (staInfo->amsduBlacklist == RS_MNG_AMSDU_SIZE_ALL) {
      staInfo->amsduSupport = FALSE;
    }
  }
}

static void _rsMngAmsduChanged(RS_MNG_STA_INFO_S* staInfo, RS_MNG_TX_AMSDU_SIZE_E size,
                               unsigned long time, U32 successRatio, U32 trafficLoadPerSec) {
  if (size < staInfo->amsduEnabledSize) {
    _rsMngAmsduFailSafe(staInfo, time);
  }
  staInfo->amsduEnabledSize = size;
  if (size != RS_MNG_AMSDU_INVALID) {
    staInfo->lastEnableJiffies = time;
  }
  _rsMngNotifAmsdu(staInfo, successRatio, trafficLoadPerSec);
}

/*
 * Returns whether amsdu state changed from enabled to disabled or vice-versa (size changes are not
 * considered a state change in this respect)
 */
static BOOLEAN _rsMngCollectAmsduTlcData(RS_MNG_STA_INFO_S* staInfo, U32 baTxed, U32 baAcked,
                                         U16 trafficLoad) {
  RS_MNG_TX_AMSDU_SIZE_E size;
  unsigned long elapsedTime, currentTime, trafficLoadPerSec, successRatio;
  BOOLEAN ret = FALSE;
  const U32 baTxedUnnormalized = baTxed;
  RS_MNG_RATE_S* rsMngRate = &staInfo->rateTblInfo.rsMngRate;

  currentTime = jiffies;
  elapsedTime = jiffies_to_usecs(currentTime - staInfo->lastTrafficLoadStatJiffies) >> 10;
  elapsedTime = elapsedTime ?: 1;

  staInfo->trafficLoad += trafficLoad;
  size = _rsMngAmsduSize(staInfo, _rsMngRateGetMode(rsMngRate), _rsMngRateGetBw(rsMngRate),
                         _rsMngRateGetIdx(rsMngRate), _rsMngRateGetGi(rsMngRate),
                         _rsMngRateGetModulation(rsMngRate));

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngCollectAmsduTlcData: baTxed %d, baAcked %d, new trafficLoad %d, time from "
             "last traffic load check %d, trafficLoad %d potential size %d",
             baTxed, baAcked, trafficLoad, elapsedTime, staInfo->trafficLoad, size);

  // Normalize the baTxed to be able to divide by it without explicit zero check
  // baAcked is zero as well in this case, so it has no functionality impact
  baTxed = baTxed ?: 1;

  if (staInfo->amsduEnabledSize == RS_MNG_AMSDU_INVALID) {
    // AMSDU TLC traffic load works in a window of 1 second
    if (elapsedTime < 1000) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngCollectAmsduTlcData: amsdu disabled, but elapsedTime less than one second");
      return FALSE;
    }

    // This div will only be done 1 time in a second, if AMSDU is supported - not that expensive
    successRatio = (baAcked * 128) / baTxed;
    // normalize the traffic load to a 1 second window
    trafficLoadPerSec = (staInfo->trafficLoad << 10) / elapsedTime;

    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngCollectAmsduTlcData: amsdu disabled. successRatio %d, trafficLoadPerSec %d",
               successRatio, trafficLoadPerSec);

    if (successRatio > RS_MNG_AMSDU_SR_ENABLE_THRESHOLD &&
        trafficLoadPerSec > RS_MNG_AMSDU_TL_ENABLE_THRESHOLD && size != RS_MNG_AMSDU_INVALID) {
      // AMSDU is disabled and should be enabled
      _rsMngAmsduChanged(staInfo, size, currentTime, successRatio, trafficLoadPerSec);
      ret = TRUE;
    }
  } else {
    successRatio = (baAcked * 128) / baTxed;
    // normalize the traffic load to a 1 second window
    trafficLoadPerSec = (staInfo->trafficLoad << 10) / elapsedTime;

    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "_rsMngCollectAmsduTlcData: amsdu is enabled with size %d, successRatio %d, "
               "trafficLoadPerSec  %d",
               staInfo->amsduEnabledSize, successRatio, trafficLoadPerSec);

    // AMSDU is enabled - check if we should disable or up/down-grade.
    // Note that we upgrade here without waiting for a second to pass since the last change in
    // order to recover as fast as possible from a momentary rate reduction (at the price of
    // perhaps triggering the failsafe mechanism too early).
    if (size != staInfo->amsduEnabledSize) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngCollectAmsduTlcData: new size is different than current size. updating");
      _rsMngAmsduChanged(staInfo, size, currentTime, successRatio, trafficLoadPerSec);
      return size == RS_MNG_AMSDU_INVALID;
    }

    if (successRatio < RS_MNG_AMSDU_SR_DISABLE_THRESHOLD && baTxedUnnormalized) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngCollectAmsduTlcData: success ratio too low. Disabling amsdu");
      _rsMngAmsduChanged(staInfo, RS_MNG_AMSDU_INVALID, currentTime, successRatio,
                         trafficLoadPerSec);
      return TRUE;
    }

    // AMSDU TLC traffic load works in a window of 1 second
    if (elapsedTime < 1000) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngCollectAmsduTlcData: elapsed time less than a second, skipping TL check");
      return FALSE;
    }

    if (trafficLoadPerSec < RS_MNG_AMSDU_TL_DISABLE_THRESHOLD) {
      staInfo->trafficLoad = trafficLoadPerSec;
      _rsMngAmsduChanged(staInfo, RS_MNG_AMSDU_INVALID, currentTime, successRatio,
                         trafficLoadPerSec);
      ret = TRUE;
    }
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngCollectAmsduTlcData: resetting trafficLoad counter");

  staInfo->trafficLoad = 0;
  staInfo->lastTrafficLoadStatJiffies = currentTime;

  return ret;
}

/**
 * Update the success/failure sliding window
 *
 * We keep a sliding window of the last 64 packets transmitted
 * at this rate.  window->data contains the bitmask of
 * successful packets.
 */
// TODO - handle the position of the acks/fails in the window. for now - we only care about the
// counters (how many failed/succeeded regardless of when)
static BOOLEAN _rsMngCollectTlcData(RS_MNG_STA_INFO_S* staInfo, int attempts, int successes) {
  RS_MNG_WIN_STAT_S* window;
  const RS_MNG_RATE_S* rsMngRate = &staInfo->rateTblInfo.rsMngRate;
  U32 expectedTpt;
  U32 tmpSumFrames = attempts;
  U32 tmpExtraFrames;
  U32 windowSize;
  BOOLEAN ret = TRUE;

  if (staInfo->searchBetterTbl) {
    RS_MNG_SEARCH_COL_DATA* searchData = &staInfo->searchColData;

    window = &searchData->win;
    expectedTpt = searchData->expectedTpt;
    rsMngRate = &searchData->rsMngRate;
  } else if (_rsMngTpcIsActive(staInfo)) {
    window = &staInfo->tpcTable.windows[staInfo->tpcTable.currStep];
    // In TPC we don't care about expected/average tpt, only about success ratio. Initialize
    // this var here anyways so the compiler won't complain.
    expectedTpt = 0;
  } else {
    RS_MNG_TBL_INFO_S* tblInfo = &staInfo->rateTblInfo;
    U32 rateIdx = _rsMngRateGetIdx(&tblInfo->rsMngRate);

    window = &tblInfo->win[rateIdx];
    expectedTpt = _rsMngGetExpectedTpt(staInfo, &rsMngColumns[tblInfo->column],
                                       _rsMngRateGetBw(&tblInfo->rsMngRate),
                                       !!(staInfo->mvmsta->agg_tids), rateIdx);
  }

  windowSize = _rsMngRateGetMode(rsMngRate) != TLC_MNG_MODE_LEGACY ? RS_MNG_MAX_WINDOW_SIZE
                                                                   : RS_MNG_MAX_WINDOW_SIZE_NON_HT;

  tmpSumFrames += window->framesCounter;

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngCollectTlcData: old data: frames: %d, success: %d",
             window->framesCounter, window->successCounter);

  if (attempts >= windowSize) {
    window->successCounter = (windowSize * successes) / attempts;
    window->framesCounter = windowSize;
  } else if (tmpSumFrames > windowSize) {
    /***** Calculate window->successCounter *****/
    // TODO - replace with good algorithem. for now this is a workaround
    tmpExtraFrames = (tmpSumFrames - windowSize);

    // 1. calculated the ratio of window->successCounter in window->framesCounter and substruct
    // the matching part of the frames substructed
    //    to avoid exceeding the MAX window size.
    window->successCounter -=
        (tmpExtraFrames * window->successCounter) / MAX(1, window->framesCounter);

    // 2. add the successes to window->successCounter, as long as it dosn't exceed the max
    // window size
    window->successCounter = min_t(U32, (window->successCounter + successes), windowSize);

    window->framesCounter = windowSize;
  } else {
    if (!WARN_ON(!((window->successCounter + successes) <= windowSize))) {
      window->successCounter += successes;
      window->framesCounter += attempts;
    }
  }

  // Calculate current success ratio
  if (window->framesCounter > 0) {
    window->successRatio = (128 * window->successCounter) / window->framesCounter;

    // Calculate average throughput, if we have enough history.
    if (_isAvgTptCalcPossible(window)) {
      window->averageTpt = ((window->successRatio * expectedTpt) + 64) / 128;
    } else {
      window->averageTpt = RS_MNG_INVALID_VAL;
      ret = FALSE;
    }
  } else {
    window->successRatio = RS_MNG_INVALID_VAL;
    window->averageTpt = RS_MNG_INVALID_VAL;
    ret = FALSE;
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngCollectTlcData: new data: framesCounter: %d, successCounter: %d, "
             "successRatio: %d/128, avg Tpt: %d, expected Tpt: %d",
             window->framesCounter, window->successCounter, window->successRatio,
             window->averageTpt, expectedTpt);

  return ret;
}

static U16 _rsMngGetCurrentThreshold(const RS_MNG_STA_INFO_S* staInfo) {
  return (U16)(_rsMngIsTestWindow(staInfo) ? RS_MNG_UPSCALE_AGG_FRAME_COUNT : RS_STAT_THOLD);
}

static BOOLEAN _rsMngUpdateGlobalStats(RS_MNG_STA_INFO_S* staInfo, TLC_STAT_COMMON_API_S* stats) {
  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "_rsMngUpdateGlobalStats: tpc stats: no-tpc %u, step1 %u, step2 %u, step3 %u, step4 "
             "%u, step5 %u",
             g_rsMngTpcStats.noTpc, g_rsMngTpcStats.step[0], g_rsMngTpcStats.step[1],
             g_rsMngTpcStats.step[2], g_rsMngTpcStats.step[3], g_rsMngTpcStats.step[4]);

  if (staInfo->rsMngState == RS_MNG_STATE_STAY_IN_COLUMN && !_rsMngIsTestWindow(staInfo)) {
    BOOLEAN isNonHt = _rsMngRateGetMode(&staInfo->rateTblInfo.rsMngRate) == TLC_MNG_MODE_LEGACY;

    staInfo->totalFramesSuccess += stats->acked[0] + stats->acked[1];
    staInfo->totalFramesFailed +=
        stats->txed[0] - stats->acked[0] + stats->txed[1] - stats->acked[1];
    staInfo->txedFrames += stats->txed[0] + stats->txed[1];

    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "Total frames succeeded: %d, total frames failed: %d",
               staInfo->totalFramesSuccess, staInfo->totalFramesFailed);

    // If we have been in this column long enough (regardless of the fail/success rate or time
    // elapsed) - start the statistics calculation from scratch
    if (staInfo->txedFrames >= g_rsMngStaModLimits[isNonHt].clearTblWindowsLimit) {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "_rsMngUpdateGlobalStats: Stayed in same column for a long time. Clear "
                 "table windows. modulation counter was %d",
                 staInfo->txedFrames);

      staInfo->txedFrames = 0;
      _rsMngClearTblWindows(staInfo);
    }
  }

  staInfo->framesSinceLastRun += stats->txed[0] + stats->txed[1];
  if (staInfo->framesSinceLastRun >= _rsMngGetCurrentThreshold(staInfo)) {
    staInfo->framesSinceLastRun = 0;
    return TRUE;
  }

  return FALSE;
}

static void tlcStatUpdateHandler(RS_MNG_STA_INFO_S* staInfo, TLC_STAT_COMMON_API_S* stats,
                                 struct iwl_mvm* mvm, struct ieee80211_sta* sta, int tid,
                                 bool is_ndp) {
  BOOLEAN forceLmacUpdate = FALSE;
  int i;

  if (!staInfo->enabled) {
    // Could happen if statistics from lmac were sent while umac was handling remove station.
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "tlcStatUpdateHandler: received stats for invalid station %d. Ignoring", i);
    return;
  }

  if (!(stats->txed[0] || stats->txed[1])) {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "tlcStatUpdateHandler: no new info for station %u, skipping it", i);
    return;
  }

  // TODO: optimize, no need for array
  staInfo->amsduInAmpdu = 0;
  for (i = 0; i < IWL_MAX_TID_COUNT; i++)
    if (staInfo->mvmsta->tid_data[i].amsdu_in_ampdu_allowed) {
      staInfo->amsduInAmpdu |= BIT(i);
    }

  if (!_rsMngIsTestWindow(staInfo) && staInfo->amsduSupport && staInfo->mvmsta->agg_tids &&
      staInfo->amsduInAmpdu) {
    forceLmacUpdate =
        _rsMngCollectAmsduTlcData(staInfo, stats->baTxed, stats->baAcked, stats->trafficLoad);
  }

  if (_rsMngAreAggsSupported(staInfo->config.bestSuppMode)) {
    // find all tids such that data was sent on them but aggregations weren't opened for them
    // yet
    if (tid < IWL_MAX_TID_COUNT && !is_ndp) {
      iwl_start_agg(mvm, sta, tid);
    }
  }

  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
             "tlcStatUpdateHandler: Received statistics for sta %d, txed[0]: %d, acked[0]: %d, "
             "tids: 0x%x",
             i, stats->txed[0], stats->acked[0], stats->tids);

  if (!staInfo->ignoreNextTlcNotif && !staInfo->fixedRate) {
    BOOLEAN doRateScale = _rsMngUpdateGlobalStats(staInfo, stats);
    doRateScale &= _rsMngCollectTlcData(staInfo, stats->txed[0], stats->acked[0]);

    if (doRateScale) {
      _rsMngRateScalePerform(staInfo, forceLmacUpdate);
    } else {
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
                 "tlcStatUpdateHandler: not performing rate scaling. #frames since last rate "
                 "scale perform: %d",
                 staInfo->framesSinceLastRun);
    }
  } else {
    DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO,
               "tlcStatUpdateHandler: ignoring notification. fixed rate: 0x%x", staInfo->fixedRate);
    staInfo->ignoreNextTlcNotif = FALSE;
  }
}

/*********************************************************************/
/*      External Functions + funcs used by them                      */
/*********************************************************************/

static RS_MNG_COLUMN_DESC_E _rsMngGetColByRate(RS_MNG_RATE_S* rsMngRate) {
  RS_MNG_GI_E gi = _rsMngRateGetGi(rsMngRate);
  U08 ant = _rsMngRateGetAnt(rsMngRate);
  RS_MNG_COLUMN_DESC_E colId = RS_MNG_COL_INVALID;

  switch (_rsMngRateGetModulation(rsMngRate)) {
    case RS_MNG_MODUL_LEGACY:
      switch (ant) {
        case TLC_MNG_CHAIN_A_MSK:
          colId = RS_MNG_COL_NON_HT_ANT_A;
          break;
        case TLC_MNG_CHAIN_B_MSK:
          colId = RS_MNG_COL_NON_HT_ANT_B;
          break;
        default:
          DBG_PRINTF(UT, TLC_OFFLOAD_DBG, ERROR, "invalid antenna Msk 0x%x for legacy rate",
                     _rsMngRateGetAnt(rsMngRate));
          break;
      }
      break;
    case RS_MNG_MODUL_SISO:
      switch (gi) {
        case HT_VHT_NGI:
          colId = RS_MNG_COL_SISO_ANT_A;
          break;
        case HT_VHT_SGI:
          colId = RS_MNG_COL_SISO_ANT_A_SGI;
          break;
        case HE_3_2_GI:
          colId = RS_MNG_COL_HE_3_2_SISO_ANT_A;
          break;
        case HE_1_6_GI:
          colId = RS_MNG_COL_HE_1_6_SISO_ANT_A;
          break;
        case HE_0_8_GI:
          colId = RS_MNG_COL_HE_0_8_SISO_ANT_A;
          break;
      }

      if (ant == TLC_MNG_CHAIN_B_MSK) {
        // This works thanks to the compilation asserts near _rsMngSetVisitedColumn
        colId ^= 1;
      }
      break;
    case RS_MNG_MODUL_MIMO2:
      switch (gi) {
        case HT_VHT_NGI:
          colId = RS_MNG_COL_MIMO2;
          break;
        case HT_VHT_SGI:
          colId = RS_MNG_COL_MIMO2_SGI;
          break;
        case HE_3_2_GI:
          colId = RS_MNG_COL_HE_3_2_MIMO;
          break;
        case HE_1_6_GI:
          colId = RS_MNG_COL_HE_1_6_MIMO;
          break;
        case HE_0_8_GI:
          colId = RS_MNG_COL_HE_0_8_MIMO;
          break;
      }
      break;
    default:
      DBG_PRINTF(UT, TLC_OFFLOAD_DBG, ERROR, "invalid modulation %d",
                 _rsMngRateGetModulation(rsMngRate));
  }

  return colId;
}

static void _rsMngSetInitRate(RS_MNG_STA_INFO_S* staInfo, RS_MNG_RATE_S* rsMngRate) {
  U16 nonHtRates = staInfo->config.nonHt;
  TLC_MNG_MODE_E mode = staInfo->config.bestSuppMode;

  _rsMngRateSetMode(rsMngRate, mode);
  if (mode == TLC_MNG_MODE_LEGACY) {
    _rsMngRateSetModulation(rsMngRate, RS_MNG_MODUL_LEGACY);
    _rsMngRateSetLdpc(rsMngRate, FALSE);
  } else {
    _rsMngRateSetModulation(rsMngRate, RS_MNG_MODUL_SISO);
    _rsMngRateSetLdpc(rsMngRate, _rsMngIsLdpcAllowed(staInfo));
  }

  _rsMngRateSetBw(rsMngRate, CHANNEL_WIDTH20);

  if (mode == TLC_MNG_MODE_HE) {
    _rsMngRateSetGi(rsMngRate, HE_3_2_GI);
  } else {
    _rsMngRateSetGi(rsMngRate, HT_VHT_NGI);
  }
  _rsMngRateSetBfer(rsMngRate, FALSE);

  if (mode > TLC_MNG_MODE_LEGACY && _rsMngIsStbcAllowed(staInfo, rsMngRate)) {
    _rsMngRateSetStbc(rsMngRate, TRUE);
    _rsMngRateSetAnt(rsMngRate, rsMngGetDualAntMsk());
  } else {
    _rsMngRateSetStbc(rsMngRate, FALSE);
    _rsMngRateSetAnt(rsMngRate, rsMngGetSingleAntMsk(staInfo->config.chainsEnabled));
  }

  if (mode > TLC_MNG_MODE_LEGACY) {
    U08 idx =
        (U08)(_rsMngGetSuppRatesSameMode(staInfo, rsMngRate) & BIT(RS_MCS_3) ? RS_MCS_3 : RS_MCS_0);

    _rsMngRateSetIdx(rsMngRate, idx);
  } else {
    if (LSB2ORD(nonHtRates) > RS_NON_HT_RATE_CCK_LAST) {
      // 11a
      _rsMngRateSetIdx(rsMngRate, RS_NON_HT_RATE_OFDM_24M);
    } else if (MSB2ORD(nonHtRates) > RS_NON_HT_RATE_CCK_LAST) {
      // 11g
      _rsMngRateSetIdx(rsMngRate, RS_NON_HT_RATE_OFDM_18M);
    } else {
      // 11b
      _rsMngRateSetIdx(rsMngRate, RS_NON_HT_RATE_CCK_5_5M);
    }

    if (!(BIT(_rsMngRateGetIdx(rsMngRate)) & nonHtRates)) {
      // we don't support the mean rate as defined in the SAS,
      // so just start from the lowest supported rate.
      _rsMngRateSetIdx(rsMngRate, (U08)LSB2ORD(nonHtRates));
    }
  }
}

static void rsMngInitTlcTbl(RS_MNG_STA_INFO_S* staInfo, RS_MNG_TBL_INFO_S* tblInfo) {
  _rsMngSetInitRate(staInfo, &tblInfo->rsMngRate);

  tblInfo->column = _rsMngGetColByRate(&(tblInfo->rsMngRate));
  staInfo->stableColumn = tblInfo->column;

  _rsMngUpdateRateTbl(staInfo, TRUE);
}

static void rsMngResetStaInfo(struct iwl_mvm* mvm, struct ieee80211_sta* sta,
                              struct iwl_mvm_sta* mvmsta, RS_MNG_STA_INFO_S* staInfo,
                              BOOLEAN reconfigure) {
  U32 fixedRate = staInfo->fixedRate;
  U16 aggDurationLimit = staInfo->aggDurationLimit;
  U08 amsduInAmpdu = staInfo->amsduInAmpdu;
  BOOLEAN longAggEnabled = staInfo->longAggEnabled;

  _memclr(staInfo, sizeof(*staInfo));

  staInfo->mvm = mvm;
  staInfo->sta = sta;
  staInfo->mvmsta = mvmsta;
  staInfo->lastSearchCycleEndTimeJiffies = jiffies;
  staInfo->lastRateUpscaleTimeJiffies = jiffies;
  staInfo->lastEnableJiffies = jiffies;

  if (reconfigure) {
    staInfo->fixedRate = fixedRate;
    staInfo->amsduInAmpdu = amsduInAmpdu;
    staInfo->longAggEnabled = longAggEnabled;
    staInfo->aggDurationLimit = aggDurationLimit;
  } else {
    staInfo->aggDurationLimit = RS_MNG_AGG_DURATION_LIMIT;
  }

  // aggState IDLE is 0. so memclear sets it
  staInfo->rsMngState = RS_MNG_STATE_STAY_IN_COLUMN;

  _rsMngRateInvalidate(&staInfo->rateTblInfo.rsMngRate);
  _rsMngClearTblWindows(staInfo);
  _rsMngClearWinArr(staInfo->tpcTable.windows, RS_MNG_TPC_NUM_STEPS);

  staInfo->tpcTable.currStep = RS_MNG_TPC_DISABLED;
  staInfo->staBuffSize = RS_MNG_AGG_FRAME_CNT_LIMIT;
  staInfo->amsduEnabledSize = RS_MNG_AMSDU_INVALID;
  staInfo->amsduSupport = FALSE;
  staInfo->failSafeCounter = 0;
  staInfo->amsduBlacklist = 0;
}

static bool _tlcMngConfigValid(TLC_MNG_CONFIG_PARAMS_CMD_API_S* params) {
  U08 chainsEnabled = params->chainsEnabled;

  // no valid chain is selected
  if (WARN_ON(!(chainsEnabled && (chainsEnabled & rsMngGetDualAntMsk()) == chainsEnabled))) {
    return false;
  }

  // at least one non-HT rate MUST be valid
  if (WARN_ON(!params->nonHt)) {
    return false;
  }

  if (params->bestSuppMode == TLC_MNG_MODE_LEGACY) {
    // check that the no non-HT rates are set
    if (WARN_ON(params->mcs[TLC_MNG_NSS_1][0] || params->mcs[TLC_MNG_NSS_1][1] ||
                params->mcs[TLC_MNG_NSS_2][0] || params->mcs[TLC_MNG_NSS_2][1])) {
      return false;
    }

    // all config flags make sense only in HT/VHT/HE scenarios, and non-ht rates can only
    // support 20MHz bandwidth
    if (WARN_ON(!(!params->configFlags && params->maxChWidth == TLC_MNG_CH_WIDTH_20MHZ))) {
      return false;
    }
  } else {  // HT/VHT/HE
    // check that there are valid rates for the best supported mode
    if (WARN_ON(!(params->mcs[TLC_MNG_NSS_1][0]))) {
      return false;
    }

    if (chainsEnabled != rsMngGetDualAntMsk()) {
      // the following flags all require 2 antennas to be used
      if (WARN_ON(params->configFlags &
                  (TLC_MNG_CONFIG_FLAGS_STBC_MSK | TLC_MNG_CONFIG_FLAGS_HE_STBC_160MHZ_MSK |
                   TLC_MNG_CONFIG_FLAGS_HE_DCM_NSS_2_MSK))) {
        return false;
      }

      // only one chain => can't do mimo
      if (WARN_ON(params->mcs[TLC_MNG_NSS_2][0] || params->mcs[TLC_MNG_NSS_2][1])) {
        return false;
      }
    }

    if (params->bestSuppMode == TLC_MNG_MODE_HT) {
      if (WARN_ON(!((params->bestSuppMode == TLC_MNG_MODE_HT &&
                     params->maxChWidth <= TLC_MNG_CH_WIDTH_40MHZ) ||
                    params->maxChWidth < TLC_MNG_CH_WIDTH_MAX))) {
        return false;
      }
    }

    if (params->bestSuppMode < TLC_MNG_MODE_HE) {
      // the following flags are for HE only
      if (WARN_ON(params->configFlags &
                  (TLC_MNG_CONFIG_FLAGS_HE_STBC_160MHZ_MSK | TLC_MNG_CONFIG_FLAGS_HE_DCM_NSS_1_MSK |
                   TLC_MNG_CONFIG_FLAGS_HE_DCM_NSS_2_MSK |
                   TLC_MNG_CONFIG_FLAGS_HE_BLOCK_2X_LTF_MSK))) {
        return false;
      }
    } else {
      if (WARN_ON(params->sgiChWidthSupport)) {
        return false;
      }
    }
  }

  if (WARN_ON(!(params->amsduSupported <= TLC_AMSDU_SUPPORTED))) {
    return false;
  }

  return true;
}

// rs_initialize_lq
//
// Initialize a station's hardware rate table
// The uCode's station table contains a table of fallback rates
// for automatic fallback during transmission.
//
// NOTE: This sets up a default set of values.  These will be replaced later
static void _rsMngTlcInit(RS_MNG_STA_INFO_S* staInfo) {
  RS_MNG_TBL_INFO_S* tblInfo = &staInfo->rateTblInfo;

  rsMngInitTlcTbl(staInfo, tblInfo);

  // Start in search cycle state in order allow quick convergence on the optimal rate.
  staInfo->rsMngState = RS_MNG_STATE_SEARCH_CYCLE_STARTED;
  _rsMngSetVisitedColumn(staInfo, tblInfo->column);
  _rsMngPrepareForBwChangeAttempt(staInfo, &staInfo->rateTblInfo.rsMngRate);
  DBG_PRINTF(UT, TLC_OFFLOAD_DBG, INFO, "_rsMngTlcInit: starting at column %d", tblInfo->column);
}

/**********************************************************************/
/*                        Cmd Handlers                                */
/**********************************************************************/

static void cmdHandlerTlcMngConfig(struct iwl_mvm* mvm, struct ieee80211_sta* sta,
                                   struct iwl_mvm_sta* mvmsta, RS_MNG_STA_INFO_S* staInfo,
                                   TLC_MNG_CONFIG_PARAMS_CMD_API_S* config, BOOLEAN reconfigure) {
  if (!_tlcMngConfigValid(config)) {
    return;
  }

  // Check if this a reconfiguration of an existing station
  if (staInfo->enabled && reconfigure) {
    // Switching between non-HT/HT/VHT/HE requires completely de-associating before
    // reassociating with the new mode. That being said, windows driver always sends a tlc
    // config command with only non-HT mode when first adding a station, and then updates it to
    // the correct mode after association. Since the switch from non-HT to HT/VHT/HE doesn't
    // require any extra processing here (no aggregation state to possibly change etc.), this is
    // allowed even though it's weird.
    if (WARN_ON(!(config->bestSuppMode >= staInfo->config.bestSuppMode))) {
      return;
    }
  }

  rsMngResetStaInfo(mvm, sta, mvmsta, staInfo, staInfo->enabled && reconfigure);
  BUILD_BUG_ON(sizeof(staInfo->config) != sizeof(*config));
  memcpy(&staInfo->config, config, sizeof(staInfo->config));

  rsMngInitAmsdu(staInfo);

  // send LQ command with basic rates table
  _rsMngTlcInit(staInfo);

  staInfo->enabled = TRUE;
}
