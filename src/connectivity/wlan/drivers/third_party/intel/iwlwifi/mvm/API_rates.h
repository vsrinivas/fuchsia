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
* File Name : API_rates.h
*
*
* Description : uCode\Driver Command & Response API Definitions
****************************************************************************/

#ifndef API_RATES_H
#define API_RATES_H

/**
  @defgroup GroupRates Rates Masks and Indices
  @ingroup GroupDataPath
  @{
 */


// *****************************************
// * channel width code/index from 20Mhz ... 160Mhz
// *****************************************
#define CHANNEL_WIDTH20        0
#define CHANNEL_WIDTH40        1
#define CHANNEL_WIDTH80        2
#define CHANNEL_WIDTH160       3
#define MAX_CHANNEL_BW_INDX    4 // used to initialize const arrays

#define MAX_NSS                2 // max number of spatial streams

#define CHANNEL_WIDTH_MSK_API_D_VER_1    1
#define CHANNEL_WIDTH_MSK_API_D_VER_2    3

// *****************************************
// * ofdm & cck rate codes
// *****************************************
/**
  @defgroup GroupRatesCodes OFDM and CCK Rate Codes
  @ingroup GroupRates
  @{
 */
#define R_6M 0xd
#define R_9M 0xf
#define R_12M 0x5
#define R_18M 0x7
#define R_24M 0x9
#define R_36M 0xb
#define R_48M 0x1
#define R_54M 0x3

#define R_1M 0xa
#define R_2M 0x14
#define R_5_5M 0x37
#define R_11M 0x6e
/**@} GroupRatesCodes */


// *****************************************
// * ofdm & cck indices
// *****************************************
/**
  @defgroup GroupRatesIndices OFDM and CCK Rate Indices
  @ingroup GroupRates
  @{
 */
#define R_6M_INDEX 0x6
#define R_9M_INDEX 0x7
#define R_12M_INDEX 0x2
#define R_18M_INDEX 0x3
#define R_24M_INDEX 0x4
#define R_36M_INDEX 0x5
#define R_48M_INDEX 0x0
#define R_54M_INDEX 0x1

#define R_1M_INDEX 0x0
#define R_2M_INDEX 0x1
#define R_5_5M_INDEX 0x3
#define R_11M_INDEX 0x2
/**@} GroupRatesIndices */


// *****************************************
// * OFDM Rate Masks
// *****************************************
/**
  @defgroup GroupOfdmRatesMasks OFDM Rate Masks
  @ingroup GroupRates
  @{
 */
#define R_6M_MSK  0x1
#define R_9M_MSK  0x2
#define R_12M_MSK 0x4
#define R_18M_MSK 0x8
#define R_24M_MSK 0x10
#define R_36M_MSK 0x20
#define R_48M_MSK 0x40
#define R_54M_MSK 0x80
/**@} GroupOfdmRatesMasks */



// *****************************************
// * CCK Rate Masks
// *****************************************
/**
  @defgroup GroupCckRatesMasks CCK Rate Masks
  @ingroup GroupRates
  @{
 */
#define R_1M_MSK   0x1
#define R_2M_MSK   0x2
#define R_5_5M_MSK 0x4
#define R_11M_MSK  0x8
/**@} GroupCckRatesMasks */


// *****************************************
// * OFDM HT rate codes
// *****************************************
#define MCS_6M 0
#define MCS_12M 1
#define MCS_18M 2
#define MCS_24M 3
#define MCS_36M 4
#define MCS_48M 5
#define MCS_54M 6
#define MCS_60M 7

// MIMO 2-spatial streams
#define MCS_MIMO2_6M  0x8
#define MCS_MIMO2_12M 0x9
#define MCS_MIMO2_18M 0xa
#define MCS_MIMO2_24M 0xb
#define MCS_MIMO2_36M 0xc
#define MCS_MIMO2_48M 0xd
#define MCS_MIMO2_54M 0xe
#define MCS_MIMO2_60M 0xf
// MIMO 3-spatial streams
#define MCS_MIMO3_6M  0x10
#define MCS_MIMO3_12M 0x11
#define MCS_MIMO3_18M 0x12
#define MCS_MIMO3_24M 0x13
#define MCS_MIMO3_36M 0x14
#define MCS_MIMO3_48M 0x15
#define MCS_MIMO3_54M 0x16
#define MCS_MIMO3_60M 0x17

#define MCS_DUP_6M 0x20

// for VHT rates (i.e. 256-QAM punc 3/4 and 5/6)
#define VHT_MCS_RATES_NUM (10)
// for HE rates (i.e. 1024-QAM punc 3/4 and 5/6)
#define HE_MCS_RATES_NUM (12)

#define MCS_VHT_6M  MCS_6M
#define MCS_VHT_12M MCS_12M
#define MCS_VHT_18M MCS_18M
#define MCS_VHT_24M MCS_24M
#define MCS_VHT_36M MCS_36M
#define MCS_VHT_48M MCS_48M
#define MCS_VHT_54M MCS_54M
#define MCS_VHT_60M MCS_60M
#define MCS_VHT_72M 8
#define MCS_VHT_80M 9

#define MCS_VHT_MIMO2_6M  (0x10 | MCS_VHT_6M )
#define MCS_VHT_MIMO2_12M (0x10 | MCS_VHT_12M)
#define MCS_VHT_MIMO2_18M (0x10 | MCS_VHT_18M)
#define MCS_VHT_MIMO2_24M (0x10 | MCS_VHT_24M)
#define MCS_VHT_MIMO2_36M (0x10 | MCS_VHT_36M)
#define MCS_VHT_MIMO2_48M (0x10 | MCS_VHT_48M)
#define MCS_VHT_MIMO2_54M (0x10 | MCS_VHT_54M)
#define MCS_VHT_MIMO2_60M (0x10 | MCS_VHT_60M)
#define MCS_VHT_MIMO2_72M (0x10 | MCS_VHT_72M)
#define MCS_VHT_MIMO2_80M (0x10 | MCS_VHT_80M)

#define MCS_VHT_MIMO3_6M  (0x20 | MCS_VHT_6M )
#define MCS_VHT_MIMO3_12M (0x20 | MCS_VHT_12M)
#define MCS_VHT_MIMO3_18M (0x20 | MCS_VHT_18M)
#define MCS_VHT_MIMO3_24M (0x20 | MCS_VHT_24M)
#define MCS_VHT_MIMO3_36M (0x20 | MCS_VHT_36M)
#define MCS_VHT_MIMO3_48M (0x20 | MCS_VHT_48M)
#define MCS_VHT_MIMO3_54M (0x20 | MCS_VHT_54M)
#define MCS_VHT_MIMO3_60M (0x20 | MCS_VHT_60M)
#define MCS_VHT_MIMO3_72M (0x20 | MCS_VHT_72M)
#define MCS_VHT_MIMO3_80M (0x20 | MCS_VHT_80M)

