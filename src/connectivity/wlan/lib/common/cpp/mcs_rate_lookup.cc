// Copyright (c) 2020 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <wlan/common/logging.h>
#include <wlan/common/mcs_rate_lookup.h>

namespace wlan::common {

namespace {

// HT has 2 valid guard intervals.
const uint8_t kHtGuardIntervalCount = 2;
using ht_mcs_entry_t = uint32_t[kHtGuardIntervalCount];
// When accessing items in ht_mcs_entry_t, the rates are indexed by guard interval.
const uint8_t kLongGiIndex = 0;
const uint8_t kShortGiIndex = 1;

#define HT_INVALID_RATE 0
// Where an HT MCS index is invalid, the lookup table will return this placeholder.
#define HT_MCS_INVALID \
  { HT_INVALID_RATE, HT_INVALID_RATE }

// All HT tables have the same number of MCS indices, 0-76.
const uint8_t kHtMaxMcs = 76;
const uint8_t kHtMaxMcsCount = 77;

using ht_per_bandwidth_table_t = ht_mcs_entry_t[kHtMaxMcsCount];

// These are MCS rate lookup tables for HT, one table per channel bandwidth.
// Each table is a multidimensional array, indexed by MCS, and then GI.
// For example, to lookup the rate for 40 MHz channel bandwidth, MCS 8, and short GI:
//     uint8_t mcs = 8;
//     uint8_t gi = kShortGiIndex;
//     uint32_t rate = kHt40mhzRateLookup[mcs][kShortGiIndex];
const ht_per_bandwidth_table_t kHt20mhzRateLookup = {
    // IEEE 802.11-2016 Table 19-27. NSS=1, NES=1
    {6500, 7200},  // MCS index 0
    {13000, 14400},
    {19500, 21700},
    {26000, 28900},
    {39000, 43300},
    {52000, 57800},
    {58500, 65000},
    {65000, 72200},
    // IEEE 802.11-2016 Table 19-28. NSS=2, NES=1, EQM
    {13000, 14400},  // MCS index 8
    {26000, 28900},
    {39000, 43300},
    {52000, 57800},
    {78000, 86700},
    {104000, 115600},
    {117000, 130000},
    {130000, 144400},
    // IEEE 802.11-2016 Table 19-29. NSS=3, NES=1, EQM
    {19500, 21700},  // MCS index 16
    {39000, 43300},
    {58500, 65000},
    {78000, 86700},
    {117000, 130000},
    {156000, 173300},
    {175500, 195000},
    {195000, 216700},
    // IEEE 802.11-2016 Table 19-30. NSS=4, NES=1, EQM
    {26000, 28900},  // MCS index 24
    {52000, 57800},
    {78000, 86700},
    {104000, 115600},
    {156000, 173300},
    {208000, 231100},
    {234000, 260000},
    {260000, 288900},
    // MCS index 32, which is not defined
    HT_MCS_INVALID,  // MCS index 32
    // IEEE 802.11-2016 Table 19-36. NSS=2, NES=1, UEQM
    {39000, 43300},  // MCS index 33
    {52000, 57800},
    {65000, 72200},
    {58500, 65000},
    {78000, 86700},
    {97500, 108300},
    // IEEE 802.11-2016 Table 19-37. NSS=3, NES=1, UEQM
    {52000, 57800},  // MCS index 39
    {65000, 72200},
    {65000, 72200},
    {78000, 86700},
    {91000, 101100},
    {91000, 101100},
    {104000, 115600},
    {78000, 86700},
    {97500, 108300},
    {97500, 108300},
    {117000, 130000},
    {136500, 151700},
    {136500, 151700},
    {156000, 173300},
    // IEEE 802.11-2016 Table 19-38. NSS=4, NES=1, UEQM
    {65000, 72200},  // MCS index 53
    {78000, 86700},
    {91000, 101100},
    {78000, 86700},
    {91000, 101100},
    {104000, 115600},
    {117000, 130000},
    {104000, 115600},
    {117000, 130000},
    {130000, 144400},
    {130000, 144400},
    {143000, 158900},
    {97500, 108300},
    {117000, 130000},
    {136500, 151700},
    {117000, 130000},
    {136500, 151700},
    {156000, 173300},
    {175500, 195000},
    {156000, 173300},
    {175500, 195000},
    {195000, 216700},
    {195000, 216700},
    {214500, 238200}};  // MCS index 76

const ht_per_bandwidth_table_t kHt40mhzRateLookup = {
    // IEEE 802.11-2016 Table 19-31. NSS=1, NES=1
    {13500, 15000},  // MCS index 0
    {27000, 30000},
    {40500, 45000},
    {54000, 60000},
    {81000, 90000},
    {108000, 120000},
    {121500, 135000},
    {135000, 150000},
    // IEEE 802.11-2016 Table 19-32. NSS=2, NES=1, EQM
    {27000, 30000},  // MCS index 8
    {54000, 60000},
    {81000, 90000},
    {108000, 120000},
    {162000, 180000},
    {216000, 240000},
    {243000, 270000},
    {270000, 300000},
    // IEEE 802.11-2016 Table 19-33. NSS=3, NES=1, EQM
    {40500, 45000},  // MCS index 16
    {81000, 90000},
    {121500, 135000},
    {162000, 180000},
    {243000, 270000},
    {324000, 360000},
    {364500, 405000},
    {405000, 450000},
    // IEEE 802.11-2016 Table 19-34. NSS=4, NES=1, EQM
    {54000, 60000},  // MCS index 24
    {108000, 120000},
    {162000, 180000},
    {216000, 240000},
    {324000, 360000},
    {432000, 480000},
    {486000, 540000},
    {540000, 600000},
    // IEEE 802.11-2016 Table 19-35. NSS=1, NES=1, MCS 32 format
    {6000, 6700},  // MCS index 32
    // IEEE 802.11-2016 Table 19-39. NSS=2, NES=1, UEQM
    {81000, 90000},  // MCS index 33
    {108000, 120000},
    {135000, 150000},
    {121500, 135000},
    {162000, 180000},
    {202500, 225000},
    // IEEE 802.11-2016 Table 19-40. NSS=3, UEQM
    {108000, 120000},  // MCS index 39
    {135000, 150000},
    {135000, 150000},
    {162000, 180000},
    {189000, 210000},
    {189000, 210000},
    {216000, 240000},
    {162000, 180000},
    {202500, 225000},
    {202500, 225000},
    {243000, 270000},
    {283500, 315000},
    {283500, 315000},
    {324000, 360000},
    // IEEE 802.11-2016 Table 19-41. NSS=4, UEQM
    {135000, 150000},  // MCS index 53
    {162000, 180000},
    {189000, 210000},
    {162000, 180000},
    {189000, 210000},
    {216000, 240000},
    {243000, 270000},
    {216000, 240000},
    {243000, 270000},
    {270000, 300000},
    {270000, 300000},
    {297000, 330000},
    {202500, 225000},
    {243000, 270000},
    {283500, 315000},
    {243000, 270000},
    {283500, 315000},
    {324000, 360000},
    {364500, 405000},
    {324000, 360000},
    {364500, 405000},
    {405000, 450000},
    {405000, 450000},
    {445500, 495000}};  // MCS index 76

// These are MCS rate lookup tables for VHT, one table per channel bandwidth.
// Each table is a multidimensional array, indexed by NSS, then MCS, and GI.
// For example, to lookup the rate for 20 MHz channel bandwidth, NSS 2, MCS 8, and short GI:
//     uint8_t nss = 2;
//     uint8_t mcs = 8;
//     uint8_t gi = kShortGiIndex;
//     uint32_t rate = VkHt20mhzRateLookup[nss][mcs][kShortGiIndex];

// VHT has 2 valid guard intervals.
const uint8_t kVhtGuardIntervalCount = 2;
// Like ht_mcs_entry_t, each vht_mcs_entry_t is indexed by guard interval.
using vht_mcs_entry_t = uint32_t[kVhtGuardIntervalCount];

// Some MCS indices are invalid, so these entries are filled with a placeholder.
#define VHT_INVALID_RATE 0
#define VHT_MCS_INVALID \
  { VHT_INVALID_RATE, VHT_INVALID_RATE }

// Valid MCS index range is 0-9.
const uint8_t kVhtMaxMcs = 9;
const uint8_t kVhtMaxMcsCount = 10;
using vht_per_nss_table_t = vht_mcs_entry_t[kVhtMaxMcsCount];

// The per-NSS tables have 1 extra entry for NSS = 0, which is invalid. The NSS=0 entry is just a
// placeholder to make it easy to use the actual NSS value as an index.
const uint8_t kVhtPerNssSize = 9;
using vht_per_bandwidth_table_t = vht_per_nss_table_t[kVhtPerNssSize];
const vht_per_bandwidth_table_t kVht20mhzRateLookup{
    // NSS = 0 is invalid, so we have one empty entry.
    {},
    // IEEE 802.11-2016 Table 21-30. NSS = 1
    {{6500, 7200},
     {13000, 14400},
     {19500, 21700},
     {26000, 28900},
     {39000, 43300},
     {52000, 57800},
     {58500, 65000},
     {65000, 72200},
     {78000, 86700},
     VHT_MCS_INVALID},
    // IEEE 802.11-2016 Table 21-31. NSS = 2
    {{13000, 14400},
     {26000, 28900},
     {39000, 43300},
     {52000, 57800},
     {78000, 86700},
     {104000, 115600},
     {117000, 130000},
     {130000, 144400},
     {156000, 173300},
     VHT_MCS_INVALID},
    // IEEE 802.11-2016 Table 21-32. NSS = 3
    {{19500, 21700},
     {39000, 43300},
     {58500, 65000},
     {78000, 86700},
     {117000, 130000},
     {156000, 173300},
     {175500, 195000},
     {195000, 216700},
     {234000, 260000},
     {260000, 288900}},
    // IEEE 802.11-2016 Table 21-33. NSS = 4
    {{26000, 28900},
     {52000, 57800},
     {78000, 86700},
     {104000, 115600},
     {156000, 173300},
     {208000, 231100},
     {234000, 260000},
     {260000, 288900},
     {312000, 346700},
     VHT_MCS_INVALID},
    // IEEE 802.11-2016 Table 21-34. NSS = 5
    {{32500, 36100},
     {65000, 72200},
     {97500, 108300},
     {130000, 144400},
     {195000, 216700},
     {260000, 288900},
     {292500, 325000},
     {325000, 361100},
     {390000, 433300},
     VHT_MCS_INVALID},
    // IEEE 802.11-2016 Table 21-35. NSS = 6
    {{39000, 43300},
     {78000, 86700},
     {117000, 130000},
     {156000, 173300},
     {234000, 260000},
     {312000, 346700},
     {351000, 390000},
     {390000, 433300},
     {468000, 520000},
     {520000, 577800}},
    // IEEE 802.11-2016 Table 21-36. NSS = 7
    {{45500, 50600},
     {91000, 101100},
     {136500, 151700},
     {182000, 202200},
     {273000, 303300},
     {364000, 404400},
     {409500, 455000},
     {455000, 505600},
     {546000, 606700},
     VHT_MCS_INVALID},
    // IEEE 802.11-2016 Table 21-37. NSS = 8
    {{52000, 57800},
     {104000, 115600},
     {156000, 173300},
     {208000, 231100},
     {312000, 346700},
     {416000, 462200},
     {468000, 520000},
     {520000, 577800},
     {624000, 693300},
     VHT_MCS_INVALID}};

const vht_per_bandwidth_table_t kVht40mhzRateLookup = {
    // NSS = 0 is invalid, so we have one empty entry.
    {},
    // IEEE 802.11-2016 Table 21-38. NSS = 1
    {{13500, 15000},
     {27000, 30000},
     {40500, 45000},
     {54000, 60000},
     {81000, 90000},
     {108000, 120000},
     {121500, 135000},
     {135000, 150000},
     {162000, 180000},
     {180000, 200000}},
    // IEEE 802.11-2016 Table 21-39. NSS = 2
    {{27000, 30000},
     {54000, 60000},
     {81000, 90000},
     {108000, 120000},
     {162000, 180000},
     {216000, 240000},
     {243000, 270000},
     {270000, 300000},
     {324000, 360000},
     {360000, 400000}},
    // IEEE 802.11-2016 Table 21-40. NSS = 3
    {{40500, 45000},
     {81000, 90000},
     {121500, 135000},
     {162000, 180000},
     {243000, 270000},
     {324000, 360000},
     {364500, 405000},
     {405000, 450000},
     {486000, 540000},
     {540000, 600000}},
    // IEEE 802.11-2016 Table 21-41. NSS = 4
    {{54000, 60000},
     {108000, 120000},
     {162000, 180000},
     {216000, 240000},
     {324000, 360000},
     {432000, 480000},
     {486000, 540000},
     {540000, 600000},
     {648000, 720000},
     {720000, 800000}},
    // IEEE 802.11-2016 Table 21-42. NSS = 5
    {{67500, 75000},
     {135000, 150000},
     {202500, 225000},
     {270000, 300000},
     {405000, 450000},
     {540000, 600000},
     {607500, 675000},
     {675000, 750000},
     {810000, 900000},
     {900000, 1000000}},
    // IEEE 802.11-2016 Table 21-43. NSS = 6
    {{81000, 90000},
     {162000, 180000},
     {243000, 270000},
     {324000, 360000},
     {486000, 540000},
     {648000, 720000},
     {729000, 810000},
     {810000, 900000},
     {972000, 1080000},
     {1080000, 1200000}},
    // IEEE 802.11-2016 Table 21-44. NSS = 7
    {{94500, 105000},
     {189000, 210000},
     {283500, 315000},
     {378000, 420000},
     {567000, 630000},
     {756000, 840000},
     {850500, 945000},
     {945000, 1050000},
     {1134000, 1260000},
     {1260000, 1400000}},
    // IEEE 802.11-2016 Table 21-45. NSS = 8
    {{108000, 120000},
     {216000, 240000},
     {324000, 360000},
     {432000, 480000},
     {648000, 720000},
     {864000, 960000},
     {972000, 1080000},
     {1080000, 1200000},
     {1296000, 1440000},
     {1440000, 1600000}},
};

const vht_per_bandwidth_table_t kVht80mhzRateLookup = {
    // NSS = 0 is invalid, so we have one empty entry.
    {},
    // IEEE 802.11-2016 Table 21-46. NSS = 1.
    {{29300, 32500},
     {58500, 65000},
     {87800, 97500},
     {117000, 130000},
     {175500, 195000},
     {234000, 260000},
     {263300, 292500},
     {292500, 325000},
     {351000, 390000},
     {390000, 433300}},
    // IEEE 802.11-2016 Table 21-47. NSS = 2
    {{58500, 65000},
     {117000, 130000},
     {175500, 195000},
     {234000, 260000},
     {351000, 390000},
     {468000, 520000},
     {526500, 585000},
     {585000, 650000},
     {702000, 780000},
     {780000, 866700}},
    // IEEE 802.11-2016 Table 21-48. NSS = 3
    {{87800, 97500},
     {175500, 195000},
     {263300, 292500},
     {351000, 390000},
     {526500, 585000},
     {702000, 780000},
     VHT_MCS_INVALID,
     {877500, 975000},
     {1053000, 1170000},
     {1170000, 1300000}},
    // IEEE 802.11-2016 Table 21-49. NSS = 4
    {{117000, 130000},
     {234000, 260000},
     {351000, 390000},
     {468000, 520000},
     {702000, 780000},
     {936000, 1040000},
     {1053000, 1170000},
     {1170000, 1300000},
     {1404000, 1560000},
     {1560000, 1733300}},
    // IEEE 802.11-2016 Table 21-50. NSS = 5
    {{146300, 162500},
     {292500, 325000},
     {438800, 487500},
     {585000, 650000},
     {877500, 975000},
     {1170000, 1300000},
     {1316300, 1462500},
     {1462500, 1625000},
     {1755000, 1950000},
     {1950000, 2166700}},
    // IEEE 802.11-2016 Table 21-51. NSS = 6
    {{175500, 195000},
     {351000, 390000},
     {526500, 585000},
     {702000, 780000},
     {1053000, 1170000},
     {1404000, 1560000},
     {1579500, 1755000},
     {1755000, 1950000},
     {2106000, 2340000}},
    // IEEE 802.11-2016 Table 21-52. NSS = 7
    {{204800, 227500},
     {409500, 455000},
     {614300, 682500},
     {819000, 910000},
     {1228500, 1365000},
     {1638000, 1820000},
     VHT_MCS_INVALID,
     {2047500, 2275000},
     {2457000, 2730000},
     {2730000, 3033300}},
    // IEEE 802.11-2016 Table 21-53. NSS = 8
    {{234000, 260000},
     {468000, 520000},
     {702000, 780000},
     {936000, 1040000},
     {1404000, 1560000},
     {1872000, 2080000},
     {2106000, 2340000},
     {2340000, 2600000},
     {2808000, 3120000},
     {3120000, 3466700}}};

// Note this lookup table is also valid for channel bandwidth = 80+80 MHz.
const vht_per_bandwidth_table_t kVht160mhzRateLookup = {
    // NSS = 0 is invalid, so we have one empty entry.
    {},
    // IEEE 802.11-2016 Table 21-54. NSS = 1
    {{58500, 65000},
     {117000, 130000},
     {175500, 195000},
     {234000, 260000},
     {351000, 390000},
     {468000, 520000},
     {526500, 585000},
     {585000, 650000},
     {702000, 780000},
     {780000, 866700}},
    // IEEE 802.11-2016 Table 21-55. NSS = 2
    {{117000, 130000},
     {234000, 260000},
     {351000, 390000},
     {468000, 520000},
     {702000, 780000},
     {936000, 1040000},
     {1053000, 1170000},
     {1170000, 1300000},
     {1404000, 1560000},
     {1560000, 1733300}},
    // IEEE 802.11-2016 Table 21-56. NSS = 3
    {{175500, 195000},
     {351000, 390000},
     {526500, 585000},
     {702000, 780000},
     {1053000, 1170000},
     {1404000, 1560000},
     {1579500, 1755000},
     {1755000, 1950000},
     {2106000, 2340000},
     VHT_MCS_INVALID},
    // IEEE 802.11-2016 Table 21-57. NSS = 4
    {{234000, 260000},
     {468000, 520000},
     {702000, 780000},
     {936000, 1040000},
     {1404000, 1560000},
     {1872000, 2080000},
     {2106000, 2340000},
     {2340000, 2600000},
     {2808000, 3120000},
     {3120000, 3466700}},
    // IEEE 802.11-2016 Table 21-58. NSS = 5
    {{292500, 325000},
     {585000, 650000},
     {877500, 975000},
     {1170000, 1300000},
     {1755000, 1950000},
     {2340000, 2600000},
     {2632500, 2925000},
     {2925000, 3250000},
     {3510000, 3900000},
     {3900000, 4333300}},
    // IEEE 802.11-2016 Table 21-59. NSS = 6
    {{351000, 390000},
     {702000, 780000},
     {1053000, 1170000},
     {1404000, 1560000},
     {2106000, 2340000},
     {2808000, 3120000},
     {3159000, 3510000},
     {3510000, 3900000},
     {4212000, 4680000},
     {4680000, 5200000}},
    // IEEE 802.11-2016 Table 21-60. NSS = 7
    {{409500, 455000},
     {819000, 910000},
     {1228500, 1365000},
     {1638000, 1820000},
     {2457000, 2730000},
     {3276000, 3640000},
     {3685500, 4095000},
     {4095000, 4550000},
     {4914000, 5460000},
     {5460000, 6066700}},
    // IEEE 802.11-2016 Table 21-61. NSS = 8
    {{468000, 520000},
     {936000, 1040000},
     {1404000, 1560000},
     {1872000, 2080000},
     {2808000, 3120000},
     {3744000, 4160000},
     {4212000, 4680000},
     {4680000, 5200000},
     {5616000, 6240000},
     {6240000, 6933300}}};

zx_status_t ValidateHtLookupRequestBounds(const ::fuchsia::wlan::common::ChannelBandwidth& cbw,
                                          uint8_t mcs,
                                          const ::fuchsia::wlan::common::GuardInterval& gi) {
  auto status = ZX_OK;
  if (mcs >= kHtMaxMcsCount) {
    status = ZX_ERR_OUT_OF_RANGE;
    errorf("Invalid HT MCS index %d (%s)\n", mcs, zx_status_get_string(status));
    return status;
  }

  // HT PHY channel bandwidth must be either 20 MHz or 40 MHz, see IEEE 802.11 19.1.1.
  if (cbw != ::fuchsia::wlan::common::ChannelBandwidth::CBW20 &&
      cbw != ::fuchsia::wlan::common::ChannelBandwidth::CBW40) {
    status = ZX_ERR_OUT_OF_RANGE;
    errorf("Invalid HT channel bandwidth (%s)\n", zx_status_get_string(status));
    return status;
  }

  // HT PHY guard intervals must be either short or long, see IEEE 802.11-2016 19.1.1.
  if (gi != ::fuchsia::wlan::common::GuardInterval::SHORT_GI &&
      gi != ::fuchsia::wlan::common::GuardInterval::LONG_GI) {
    status = ZX_ERR_OUT_OF_RANGE;
    errorf("Invalid HT guard interval (%s)\n", zx_status_get_string(status));
    return status;
  }

  // Valid MCS indices are specified in IEEE 802.11-2016 19.5.
  // The MCS table for each channel bandwidth has MCS indices 0-76, which may include entries
  // for invalid combinations of parameters. Here we are just checking bounds.
  if (mcs > kHtMaxMcs) {
    errorf("Invalid HT MCS index %d (%s)\n", mcs, zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t ValidateVhtLookupRequestBounds(const ::fuchsia::wlan::common::ChannelBandwidth& cbw,
                                           uint8_t mcs,
                                           const ::fuchsia::wlan::common::GuardInterval& gi,
                                           uint8_t nss) {
  auto status = ZX_OK;
  if (nss < 1 || nss > 8) {
    status = ZX_ERR_OUT_OF_RANGE;
    errorf("Invalid VHT NSS: %d (%s)", nss, zx_status_get_string(status));
    return status;
  }

  // VHT PHY guard intervals must be either short or long, see IEEE 802.11-2016 21.1.1.
  if (gi != ::fuchsia::wlan::common::GuardInterval::SHORT_GI &&
      gi != ::fuchsia::wlan::common::GuardInterval::LONG_GI) {
    status = ZX_ERR_OUT_OF_RANGE;
    errorf("Invalid VHT guard interval (%s)\n", zx_status_get_string(status));
    return status;
  }

  // Valid MCS indices are specified in IEEE 802.11-2016 21.5.
  // The MCS table for each channel bandwidth has MCS indices 0-9, which may include entries
  // for invalid combinations of parameters. Here we are just checking bounds.
  if (mcs > kVhtMaxMcs) {
    status = ZX_ERR_OUT_OF_RANGE;
    errorf("Invalid VHT MCS index %d (%s)\n", mcs, zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

// Translate the GI enum value into an index into the lookup table.
zx_status_t GiEnumToIndex(::fuchsia::wlan::common::GuardInterval gi, uint8_t* gi_index) {
  if (gi == ::fuchsia::wlan::common::GuardInterval::LONG_GI) {
    *gi_index = kLongGiIndex;
  } else if (gi == ::fuchsia::wlan::common::GuardInterval::SHORT_GI) {
    *gi_index = kShortGiIndex;
  } else {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

}  // namespace

zx_status_t HtDataRateLookup(const ::fuchsia::wlan::common::ChannelBandwidth& cbw, uint8_t mcs,
                             const ::fuchsia::wlan::common::GuardInterval& gi, uint32_t* out_kbps) {
  auto status = ValidateHtLookupRequestBounds(cbw, mcs, gi);
  if (status != ZX_OK) {
    return status;
  }
  uint8_t gi_index;
  status = GiEnumToIndex(gi, &gi_index);
  if (status != ZX_OK) {
    return status;
  }
  uint32_t rate_kbps = HT_INVALID_RATE;
  switch (cbw) {
    case ::fuchsia::wlan::common::ChannelBandwidth::CBW20:
      rate_kbps = kHt20mhzRateLookup[mcs][gi_index];
      break;
    case ::fuchsia::wlan::common::ChannelBandwidth::CBW40:
      rate_kbps = kHt40mhzRateLookup[mcs][gi_index];
      break;
    default:
      // The validation above should have prevented failed lookups.
      errorf("Invalid HT channel bandwidth value: %d\n", cbw);
      return (ZX_ERR_INTERNAL);
  }
  // Check for invalid rates, which exist for some combinations of parameters.
  if (rate_kbps == HT_INVALID_RATE) {
    errorf("Invalid HT MCS index %d (%s)\n", mcs, zx_status_get_string(status));
    status = ZX_ERR_OUT_OF_RANGE;
  } else {
    *out_kbps = rate_kbps;
  }
  return status;
}

zx_status_t VhtDataRateLookup(const ::fuchsia::wlan::common::ChannelBandwidth& cbw, uint8_t mcs,
                              const ::fuchsia::wlan::common::GuardInterval& gi, uint8_t num_sts,
                              uint8_t stbc, uint32_t* out_kbps) {
  // IEEE 802.11-2016 8.3.4.4 defines this formula for finding the VHT NSS.
  const uint8_t nss = num_sts / (stbc + 1);
  return VhtDataRateLookup(cbw, mcs, gi, nss, out_kbps);
}

zx_status_t VhtDataRateLookup(const ::fuchsia::wlan::common::ChannelBandwidth& cbw, uint8_t mcs,
                              const ::fuchsia::wlan::common::GuardInterval& gi, uint8_t nss,
                              uint32_t* out_kbps) {
  auto status = ValidateVhtLookupRequestBounds(cbw, mcs, gi, nss);
  if (status != ZX_OK) {
    return status;
  }
  uint8_t gi_index;
  status = GiEnumToIndex(gi, &gi_index);
  if (status != ZX_OK) {
    return status;
  }
  uint32_t rate_kbps = VHT_INVALID_RATE;
  switch (cbw) {
    case ::fuchsia::wlan::common::ChannelBandwidth::CBW20:
      rate_kbps = kVht20mhzRateLookup[nss][mcs][gi_index];
      break;
    case ::fuchsia::wlan::common::ChannelBandwidth::CBW40:
      rate_kbps = kVht40mhzRateLookup[nss][mcs][gi_index];
      break;
    case ::fuchsia::wlan::common::ChannelBandwidth::CBW80:
      rate_kbps = kVht80mhzRateLookup[nss][mcs][gi_index];
      break;
    case ::fuchsia::wlan::common::ChannelBandwidth::CBW80P80:
      __FALLTHROUGH;  // 80+80 uses the same lookup table as 160 MHz.
    case ::fuchsia::wlan::common::ChannelBandwidth::CBW160:
      rate_kbps = kVht160mhzRateLookup[nss][mcs][gi_index];
      break;
    default:
      // The validation above should have prevented failed lookups.
      errorf("Invalid VHT channel bandwidth value: %d\n", cbw);
      return (ZX_ERR_INTERNAL);
  }
  // Check for invalid rates, which exist for some combinations of parameters.
  if (rate_kbps == VHT_INVALID_RATE) {
    errorf("Invalid VHT MCS index %d (%s)\n", mcs, zx_status_get_string(status));
    status = ZX_ERR_OUT_OF_RANGE;
  } else {
    *out_kbps = rate_kbps;
  }
  return status;
}

}  // namespace wlan::common
