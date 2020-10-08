// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

class SetKeysTest : public SimTest {
 public:
  SetKeysTest() = default;
  void SetUp() override {
    ASSERT_EQ(ZX_OK, SimTest::Init());
    ASSERT_EQ(ZX_OK, StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_));
  }
  void TearDown() override { SimTest::DeleteInterface(&client_ifc_); }

  SimInterface client_ifc_;
};

wlanif_set_keys_req FakeSetKeysRequest(const uint8_t keys[][WLAN_MAX_KEY_LEN], size_t n) {
  wlanif_set_keys_req set_keys_req{.num_keys = n};

  for (size_t i = 0; i < n; i++) {
    set_keys_req.keylist[i] = {
        .key_list = keys[i],
        .key_count = strlen(reinterpret_cast<const char*>(keys[i])),
        .key_id = static_cast<uint16_t>(i),
        .cipher_suite_type = WPA_CIPHER_CCMP_128,
    };
  }
  return set_keys_req;
}

TEST_F(SetKeysTest, MultipleKeys) {
  const uint8_t keys[WLAN_MAX_KEYLIST_SIZE][WLAN_MAX_KEY_LEN] = {"One", "Two", "Three", "Four"};
  wlanif_set_keys_req set_keys_req = FakeSetKeysRequest(keys, WLAN_MAX_KEYLIST_SIZE);
  client_ifc_.if_impl_ops_->set_keys_req(client_ifc_.if_impl_ctx_, &set_keys_req);

  std::vector<brcmf_wsec_key_le> firmware_keys =
      device_->GetSim()->sim_fw->GetKeyList(client_ifc_.iface_id_);
  ASSERT_EQ(firmware_keys.size(), WLAN_MAX_KEYLIST_SIZE);
  EXPECT_STREQ(reinterpret_cast<const char*>(firmware_keys[0].data), "One");
  EXPECT_STREQ(reinterpret_cast<const char*>(firmware_keys[1].data), "Two");
  EXPECT_STREQ(reinterpret_cast<const char*>(firmware_keys[2].data), "Three");
  EXPECT_STREQ(reinterpret_cast<const char*>(firmware_keys[3].data), "Four");
}

// Ensure that group key is set correctly by the driver in firmware.
TEST_F(SetKeysTest, SetGroupKey) {
  const uint8_t group_key[WLAN_MAX_KEY_LEN] = "Group Key";
  const uint8_t ucast_key[WLAN_MAX_KEY_LEN] = "My Key";
  wlanif_set_keys_req key_req = {
      .num_keys = 2,
      .keylist[0] =
          {
              .key_list = group_key,
              .key_count = strlen(reinterpret_cast<const char*>(group_key)),
              .key_id = 0,
              .address = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
              .cipher_suite_type = WPA_CIPHER_CCMP_128,
          },
      .keylist[1] =
          {
              .key_list = ucast_key,
              .key_count = strlen(reinterpret_cast<const char*>(ucast_key)),
              .key_id = 1,
              .address = {0xde, 0xad, 0xbe, 0xef, 0xab, 0xcd},
              .cipher_suite_type = WPA_CIPHER_CCMP_128,
          },
  };
  client_ifc_.if_impl_ops_->set_keys_req(client_ifc_.if_impl_ctx_, &key_req);

  std::vector<brcmf_wsec_key_le> firmware_keys =
      device_->GetSim()->sim_fw->GetKeyList(client_ifc_.iface_id_);
  ASSERT_EQ(firmware_keys.size(), 2U);
  EXPECT_STREQ(reinterpret_cast<const char*>(firmware_keys[0].data),
               reinterpret_cast<const char*>(group_key));
  // Group key should have been set as the PRIMARY KEY
  ASSERT_EQ(firmware_keys[0].flags, (const unsigned int)BRCMF_PRIMARY_KEY);
  EXPECT_STREQ(reinterpret_cast<const char*>(firmware_keys[1].data),
               reinterpret_cast<const char*>(ucast_key));
  ASSERT_NE(firmware_keys[1].flags, (const unsigned int)BRCMF_PRIMARY_KEY);
}
}  // namespace wlan::brcmfmac