// HT these rates are not used
#define MCS_MIMO2_MIXED_16Q_04Q_39M 0x21
#define MCS_MIMO2_MIXED_64Q_04Q_52M 0x22
#define MCS_MIMO2_MIXED_64Q_16Q_65M 0x23
#define MCS_MIMO2_MIXED_16Q_04Q_58M 0x24
#define MCS_MIMO2_MIXED_64Q_04Q_78M 0x25
#define MCS_MIMO2_MIXED_64Q_16Q_97M 0x26

#define MCS_MIMO3_MIXED_16Q_04Q_04Q_52M 0x27
#define MCS_MIMO3_MIXED_16Q_16Q_04Q_65M 0x28
#define MCS_MIMO3_MIXED_64Q_04Q_04Q_65M 0x29
#define MCS_MIMO3_MIXED_64Q_16Q_04Q_78M 0x2a
#define MCS_MIMO3_MIXED_64Q_16Q_16Q_91M 0x2b
#define MCS_MIMO3_MIXED_64Q_64Q_04Q_91M 0x2c
#define MCS_MIMO3_MIXED_64Q_64Q_16Q_104M 0x2d
#define MCS_MIMO3_MIXED_16Q_04Q_04Q_78M 0x2e
#define MCS_MIMO3_MIXED_16Q_16Q_04Q_97M 0x2f
#define MCS_MIMO3_MIXED_64Q_04Q_04Q_97M 0x30
#define MCS_MIMO3_MIXED_64Q_16Q_04Q_117M 0x31
#define MCS_MIMO3_MIXED_64Q_16Q_16Q_136M 0x32
#define MCS_MIMO3_MIXED_64Q_64Q_04Q_136M 0x33
#define MCS_MIMO3_MIXED_64Q_64Q_16Q_156M 0x34

#define QAM_BPSK 0 // MCS 0
#define QAM_QPSK 1 // MCS 1, 2
#define QAM_16   2 // MCS 3, 4
#define QAM_64   3 // MCS 5, 6, 7
#define QAM_256  4 // MCS 8, 9
#define QAM_1024 5 // MCS 10, 11

// *****************************************
// * OFDM HT Rate Masks
// *****************************************
#define MCS_6M_MSK           0x1
#define MCS_12M_MSK          0x2
#define MCS_18M_MSK          0x4
#define MCS_24M_MSK          0x8
#define MCS_36M_MSK          0x10
#define MCS_48M_MSK          0x20
#define MCS_54M_MSK          0x40
#define MCS_60M_MSK          0x80
#define MCS_12M_DUAL_MSK     0x100
#define MCS_24M_DUAL_MSK     0x200
#define MCS_36M_DUAL_MSK     0x400
#define MCS_48M_DUAL_MSK     0x800
#define MCS_72M_DUAL_MSK     0x1000
#define MCS_96M_DUAL_MSK     0x2000
#define MCS_108M_DUAL_MSK    0x4000
#define MCS_120M_DUAL_MSK    0x8000
#define MCS_18M_TRIPPLE_MSK  0x10000
#define MCS_36M_TRIPPLE_MSK  0x20000
#define MCS_54M_TRIPPLE_MSK  0x40000
#define MCS_72M_TRIPPLE_MSK  0x80000
#define MCS_108M_TRIPPLE_MSK 0x100000
#define MCS_144M_TRIPPLE_MSK 0x200000
#define MCS_162M_TRIPPLE_MSK 0x400000
#define MCS_180M_TRIPPLE_MSK 0x800000

// *****************************************
// * OFDM VHT Rate Masks
// *****************************************
#define MCS_VHT_6M_MSK        0x1
#define MCS_VHT_12M_MSK       0x2
#define MCS_VHT_18M_MSK       0x4
#define MCS_VHT_24M_MSK       0x8
#define MCS_VHT_36M_MSK       0x10
#define MCS_VHT_48M_MSK       0x20
#define MCS_VHT_54M_MSK       0x40
#define MCS_VHT_60M_MSK       0x80
#define MCS_VHT_72M_MSK       0x100
#define MCS_VHT_80M_MSK       0x200
#define MCS_VHT_6M_MIMO2_MSK  0x400
#define MCS_VHT_12M_MIMO2_MSK 0x800
#define MCS_VHT_18M_MIMO2_MSK 0x1000
#define MCS_VHT_24M_MIMO2_MSK 0x2000
#define MCS_VHT_36M_MIMO2_MSK 0x4000
#define MCS_VHT_48M_MIMO2_MSK 0x8000
#define MCS_VHT_54M_MIMO2_MSK 0x10000
#define MCS_VHT_60M_MIMO2_MSK 0x20000
#define MCS_VHT_72M_MIMO2_MSK 0x40000
#define MCS_VHT_80M_MIMO2_MSK 0x80000
#define MCS_VHT_6M_MIMO3_MSK  0x100000
#define MCS_VHT_12M_MIMO3_MSK 0x200000
#define MCS_VHT_18M_MIMO3_MSK 0x400000
#define MCS_VHT_24M_MIMO3_MSK 0x800000
#define MCS_VHT_36M_MIMO3_MSK 0x1000000
#define MCS_VHT_48M_MIMO3_MSK 0x2000000
#define MCS_VHT_54M_MIMO3_MSK 0x4000000
#define MCS_VHT_60M_MIMO3_MSK 0x8000000
#define MCS_VHT_72M_MIMO3_MSK 0x10000000
#define MCS_VHT_80M_MIMO3_MSK 0x20000000


#define MCS_HE_6M  MCS_VHT_6M
#define MCS_HE_12M MCS_VHT_12M
#define MCS_HE_18M MCS_VHT_18M
#define MCS_HE_24M MCS_VHT_24M
#define MCS_HE_36M MCS_VHT_36M
#define MCS_HE_48M MCS_VHT_48M
#define MCS_HE_54M MCS_VHT_54M
#define MCS_HE_60M MCS_VHT_60M
#define MCS_HE_72M MCS_VHT_72M
#define MCS_HE_80M MCS_VHT_80M
#define MCS_HE_90M 0xa
#define MCS_HE_100M 0xb
#define MCS_HE_MAX MCS_HE_100M

#define BEACON_TEMPLATE_FLAGS_ANT_A_POS          (9)
#define BEACON_TEMPLATE_FLAGS_ANT_B_POS          (10)
#define BEACON_TEMPLATE_FLAGS_ANT_C_POS          (11)
#define BEACON_TEMPLATE_FLAGS_ANT_ABC_NORM_MSK   (BIT_MASK_3BIT)
#define BEACON_TEMPLATE_FLAGS_ANT_ABC_MSK        (BEACON_TEMPLATE_FLAGS_ANT_ABC_NORM_MSK << BEACON_TEMPLATE_FLAGS_ANT_A_POS)

