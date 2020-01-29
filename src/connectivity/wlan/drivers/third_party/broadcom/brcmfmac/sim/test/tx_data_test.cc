// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/proto.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

#define FW_BUF(sim) sim->sim_fw->last_pkt_buf_

namespace wlan::brcmfmac {

class TxDataTest : public SimTest {
 public:
  TxDataTest() = default;
  void Init();
  void Destroy();

  // Test functions
  void VerifyDataOnFirmware(ethernet_netbuf_t* ethernet_netbuf);
  void StartTxPacket(ethernet_netbuf_t* ethernet_netbuf);

  // The particular odd data len comes from an actual failure.
  static constexpr uint32_t kLenNeedAligned = 113;

 private:
  // This is the interface we will use for our single client interface
  std::unique_ptr<SimInterface> client_ifc_;
  // SME callbacks
  wlanif_impl_ifc_protocol sme_protocol_ = {.ctx = this};
};

void TxDataTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT, sme_protocol_, &client_ifc_), ZX_OK);
}

void TxDataTest::VerifyDataOnFirmware(ethernet_netbuf_t* ethernet_netbuf) {
  brcmf_simdev* sim = device_->GetSim();
  const bool is_buf_size_aligned = (FW_BUF(sim).allocated_size_of_buf_in % 4) == 0;
  EXPECT_EQ(is_buf_size_aligned, true);
  EXPECT_EQ(ethernet_netbuf->data_size, FW_BUF(sim).len);
  EXPECT_EQ(memcmp(ethernet_netbuf->data_buffer, FW_BUF(sim).data.get(), FW_BUF(sim).len), 0);
}

void TxDataTest::Destroy() {
  zx_status_t status = device_->WlanphyImplDestroyIface(client_ifc_->iface_id_);
  EXPECT_EQ(status, ZX_OK);
}

void TxDataTest::StartTxPacket(ethernet_netbuf_t* netbuf) {
  // No callback and cookie needed
  client_ifc_->if_impl_ops_->data_queue_tx(
      client_ifc_->if_impl_ctx_, 0, netbuf,
      [](void* ctx, zx_status_t status, ethernet_netbuf_t* netbuf) {}, nullptr);
}

// This test verify the size of data of ethernet packet(including ethernet header) will not change
// when transmitted to firmware.
TEST_F(TxDataTest, DataPacketSize) {
  Init();
  // Create data packet to transmit.
  uint8_t* packet = static_cast<decltype(packet)>(malloc(kLenNeedAligned));
  memset(packet, '.', kLenNeedAligned);
  ethernet_netbuf_t netbuf = {
      .data_buffer = packet,
      .data_size = kLenNeedAligned,
  };

  StartTxPacket(&netbuf);
  VerifyDataOnFirmware(&netbuf);

  Destroy();
  free(packet);
}

}  // namespace wlan::brcmfmac
