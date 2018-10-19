// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_TESTS_TEST_DATA_H_
#define GARNET_LIB_WLAN_MLME_TESTS_TEST_DATA_H_

#include <cstdint>
#include <utility>
#include <vector>

namespace wlan {
namespace test_data {

extern std::vector<uint8_t> kBeaconFrame;
extern std::vector<uint8_t> kBlockAckUnsupportedFrame;
extern std::vector<uint8_t> kDeauthFrame;
extern std::vector<uint8_t> kDeauthFrame10BytePadding;
extern std::vector<uint8_t> kActionFrame;
extern std::vector<uint8_t> kProbeRequestFrame;
extern std::vector<uint8_t> kAssocReqFrame;
extern std::vector<uint8_t> kAssocRespFrame;
extern std::vector<uint8_t> kAuthFrame;
extern std::vector<uint8_t> kDisassocFrame;
extern std::vector<uint8_t> kAddBaReqFrame;
extern std::vector<uint8_t> kAddBaReqBody;
extern std::vector<uint8_t> kAddBaRespFrame;
extern std::vector<uint8_t> kAddBaRespBody;
extern std::vector<uint8_t> kQosDataFrame;
extern std::vector<uint8_t> kQosNullDataFrame;
extern std::vector<uint8_t> kDataFrame;
extern std::vector<uint8_t> kNullDataFrame;
extern std::vector<uint8_t> kPsPollFrame;
extern std::vector<uint8_t> kPsPollHtcUnsupportedFrame;
extern std::vector<uint8_t> kEthernetFrame;
extern std::vector<uint8_t> kAmsduDataFrame;
extern std::vector<uint8_t> kDataLlcEapolFrame;

}  // namespace test_data
}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_TESTS_TEST_DATA_H_