// Kedron, added rate & MCS struct
// bit 7:0 Rate or MCS
// bit 8 OFDM-HT
// bit 9 CCK
// bit 10 reserved (removed CCK short preamble)
// bit 11 FAT channel
// bit 12 reserved (removed FAT duplicate)
// bit 13 short GI
// bit 14 TX chain A enable
// bit 15 TX chain B enable
// bit 16 TX chain C enable

// HT  bit-2:0 give rate code (QAM/puncture) such 0==>6M....7==>63M,
//     bit-4:3 spatial streams such 0==>1 SS, 1==>2 SS, 2==>3 SS
// VHT bit-3:0 give rate code (QAM/puncture)
//     bit-5:4 spatial streams such 0==>1 SS, 1==>2 SS, 2==>3 SS
// HE
#define RATE_MCS_CODE_MSK     0x7f

typedef enum _MIMO_INDX_E
{
  SISO_INDX  = 0,
  MIMO2_INDX = 1,
  MIMO3_INDX = 2,
  MIMO4_INDX = 3,
  MAX_MIMO_INDX
} MIMO_INDX_E;

// this mask will apply to all MCS with the exception of MCS 32 which is not mimo but bit 5 is set
// for all other MCSs bits [6..3] set --> MIMO rate
#define RATE_MCS_HT_RATE_CODE_MSK 0x7
#define RATE_MCS_HT_MIMO_POS      3
#define RATE_MCS_HT_MIMO_MSK      (0x3 << RATE_MCS_HT_MIMO_POS)
// bit-3 0==>one spatial stream 1==>two spatial streams
#define RATE_MCS_HT_SISO_MSK      (0 << RATE_MCS_HT_MIMO_POS)
#define RATE_MCS_HT_MIMO2_MSK     (1 << RATE_MCS_HT_MIMO_POS)
#define RATE_MCS_HT_MIMO3_MSK     (2 << RATE_MCS_HT_MIMO_POS)

// bit-3:0 give VHT rate code, QAM and puncturing
// 0==>BPSK 1/2,   1==>QPSK 1/2,  2==>QPSK 2/3,  3==>16QAM 1/2
// 4==>16QAM 3/4,  5==>64QAM 2/3, 6==>64QAM 3/4, 7==>64QAM 5/6
// 8==>256QAM 3/4, 9==>256QAM 5/6
#define RATE_MCS_VHT_RATE_CODE_MSK 0xf
#define RATE_MCS_VHT_MIMO_POS      4
#define RATE_MCS_VHT_MIMO_MSK      (3 << RATE_MCS_VHT_MIMO_POS)
#define RATE_MCS_VHT_SISO          (0 << RATE_MCS_VHT_MIMO_POS)
#define RATE_MCS_VHT_MIMO2         (1 << RATE_MCS_VHT_MIMO_POS)
#define RATE_MCS_VHT_MIMO3         (2 << RATE_MCS_VHT_MIMO_POS)
#define RATE_MCS_VHT_MIMO4         (3 << RATE_MCS_VHT_MIMO_POS)

// bit-5 HT MCS 0x20, 2x6Mbps, not used
#define RATE_MCS_HT_DUP_POS        5
#define RATE_MCS_HT_DUP_MSK        (1 << RATE_MCS_HT_DUP_POS)
// flag field reside at bits 15:8
#define RATE_MCS_FLAGS_POS         8

// bit-8 0==>legacy rate 1==>HT MCS
#define RATE_MCS_HT_POS            8
#define RATE_MCS_HT_MSK            (1 << RATE_MCS_HT_POS)
// bit-9 0==>OFDM 1==>CCK
#define RATE_MCS_CCK_POS           9
#define RATE_MCS_CCK_MSK           (1 << RATE_MCS_CCK_POS)

// bit-10 mark usage of green-field preamble
//#define RATE_MCS_GF_POS            10
//#define RATE_MCS_GF_MSK            (1 << RATE_MCS_GF_POS)

// bit-10 mark modulation is OFDM HE, exact type is given in VHT_HT_TYPE field
#define RATE_MCS_HE_POS           10

// bit-12:11 0==>20MHz, 1==>40MHz, 2==>80MHz, 3==>160MHz
// Note: for duplicate use non-FAT
#define RATE_MCS_FAT_POS           11
#define RATE_MCS_FAT_MSK_API_D_VER_1           (CHANNEL_WIDTH_MSK_API_D_VER_1 << RATE_MCS_FAT_POS)
#define RATE_MCS_FAT_MSK_API_D_VER_2           (CHANNEL_WIDTH_MSK_API_D_VER_2 << RATE_MCS_FAT_POS)
#define RATE_MCS_FAT40             (CHANNEL_WIDTH40 << RATE_MCS_FAT_POS)
#define RATE_MCS_FAT80             (CHANNEL_WIDTH80 << RATE_MCS_FAT_POS)
#define RATE_MCS_FAT160            (CHANNEL_WIDTH160 << RATE_MCS_FAT_POS)

// bit-13 0==>normal guard interval 1==>short guard interval
#define RATE_MCS_SGI_POS           13
#define RATE_MCS_SGI_MSK           (1 << RATE_MCS_SGI_POS)
// bit-14 0==>chain A incative 1==>chain A active
#define RATE_MCS_ANT_A_POS         14
// bit-15 0==>chain B incative 1==>chain B active
#define RATE_MCS_ANT_B_POS         15

//new flags in shiloh (ext_flags)

// bit-16 0==>chain B incative 1==>chain B active
#define RATE_MCS_ANT_C_POS         16
// bit-15:14 mask for both ant.
#define RATE_MCS_ANT_AB_MSK        (RATE_MCS_ANT_A_MSK | RATE_MCS_ANT_B_MSK)
#define RATE_MCS_ANT_AC_MSK        (RATE_MCS_ANT_A_MSK | RATE_MCS_ANT_C_MSK)
#define RATE_MCS_ANT_BC_MSK        (RATE_MCS_ANT_B_MSK | RATE_MCS_ANT_C_MSK)

// bit-16:14 mask for all ants.

#define RATE_MCS_3ANT_MSK(rate) \
  ((rate.rate_n_flags & RATE_MCS_ANT_ABC_MSK) == RATE_MCS_ANT_ABC_MSK)

#define RATE_MCS_2ANT_MSK(rate) \
  (((rate.rate_n_flags & RATE_MCS_ANT_ABC_MSK) == RATE_MCS_ANT_AB_MSK) || \
  ((rate.rate_n_flags & RATE_MCS_ANT_ABC_MSK) == RATE_MCS_ANT_AC_MSK)|| \
  ((rate.rate_n_flags & RATE_MCS_ANT_ABC_MSK) == RATE_MCS_ANT_BC_MSK))

#define RATE_MCS_1ANT_MSK(rate) \
  (((rate.rate_n_flags & RATE_MCS_ANT_ABC_MSK) == RATE_MCS_ANT_A_MSK) || \
  ((rate.rate_n_flags & RATE_MCS_ANT_ABC_MSK) == RATE_MCS_ANT_B_MSK)|| \
  ((rate.rate_n_flags & RATE_MCS_ANT_ABC_MSK) == RATE_MCS_ANT_C_MSK))

