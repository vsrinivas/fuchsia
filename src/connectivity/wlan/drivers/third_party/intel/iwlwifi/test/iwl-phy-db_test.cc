// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used to test iwl-phy-db.c.
//
// A note for the GCC static initialization of flexible array members:
//
//   Since 'struct iwl_calib_res_notif_phy_db' contains a zero-length-array, this requires a special
//   way to assign initialization value for it. See
//
//      https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html
//
#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-phy-db.h"
}

namespace wlan {
namespace testing {
namespace {

class IwlPhyDbTest : public ::zxtest::Test {
 public:
  IwlPhyDbTest() {}
  ~IwlPhyDbTest() {}
};

// Expect the iwl_phy_db_send_all_channel_groups() returns ZX_OK right after init before there is
// no data set yet. This is a regression test to catch a bug involving underflow on the channel
// argument to iwl_phy_db_send_all_channel_groups().
TEST_F(IwlPhyDbTest, TestSendAllChannelGroupsAfterInit) {
  struct iwl_phy_db* phy_db = iwl_phy_db_init(nullptr);
  EXPECT_NE(nullptr, phy_db);

  EXPECT_EQ(ZX_OK, iwl_phy_db_send_all_channel_groups(phy_db, IWL_PHY_DB_CALIB_CHG_PAPD, -1));
  EXPECT_EQ(ZX_OK, iwl_phy_db_send_all_channel_groups(phy_db, IWL_PHY_DB_CALIB_CHG_TXP, -1));

  iwl_phy_db_free(phy_db);
}

// Test setting the IWL_PHY_DB_CFG section
TEST_F(IwlPhyDbTest, SetSectionCfg) {
  struct iwl_phy_db* phy_db = iwl_phy_db_init(nullptr);
  EXPECT_EQ(nullptr, phy_db->calib_ch_group_papd);
  EXPECT_EQ(-1, phy_db->n_group_papd);

  // A packet to set the cfg db.
  const size_t chg_id_size = 7;  // Just an arbitrary value.
  struct {
    struct iwl_rx_packet hdr;  // must be at begin of struct.
    struct {                   // equal to the 'data' field.
      struct iwl_calib_res_notif_phy_db hdr;
      uint8_t data[chg_id_size];
    } phy_db_notif;
  } pkt = {
      .phy_db_notif =
          {
              .hdr =
                  {
                      .type = IWL_PHY_DB_CFG,
                      .length = chg_id_size,
                  },
              .data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
          },
  };
  struct iwl_rx_packet* pkt_p = reinterpret_cast<struct iwl_rx_packet*>(&pkt);

  // Expect success
  EXPECT_EQ(ZX_OK, iwl_phy_db_set_section(phy_db, pkt_p));

  struct iwl_phy_db_entry* entry = iwl_phy_db_get_section(phy_db, IWL_PHY_DB_CFG, 0);
  EXPECT_NE(nullptr, entry);
  EXPECT_EQ(chg_id_size, entry->size);
  EXPECT_NE(nullptr, entry->data);
}

// Test setting the IWL_PHY_DB_CALIB_CHG_PAPD section
TEST_F(IwlPhyDbTest, SetSectionPapd) {
  struct iwl_phy_db* phy_db = iwl_phy_db_init(nullptr);
  EXPECT_EQ(nullptr, phy_db->calib_ch_group_papd);
  EXPECT_EQ(-1, phy_db->n_group_papd);

  // A packet to set the calib db with chg_id 0x0102.
  const size_t chg_id_size = sizeof(__le16);  // chg_id
  struct {
    struct iwl_rx_packet hdr;  // must be at begin of struct.
    struct {                   // equal to the 'data' field.
      struct iwl_calib_res_notif_phy_db hdr;
      uint8_t data[chg_id_size];
    } phy_db_notif;
  } pkt = {
      .phy_db_notif =
          {
              .hdr =
                  {
                      .type = IWL_PHY_DB_CALIB_CHG_PAPD,
                      .length = chg_id_size,
                  },
              .data =
                  {
                      0x02, 0x01,  // chg_id 0x0102
                  },
          },
  };
  struct iwl_rx_packet* pkt_p = reinterpret_cast<struct iwl_rx_packet*>(&pkt);

  // Expect success
  EXPECT_EQ(ZX_OK, iwl_phy_db_set_section(phy_db, pkt_p));
  EXPECT_NE(nullptr, phy_db->calib_ch_group_papd);
  EXPECT_EQ(0x0103, phy_db->n_group_papd);
  struct iwl_phy_db_entry* entry =
      iwl_phy_db_get_section(phy_db, IWL_PHY_DB_CALIB_CHG_PAPD, 0x0102);
  EXPECT_NE(nullptr, entry);
  EXPECT_EQ(chg_id_size, entry->size);
  EXPECT_EQ(0x02, entry->data[0]);
  EXPECT_EQ(0x01, entry->data[1]);

  // Set with a larger chg_id 0x01ff. Expect it is rejected.
  pkt.phy_db_notif.data[0] = 0xff;  // data[1] is still 0x01.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, iwl_phy_db_set_section(phy_db, pkt_p));
  EXPECT_EQ(0x0103, phy_db->n_group_papd);  // Expect unchanged.
  entry = iwl_phy_db_get_section(phy_db, IWL_PHY_DB_CALIB_CHG_PAPD, 0x0102);
  EXPECT_EQ(chg_id_size, entry->size);
  EXPECT_EQ(0x02, entry->data[0]);
  EXPECT_EQ(0x01, entry->data[1]);

  // Set with a smaller chg_id 0x0101. Expect success.
  pkt.phy_db_notif.data[0] = 0x01;
  EXPECT_EQ(ZX_OK, iwl_phy_db_set_section(phy_db, pkt_p));
  EXPECT_EQ(0x0103, phy_db->n_group_papd);  // Expect unchanged.
  entry = iwl_phy_db_get_section(phy_db, IWL_PHY_DB_CALIB_CHG_PAPD, 0x0101);
  EXPECT_NE(nullptr, entry);
  EXPECT_EQ(chg_id_size, entry->size);
  EXPECT_EQ(0x01, entry->data[0]);
  EXPECT_EQ(0x01, entry->data[1]);

  iwl_phy_db_free(phy_db);
}

// Similar to SetSectionCalib above, but check different fields.
TEST_F(IwlPhyDbTest, SetSectionTxp) {
  struct iwl_phy_db* phy_db = iwl_phy_db_init(nullptr);
  EXPECT_EQ(nullptr, phy_db->calib_ch_group_txp);
  EXPECT_EQ(-1, phy_db->n_group_txp);

  // A packet to set the calib db with chg_id 0x0102.
  const size_t chg_id_size = sizeof(__le16);  // chg_id
  struct {
    struct iwl_rx_packet hdr;  // must be at begin of struct.
    struct {                   // equal to the 'data' field.
      struct iwl_calib_res_notif_phy_db hdr;
      uint8_t data[2];
    } phy_db_notif;
  } pkt = {
      .phy_db_notif = {.hdr =
                           {
                               .type = IWL_PHY_DB_CALIB_CHG_TXP,
                               .length = chg_id_size,
                           },
                       .data =
                           {
                               0x04, 0x02,  // chg_id 0x0204
                           }},
  };
  struct iwl_rx_packet* pkt_p = reinterpret_cast<struct iwl_rx_packet*>(&pkt);

  // Expect success
  EXPECT_EQ(ZX_OK, iwl_phy_db_set_section(phy_db, pkt_p));
  EXPECT_NE(nullptr, phy_db->calib_ch_group_txp);
  EXPECT_EQ(0x0205, phy_db->n_group_txp);
  struct iwl_phy_db_entry* entry = iwl_phy_db_get_section(phy_db, IWL_PHY_DB_CALIB_CHG_TXP, 0x0204);
  EXPECT_NE(nullptr, entry);
  EXPECT_EQ(chg_id_size, entry->size);
  EXPECT_EQ(0x04, entry->data[0]);
  EXPECT_EQ(0x02, entry->data[1]);

  // Set with a larger chg_id 0x02ff. Expect it is rejected.
  pkt.phy_db_notif.data[0] = 0xff;  // data[1] is still 0x01.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, iwl_phy_db_set_section(phy_db, pkt_p));
  EXPECT_EQ(0x0205, phy_db->n_group_txp);  // Expect unchanged.
  entry = iwl_phy_db_get_section(phy_db, IWL_PHY_DB_CALIB_CHG_TXP, 0x0204);
  EXPECT_EQ(chg_id_size, entry->size);
  EXPECT_EQ(0x04, entry->data[0]);
  EXPECT_EQ(0x02, entry->data[1]);

  // Set with a smaller chg_id 0x0201. Expect success.
  pkt.phy_db_notif.data[0] = 0x01;
  EXPECT_EQ(ZX_OK, iwl_phy_db_set_section(phy_db, pkt_p));
  EXPECT_EQ(0x0205, phy_db->n_group_txp);  // Expect unchanged.
  entry = iwl_phy_db_get_section(phy_db, IWL_PHY_DB_CALIB_CHG_TXP, 0x0201);
  EXPECT_NE(nullptr, entry);
  EXPECT_EQ(chg_id_size, entry->size);
  EXPECT_EQ(0x01, entry->data[0]);
  EXPECT_EQ(0x02, entry->data[1]);

  iwl_phy_db_free(phy_db);
}

}  // namespace
}  // namespace testing
}  // namespace wlan