// rate&flags cleanup - use single bit for STBC
// for HT the number of space-time-streams is SS+STBC, STBC is 0/1/2
// for VHT the number of space-time-streams is: STBC=0 ==> SS, STBC=1 ==> SS*2, STBC is 0/1
// note for HT STBC is writen in TX_VEC0, while for VHT in TX_VEC_VHT1 (0x8051)
#define RATE_MCS_STBC_POS          17

// HE - dual carrier mode
#define RATE_MCS_HE_DCM_POS        18
#define RATE_MCS_HE_DCM_MSK        (1 << RATE_MCS_HE_DCM_POS)

// Beam-forming defines
#define RATE_MCS_BF_POS            19
#define RATE_MCS_BF_MSK            (1 << RATE_MCS_BF_POS)

// removed - HE rate&flags cleanup
// BFR NDP frame, this is used only for TX NDP.
//#define RATE_MCS_ZLF_POS           (20)
//#define RATE_MCS_ZLF_MSK           (1 << RATE_MCS_ZLF_POS)

// removed - HE rate&flags cleanup
// sounding packet, used (currently) only for HT BF, (i.e. not VHT)
//#define RATE_MCS_HT_SOUNDING_POS   (21)
//#define RATE_MCS_HT_SOUNDING_MSK   (1 << RATE_MCS_HT_SOUNDING_POS)

// removed - HE rate&flags cleanup
// extended LTFs used to allow the transmitter to send more LTFs then the MIMO level
// currently used only for HT rates, (for VHT use NDP (i.e. ZLF))
//#define RATE_MCS_HT_NUMBER_EXT_LTF_POS (22)
//#define RATE_MCS_HT_NUMBER_EXT_LTF_MSK (3 << RATE_MCS_HT_NUMBER_EXT_LTF_POS)

//HE guard-interval and LTF size: 0 - 1xLTF+0.8us, 1 - 2xLTF+0.8us, 2 - 2xLTF+1.6us, 3 - 4xLTF+3.2us,
//  for HE_SU a 4th - 4xLTF+0.8us <- GI_LTF=3 & SGI=1
#define RATE_MCS_HE_GI_LTF_POS       20

//VHT: 0 - SU, 2 - MU, HE: 0 - SU, 1 - SU_EXT, 2 - MU, 3 - trig-base
#define RATE_MCS_VHT_HE_TYPE_POS     22
#define RATE_MCS_VHT_HE_TYPE_MSK     (0x3 << RATE_MCS_VHT_HE_TYPE_POS)
#define RATE_MCS_VHT_HE_SU           0
#define RATE_MCS_HE_EXT_RANGE        1
#define RATE_MCS_VHT_HE_MU           2
#define RATE_MCS_HE_TRIG_BASE        3

// VHT rates defines
// VHT frame
// 0==>1x20MHz (i.e. no dup), 1==>2x20MHz, 2==>4x20MHz, 3==>8x20MHz
// note: when using duplicate FAT should be 0, i.e. 20Mhz
#define RATE_MCS_DUP_POS_API_D_VER_1         (12)
#define RATE_MCS_DUP_POS_API_D_VER_2         (24)

#define RATE_MCS_DUP_MSK_API_D_VER_1           (CHANNEL_WIDTH_MSK_API_D_VER_1 << RATE_MCS_DUP_POS_API_D_VER_1)
#define RATE_MCS_DUP_MSK_API_D_VER_2           (CHANNEL_WIDTH_MSK_API_D_VER_2 << RATE_MCS_DUP_POS_API_D_VER_2)

//rate&flags cleanup, moved to VHT_HE_TYPE field
//#define RATE_MCS_MU_POS            (28)
//#define RATE_MCS_MU_MSK            (1 << RATE_MCS_MU_POS)

// mark HE extended range using 106-tones
#define RATE_MCS_HE_ER_106_POS     (28)
#define RATE_MCS_HE_ER_106_MSK     (1 << RATE_MCS_HE_ER_106_POS)

#define RATE_MCS_RTS_REQUIRED_POS  (30)
#define RATE_MCS_RTS_REQUIRED_MSK  (0x1 << RATE_MCS_RTS_REQUIRED_POS)

#define RATE_MCS_CTS_REQUIRED_POS  (31)
#define RATE_MCS_CTS_REQUIRED_MSK  ((U32)0x1 << RATE_MCS_CTS_REQUIRED_POS)

#define RATE_MCS_PROT_REQUIRED_POS (30)
#define RATE_MCS_PROT_REQUIRED_MSK ((U32)0x3 << RATE_MCS_PROT_REQUIRED_POS)

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//The below defines are obsolete.
//There are still define here since the driver still use it.
//Need to cleanup the driver code and remove it from here.
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#define RATE_MCS_GF_POS                (10)
#define RATE_MCS_GF_MSK                (1 << RATE_MCS_GF_POS)
#define RATE_MCS_ZLF_POS               (20)
#define RATE_MCS_ZLF_MSK               (1 << RATE_MCS_ZLF_POS)
#define RATE_MCS_HT_SOUNDING_POS       (21)
#define RATE_MCS_HT_SOUNDING_MSK       (1 << RATE_MCS_HT_SOUNDING_POS)
#define RATE_MCS_HT_NUMBER_EXT_LTF_POS (22)
#define RATE_MCS_HT_NUMBER_EXT_LTF_MSK (3 << RATE_MCS_HT_NUMBER_EXT_LTF_POS)
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#define POWER_TABLE_NUM_HT_OFDM_ENTRIES           (32) //for Kedron

#define MAX_SUPP_MCS_API_D_VER_1      (15)

// modified the max rate index from 76 which is the spec value to 31 which is the last "normal" rate index
// the next "crazy" rate codes are 32-HT_DUP and 33..76 Nss with different QAM per spatial stream.
// note the bc/rate-->time conversion doesn't handle the crazy rates.
//#define MAX_LEGAL_MCS_API_D_VER_1  (76)
#define MAX_LEGAL_MCS_API_D_VER_1  (31)
#define MAX_TX_MCS_API_D_VER_1     (31)

// two entries: one for 2 chains TX(index 1), one for single chain(index 0)
#define POWER_TABLE_NUM_CCK_ENTRIES_API_D_VER_1      (2)

#define POWER_TABLE_NUM_HT_OFDM_ENTRIES_API_D_VER_1  (24) //MCSs 0-23

#define POWER_TABLE_3_STREAM_HT_OFDM_ENTRIES_API_D_VER_1      (24) //MCSs 0-23
#define POWER_TABLE_2_STREAM_HT_OFDM_ENTRIES_API_D_VER_1      (16) //MCSs 0-15
#define POWER_TABLE_1_STREAM_HT_OFDM_ENTRIES_API_D_VER_1      (8)  //MCSs 0-7

#define POWER_TABLE_TOTAL_ENTRIES_API_D_VER_1        (POWER_TABLE_NUM_CCK_ENTRIES_API_D_VER_1 + POWER_TABLE_3_STREAM_HT_OFDM_ENTRIES_API_D_VER_1) //3 tx chain
#define POWER_TABLE_TOTAL_ENTRIES_API_D_VER_2        (POWER_TABLE_NUM_CCK_ENTRIES_API_D_VER_1 + POWER_TABLE_2_STREAM_HT_OFDM_ENTRIES_API_D_VER_1) //2 tx chain
#define POWER_TABLE_TOTAL_ENTRIES_API_D_VER_3        (POWER_TABLE_NUM_CCK_ENTRIES_API_D_VER_1 + POWER_TABLE_1_STREAM_HT_OFDM_ENTRIES_API_D_VER_1) //1 tx chain

/**
 * @ingroup apiCmdAllTypes
 * \brief rates enum
 *
 */
typedef enum _MCS_API_E_VER_1
{
  mcs_6m  = MCS_6M,
  mcs_12m = MCS_12M,
  mcs_18m = MCS_18M,
  mcs_24m = MCS_24M,
  mcs_36m = MCS_36M,
  mcs_48m = MCS_48M,
  mcs_54m = MCS_54M,
  mcs_60m = MCS_60M,

  mcs_MIMO2_6m  = MCS_MIMO2_6M,
  mcs_MIMO2_12m = MCS_MIMO2_12M,
  mcs_MIMO2_18m = MCS_MIMO2_18M,
  mcs_MIMO2_24m = MCS_MIMO2_24M,
  mcs_MIMO2_36m = MCS_MIMO2_36M,
  mcs_MIMO2_48m = MCS_MIMO2_48M,
  mcs_MIMO2_54m = MCS_MIMO2_54M,
  mcs_MIMO2_60m = MCS_MIMO2_60M,

  mcs_MIMO3_6m  = MCS_MIMO3_6M,
  mcs_MIMO3_12m = MCS_MIMO3_12M,
  mcs_MIMO3_18m = MCS_MIMO3_18M,
  mcs_MIMO3_24m = MCS_MIMO3_24M,
  mcs_MIMO3_36m = MCS_MIMO3_36M,
  mcs_MIMO3_48m = MCS_MIMO3_48M,
  mcs_MIMO3_54m = MCS_MIMO3_54M,
  mcs_MIMO3_60m = MCS_MIMO3_60M,

  mcs_dup_6m = MCS_DUP_6M,

  mcs_MIMO2_mixed_16q_04q_39m = MCS_MIMO2_MIXED_16Q_04Q_39M,
  mcs_MIMO2_mixed_64q_04q_52m = MCS_MIMO2_MIXED_64Q_04Q_52M,
  mcs_MIMO2_mixed_64q_16q_65m = MCS_MIMO2_MIXED_64Q_16Q_65M,
  mcs_MIMO2_mixed_16q_04q_58m = MCS_MIMO2_MIXED_16Q_04Q_58M,
  mcs_MIMO2_mixed_64q_04q_78m = MCS_MIMO2_MIXED_64Q_04Q_78M,
  mcs_MIMO2_mixed_64q_16q_97m = MCS_MIMO2_MIXED_64Q_16Q_97M,

  mcs_MIMO3_mixed_16q_04q_04q_52m  = MCS_MIMO3_MIXED_16Q_04Q_04Q_52M,
  mcs_MIMO3_mixed_16q_16q_04q_65m  = MCS_MIMO3_MIXED_16Q_16Q_04Q_65M,
  mcs_MIMO3_mixed_64q_04q_04q_65m  = MCS_MIMO3_MIXED_64Q_04Q_04Q_65M,
  mcs_MIMO3_mixed_64q_16q_04q_78m  = MCS_MIMO3_MIXED_64Q_16Q_04Q_78M,
  mcs_MIMO3_mixed_64q_16q_16q_91m  = MCS_MIMO3_MIXED_64Q_16Q_16Q_91M,
  mcs_MIMO3_mixed_64q_64q_04q_91m  = MCS_MIMO3_MIXED_64Q_64Q_04Q_91M,
  mcs_MIMO3_mixed_64q_64q_16q_104m = MCS_MIMO3_MIXED_64Q_64Q_16Q_104M,
  mcs_MIMO3_mixed_16q_04q_04q_78m  = MCS_MIMO3_MIXED_16Q_04Q_04Q_78M,
  mcs_MIMO3_mixed_16q_16q_04q_97m  = MCS_MIMO3_MIXED_16Q_16Q_04Q_97M,
  mcs_MIMO3_mixed_64q_04q_04q_97m  = MCS_MIMO3_MIXED_64Q_04Q_04Q_97M,
  mcs_MIMO3_mixed_64q_16q_04q_117m = MCS_MIMO3_MIXED_64Q_16Q_04Q_117M,
  mcs_MIMO3_mixed_64q_16q_16q_136m = MCS_MIMO3_MIXED_64Q_16Q_16Q_136M,
  mcs_MIMO3_mixed_64q_64q_04q_136m = MCS_MIMO3_MIXED_64Q_64Q_04Q_136M,
  mcs_MIMO3_mixed_64q_64q_16q_156m = MCS_MIMO3_MIXED_64Q_64Q_16Q_156M,

} MCS_API_E_VER_1;

/**
 * @ingroup apiCmdAllTypes
 * \brief mcs rates
 *
 *
 */
typedef struct _RATE_MCS_API_S_VER_1
{
  U08 rate;
  U08 flags;
  U16 ext_flags;
} __attribute__((packed)) RATE_MCS_API_S_VER_1;

/**
 * Rate representation, with dissection for individual bits
 */
/**
 * @ingroup apiCmdAllTypes
 * \brief mcs rate bits
 *
 *
 */
typedef struct RATE_MCS_BITS_API_S_VER_3
{
  unsigned int rate:7;             /**< bit 6:0 Rate or MCS */
  unsigned int reserved1:1;        /**< bit 7 reserved */
  unsigned int ofdm_ht:1;          /**< bit 8 OFDM-HT */
  unsigned int cck:1;              /**< bit 9 CCK */
  unsigned int ofdm_he:1;          /**< bit 10 OFDM-HE */
  unsigned int fat_channel:2;      /**< bit 12:11 FAT channel 20Mhz...160Mhz, for OFDMA this gives the full channel width vs. RU */
  unsigned int short_gi:1;         /**< bit 13 short GI, for HT/VHT 0 - 0.8us, 1 - 0.4us, for HE-SU use for 5th LTF_GI
                                               for HE-SU use for 5th LTF_GI=3 -> 4xLTF+0.8 */
  unsigned int ant_a:1;            /**< bit 14 chain A active */
  unsigned int ant_b:1;            /**< bit 15 chain B active */
  unsigned int ant_c:1;            /**< bit 16 chain C active */
  unsigned int stbc:1;             /**< bit 17 STBC */
  unsigned int he_dcm:1;           /**< bit 18 OFDM-HE dual carrier mode, this reduce the number of data tones by half (for all RUs) */
  unsigned int bf:1;               /**< bit 19 beamforming*/
  unsigned int he_gi_ltf:2;        /**< bit 21:20 HE guard-interval and LTF
                                                  HE SU  : 0 - 1xLTF+0.8, 1 - 2xLTF+0.8, 2 - 2xLTF+1.6, 3 - 4xLTF+3.2, 3+short_gi - 4xLTF+0.8
                                                  HE MU  : 0 - 4xLTF+0.8, 1 - 2xLTF+0.8, 2 - 2xLTF+1.6, 3 - 4xLTF+3.2
                                                  HE TRIG: 0 - 1xLTF+1.6, 1 - 2xLTF+1.6, 2 - 4xLTF+3.2*/
  unsigned int vht_he_type:2;      /**< bit 23:22 VHT: 0 - SU, 2 - MU, HE: 0 - SU, 1 - SU_EXT_RANGE, 2 - MU (RU gives data channel width), 3 - trig-base */
  unsigned int dup_channel:2;      /**< bit 25:24 duplicate channel x1, x2, x4, x8*/
  unsigned int ofdm_vht:1;         /**< bit 26 VHT */
  unsigned int ldpc:1;             /**< bit 27 LDPC code */
  unsigned int he_er_106:1;        /**< bit 28 HE extended range use 102 data-tones (or 106 tones)*/
  unsigned int reserved3:1;        /**< bit 29 */
  unsigned int rts_required:1;     /**< bit 30 RTS reuired for this rate (uCode decision) */
  unsigned int cts_required:1;     /**< bit 31 CTS reuired for this rate (uCode decision) */

  //unsigned int gf:1;             /**< bit 10 green-field */
  //unsigned int stbc:2;           /**< bit 18:17 STBC */
  //unsigned int zlf:1;            /**< bit 20 ZLF (NDP) */
  //unsigned int sounding:1;       /**< bit 21 sounding packet*/
  //unsigned int num_of_ext_ss:2;  /**< bit 23:22 number of extended spatial streams for sounding*/
  //unsigned int vht_mu:1;         /**< bit 28 VHT/HE Multi-user */
} __attribute__((packed)) RATE_MCS_BITS_API_S_VER_3;

/**
 * @ingroup apiCmdAllTypes
 * \brief Add group and description ...
 *
 *
 */
typedef union _RATE_MCS_API_U_VER_1
{
  RATE_MCS_API_S_VER_1  s;
#if !defined(SV_TOOL_PRECOMPILE_HEADERS)
  RATE_MCS_BITS_API_S_VER_3 bits;
#endif
  U32         rate_n_flags;
} __attribute__((packed)) RATE_MCS_API_U_VER_1;

/**
 * @ingroup apiCmdAllTypes
 * \brief rate struct
 *
 *
 */
typedef struct _RATE_MCS_API_S_VER_0
{
  U08 rate;
  U08 flags;
} __attribute__((packed)) RATE_MCS_API_S_VER_0;

#define IS_RATE_OFDM_API_M_VER_2(c_rate) \
  ((((c_rate).rate_n_flags) & RATE_MCS_CCK_MSK) == 0)

#define IS_RATE_OFDM_LEGACY_API_M_VER_2(c_rate) \
  ((((c_rate).rate_n_flags) & (RATE_MCS_CCK_MSK | RATE_MCS_HT_MSK | RATE_MCS_VHT_MSK)) == 0)

#define IS_RATE_OFDM_HT_API_M_VER_2(c_rate) \
  (((c_rate).rate_n_flags) & RATE_MCS_HT_MSK)

#define IS_RATE_OFDM_VHT_API_M_VER_3(c_rate) \
  (((c_rate).rate_n_flags) & RATE_MCS_VHT_MSK)

#define IS_RATE_OFDM_HT_VHT_API_M_VER_3(c_rate) \
  (((c_rate).rate_n_flags) & (RATE_MCS_HT_MSK | RATE_MCS_VHT_MSK))

#define IS_RATE_OFDM_VHT_API_M_VER_2(c_rate) (FALSE)

#define IS_RATE_OFDM_HT_VHT_API_M_VER_2(c_rate) \
  IS_RATE_OFDM_HT_API_M_VER_2(c_rate)

#define IS_RATE_CCK_API_M_VER_3(c_rate) \
  (((c_rate).rate_n_flags) & RATE_MCS_CCK_MSK)

#define GET_ANT_CHAIN_API_M_VER_1(c_rate) \
   SHIFT_AND_MASK(c_rate.rate_n_flags,RATE_MCS_ANT_ABC_MSK,RATE_MCS_ANT_A_POS)

#define GET_ANT_CHAIN_NUM_API_M_VER_1(c_rate) \
  (g_ChainCfg2ChainNum[GET_ANT_CHAIN_API_M_VER_1(c_rate)])

// don't use for VHT
#define IS_RATE_STBC_PRESENT_API_M_VER_1(c_rate) \
  (((c_rate.rate_n_flags) & RATE_MCS_STBC_MSK))

// don't use for VHT
#define GET_NUM_OF_STBC_SS_API_M_VER_1(c_rate) \
   SHIFT_AND_MASK((c_rate.rate_n_flags), RATE_MCS_STBC_MSK, RATE_MCS_STBC_POS)

// 0==>20MHz, 1==>40MHz
#define GET_BW_INDEX_API_M_VER_1(c_rate) \
  (((c_rate.rate_n_flags) & RATE_MCS_FAT_MSK_API_D_VER_1) >> RATE_MCS_FAT_POS)
// 0==>20MHz, 1==>40MHz, 2==>80MHz, 3==>160MHz
#define GET_BW_INDEX_API_M_VER_2(c_rate) \
  (((c_rate.rate_n_flags) & RATE_MCS_FAT_MSK_API_D_VER_2) >> RATE_MCS_FAT_POS)

// 0==>x1, 1==>x2
#define GET_DUP_INDEX_API_M_VER_1(c_rate) \
  (((c_rate.rate_n_flags) & RATE_MCS_DUP_MSK_API_D_VER_1) >> RATE_MCS_DUP_POS_API_D_VER_1)
// 0==>x1, 1==>x2, 2==>x4, 3==>x8
#define GET_DUP_INDEX_API_M_VER_2(c_rate) \
  (((c_rate.rate_n_flags) & RATE_MCS_DUP_MSK_API_D_VER_2) >> RATE_MCS_DUP_POS_API_D_VER_2)

// get channel width, either by using true wide channel or by duplicate
#define GET_CHANNEL_WIDTH_INDEX_API_M_VER_1(c_rate) \
  (GET_BW_INDEX_API_M_VER_1(c_rate) | GET_DUP_INDEX_API_M_VER_1(c_rate))
#define GET_CHANNEL_WIDTH_INDEX_API_M_VER_2(c_rate) \
  (GET_BW_INDEX_API_M_VER_2(c_rate) | GET_DUP_INDEX_API_M_VER_2(c_rate))

// 0==>normal GI, 1==>short GI
#define GET_GI_INDEX_API_M_VER_1(c_rate) \
   SHIFT_AND_MASK((c_rate.rate_n_flags), RATE_MCS_SGI_MSK, RATE_MCS_SGI_POS)

#define IS_RATE_OFDM_HT_FAT_API_M_VER_2(c_rate) \
  (((c_rate.rate_n_flags) & (RATE_MCS_HT_MSK | RATE_MCS_FAT_MSK_API_D_VER_1)) == \
   (RATE_MCS_HT_MSK | RATE_MCS_FAT40))

#define GET_HT_MIMO_INDEX_API_M_VER_1(c_rate) \
   SHIFT_AND_MASK((c_rate.rate_n_flags),RATE_MCS_HT_MIMO_MSK,RATE_MCS_HT_MIMO_POS)

#define GET_NUM_OF_HT_SS_API_M_VER_1(c_rate) \
  (GET_HT_MIMO_INDEX_API_M_VER_1(c_rate) + 1)

#define IS_RATE_OFDM_HT_MIMO_API_M_VER_2(c_rate) \
  (IS_RATE_OFDM_HT_API_M_VER_2(c_rate) && (GET_NUM_OF_HT_SS_API_M_VER_1(c_rate) > 1))

#define IS_RATE_OFDM_HT2x2MIMO_API_M_VER_1(c_rate) \
  (IS_RATE_OFDM_HT_API_M_VER_2(c_rate) && (GET_NUM_OF_HT_SS_API_M_VER_1(c_rate) == 2))

#define IS_RATE_OFDM_HT3x3MIMO_API_M_VER_1(c_rate) \
  (IS_RATE_OFDM_HT_API_M_VER_2(c_rate) && (GET_NUM_OF_HT_SS_API_M_VER_1(c_rate) == 3))

#define IS_RATE_OFDM_HT4x4MIMO_API_M_VER_1(c_rate) \
  (IS_RATE_OFDM_HT_API_M_VER_2(c_rate) && (GET_NUM_OF_HT_SS_API_M_VER_1(c_rate) == 4))

#define GET_HT_RATE_CODE_API_M_VER_1(c_rate) \
  ((c_rate).rate_n_flags & RATE_MCS_HT_RATE_CODE_MSK)

#define IS_RATE_HT_STBC_SINGLE_SS_API_M_VER_1(c_rate) \
  (((c_rate.rate_n_flags) & (RATE_MCS_HT_MSK | RATE_MCS_STBC_MSK)) == \
   (RATE_MCS_HT_MSK | RATE_MCS_STBC_MSK))

// rate&flags cleanup, note extended HT-LTF not supported by DSP
#define GET_NUM_OF_HT_EXT_LTF_API_M_VER_1(c_rate) 0

#define GET_NUM_OF_HT_SPACE_TIME_STREAMS_API_M_VER_1(c_rate) \
  (GET_NUM_OF_HT_SS_API_M_VER_1(c_rate) + GET_NUM_OF_STBC_SS_API_M_VER_1(c_rate))

// check if supported rate the bad rate conditions are:
// 1. MCS is 32 but FAT is not set
// 2. SGI and GF are set w/o MIMO - removed, as support for GF was removed
// 3. Number of STBC SS is greater than 2
// 4. Number of STBC is 2 and number of SS isn't equals to 2
// 5. Legal MCS
#define IS_BAD_OFDM_HT_RATE_API_M_VER_2(rx_rate) \
  ((((rx_rate.s.rate) == MCS_DUP_6M) && (!((rx_rate.rate_n_flags) & RATE_MCS_FAT_MSK_API_D_VER_1))) || \
  (GET_NUM_OF_STBC_SS_API_M_VER_1(rx_rate) > 2) || \
  ((GET_NUM_OF_STBC_SS_API_M_VER_1(rx_rate) == 2) && (!(IS_RATE_OFDM_HT2x2MIMO_API_M_VER_1(rx_rate)))) || \
  (IS_RATE_OFDM_HT_API_M_VER_2(rx_rate) && (rx_rate.s.rate > MAX_LEGAL_MCS_API_D_VER_1)))

// removed: GF support:
//  ((!(IS_RATE_OFDM_HT_MIMO_API_M_VER_2(rx_rate))) && (((rx_rate.rate_n_flags) & (RATE_MCS_GF_MSK | RATE_MCS_SGI_MSK)) == (RATE_MCS_GF_MSK | RATE_MCS_SGI_MSK))) ||

#define IS_BAD_OFDM_HT_RATE_API_M_VER_3(rx_rate) \
( (((rx_rate.s.rate) == MCS_DUP_6M) && (!((rx_rate.rate_n_flags) & RATE_MCS_FAT_MSK_API_D_VER_2))) || \
  (GET_NUM_OF_STBC_SS_API_M_VER_1(rx_rate) > 2) || \
  ((GET_NUM_OF_STBC_SS_API_M_VER_1(rx_rate) == 2) && (!(IS_RATE_OFDM_HT2x2MIMO_API_M_VER_1(rx_rate)))) || \
  (IS_RATE_OFDM_HT_API_M_VER_2(rx_rate) && (rx_rate.s.rate > MAX_LEGAL_MCS_API_D_VER_1)) )

#define IS_RATE_HT_HIGH_RATE_API_M_VER_1(c_rate) \
  (((c_rate.rate_n_flags) & (RATE_MCS_HT_MSK | RATE_MCS_CODE_MSK)) > \
   (RATE_MCS_HT_MSK | RATE_MCS_HT_MIMO2_MSK | MCS_24M_MSK))

// 0==>SISO, 1==>MIMO2, 2==>MIMO3, 3==>MIMO4
#define GET_VHT_MIMO_INDX_API_M_VER_1(c_rate) \
   SHIFT_AND_MASK(((c_rate).s.rate), RATE_MCS_VHT_MIMO_MSK, RATE_MCS_VHT_MIMO_POS)

// for Single-User number of Spatial-steams in SIG is actual number -1
// for Multi-User number of Spatial-steams in SIG is actual number
#define GET_NUM_OF_VHT_SS_API_M_VER_1(c_rate) \
  (GET_VHT_MIMO_INDX_API_M_VER_1(c_rate) + 1)

#define GET_VHT_RATE_CODE_API_M_VER_1(c_rate) \
  ((c_rate).rate_n_flags & RATE_MCS_VHT_RATE_CODE_MSK)

#define GET_HT_VHT_RATE_CODE_API_M_VER_1(c_rate) \
  IS_RATE_OFDM_HT_API_M_VER_2(c_rate) ? \
  GET_HT_RATE_CODE_API_M_VER_1(c_rate) : \
  GET_VHT_RATE_CODE_API_M_VER_1(c_rate)

// for VHT (unlike HT) STBC may be turned off/on, thus with STBC on, the number of
// space-time-streams is doubled.
#define IS_VHT_STBC_PRESENT_API_M_VER_1(c_rate) \
  (((c_rate.rate_n_flags) & RATE_MCS_STBC_MSK))

#define GET_NUM_OF_VHT_SPACE_TIME_STREAMS_API_M_VER_1(c_rate) \
  (GET_NUM_OF_VHT_SS_API_M_VER_1(c_rate) << (IS_VHT_STBC_PRESENT_API_M_VER_1(c_rate) >> RATE_MCS_STBC_POS))

// get Mimo level for any rate, i.e. note legacy rate is SISO
// vht not supported
#define GET_MIMO_INDEX_API_M_VER_2(c_rate) \
  (IS_RATE_OFDM_HT_API_M_VER_2(c_rate) ? GET_HT_MIMO_INDEX_API_M_VER_1(c_rate) : SISO_INDX)

// LDPC rate
#define IS_RATE_OFDM_LDPC_API_M_VER_2(c_rate) \
  ((c_rate).rate_n_flags & RATE_MCS_LDPC_MSK)

// beamformed frame indication
#define GET_BF_INDEX_API_M_VER_1(c_rate) \
  (((c_rate.rate_n_flags) & RATE_MCS_BF_MSK) >> RATE_MCS_BF_POS)

// VHT/HE MU
#define IS_RATE_VHT_MU_API_M_VER_1(c_rate) \
  (((c_rate.rate_n_flags) & (RATE_MCS_VHT_MSK | RATE_MCS_VHT_HE_TYPE_MSK)) == (RATE_MCS_VHT_MSK | (RATE_MCS_VHT_HE_MU << RATE_MCS_VHT_HE_TYPE_POS)))

// *************************************************************************
// *            HE definitions (currently w/o version)
// *************************************************************************
// rate is OFDM-HE
#define IS_RATE_OFDM_HE_API_M(c_rate) ((c_rate.rate_n_flags) & RATE_MCS_HE_MSK)
// rate is OFDM VHT/HE
#define IS_RATE_OFDM_VHT_HE_API_M(c_rate) \
  (((c_rate).rate_n_flags) & (RATE_MCS_VHT_MSK | RATE_MCS_HE_MSK))
// rate is OFDM HT/VHT/HE
#define IS_RATE_OFDM_HT_VHT_HE_API_M(c_rate) \
  ((c_rate) & (RATE_MCS_HT_MSK | RATE_MCS_VHT_MSK | RATE_MCS_HE_MSK))
// rate is OFDM HE and STBC (note should check no DCM, as STBC & DCM mark special GI/HE-LTF)
// basically don't support this combo for now.
#define IS_RATE_OFDM_HE_STBC_API_M(c_rate) \
  ((((c_rate.rate_n_flags) & (RATE_MCS_STBC_MSK | RATE_MCS_HE_MSK | RATE_MCS_HE_DCM_MSK))) == (RATE_MCS_STBC_MSK | RATE_MCS_HE_MSK))
// number of space time streams
#define GET_NUM_OF_HE_SPACE_TIME_STREAMS_API_M(c_rate) \
  ((c_rate.rate_n_flags) & (RATE_MCS_STBC_MSK | RATE_MCS_VHT_MIMO_MSK)) ? 2 : 1
// rate is OFDM-HE w/ DCM (dual carrier mode)
#define IS_RATE_OFDM_HE_DCM_API_M(c_rate) (c_rate.rate_n_flags & RATE_MCS_HE_DCM_MSK)
#define GET_OFDM_HE_DCM_API_M(c_rate) SHIFT_AND_MASK(c_rate.rate_n_flags, RATE_MCS_HE_DCM_MSK, RATE_MCS_HE_DCM_POS)
// rate is OFDM-HE ext-range using 106-tones (i.e. same as RU 8MHz 102 data-tones)
#define IS_RATE_OFDM_HE_ER_106_API_M(c_rate) (c_rate.rate_n_flags & RATE_MCS_HE_ER_106_MSK)
#define GET_OFDM_HE_ER_106_API_M(c_rate) SHIFT_AND_MASK(c_rate.rate_n_flags, RATE_MCS_HE_ER_106_MSK, RATE_MCS_HE_ER_106_POS)
// get OFDM-HE HE-LTF size / GI size
#define GET_OFDM_HE_GI_LTF_INDX_API_M(c_rate) SHIFT_AND_MASK(c_rate.rate_n_flags, RATE_MCS_HE_GI_LTF_MSK, RATE_MCS_HE_GI_LTF_POS)
// get OFDM-HE type: SU, extended-range, MU, TRIG
#define GET_OFDM_VHT_HE_TYPE_API_M(c_rate) SHIFT_AND_MASK(c_rate.rate_n_flags, RATE_MCS_VHT_HE_TYPE_MSK, RATE_MCS_VHT_HE_TYPE_POS)
// check if OFDM-VHT/HE single-user
#define IS_RATE_OFDM_VHT_HE_SU_API_M(c_rate) (GET_OFDM_VHT_HE_TYPE_API_M(c_rate) == RATE_MCS_VHT_HE_SU)
// check if OFDM-HE single-user extended range
#define IS_RATE_OFDM_HE_EXT_RANGE_API_M(c_rate) (GET_OFDM_VHT_HE_TYPE_API_M(c_rate) == RATE_MCS_HE_EXT_RANGE)
// check if OFDM-HE trigger frame based
#define IS_RATE_OFDM_HE_TRIG_BASE_API_M(c_rate) (GET_OFDM_VHT_HE_TYPE_API_M(c_rate) == RATE_MCS_HE_TRIG_BASE)
// check if rate is VHT/HE multi-user
#define IS_RATE_OFDM_VHT_HE_MU_API_M(c_rate) (GET_OFDM_VHT_HE_TYPE_API_M(c_rate) == RATE_MCS_VHT_HE_MU)
// get the MCS index for HT/VHT/HE
#define GET_HT_VHT_HE_RATE_CODE_API_M(c_rate) GET_HT_VHT_RATE_CODE_API_M_VER_1(c_rate)

// vht supported
#define GET_MIMO_INDEX_API_M_VER_3(c_rate) \
  (IS_RATE_OFDM_HT_API_M_VER_2(c_rate) ? GET_HT_MIMO_INDEX_API_M_VER_1(c_rate) : \
   IS_RATE_OFDM_VHT_HE_API_M(c_rate) ? GET_VHT_MIMO_INDX_API_M_VER_1(c_rate) : SISO_INDX)


/**@} GroupRates */


#endif // API_RATES_H
