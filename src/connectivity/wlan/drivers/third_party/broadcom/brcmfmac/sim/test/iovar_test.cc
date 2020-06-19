// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

class IovarTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);
  IovarTest() = default;
  // This is the interface we will use for our single client interface
  SimInterface client_ifc_;

  void Init();
  void Finish();

  zx_status_t CreateInterface(wlan_info_mac_role_t role,
                              std::optional<common::MacAddr> mac_addr = std::nullopt);
  uint32_t DeviceCount();

  zx_status_t IovarGet(char* buf, uint32_t buf_len);
  zx_status_t IovarSet(char* buf, uint32_t buf_len);

 private:
  // SME callbacks
  wlanif_impl_ifc_protocol_ops_t sme_ops_ = {};
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};
};

zx_status_t IovarTest::CreateInterface(wlan_info_mac_role_t role,
                                       std::optional<common::MacAddr> mac_addr) {
  EXPECT_EQ(role, WLAN_INFO_MAC_ROLE_CLIENT);
  return SimTest::StartInterface(role, &client_ifc_, &sme_protocol_, mac_addr);
}

uint32_t IovarTest::DeviceCount() { return (dev_mgr_->DeviceCount()); }

// Create our device instance and hook up the callbacks
void IovarTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void IovarTest::Finish() {}

zx_status_t IovarTest::IovarGet(char* buf, uint32_t buf_len) {
  brcmf_simdev* sim = device_->GetSim();
  return brcmf_send_cmd_to_firmware(sim->drvr, client_ifc_.iface_id_, BRCMF_C_GET_VAR, buf, buf_len,
                                    false);
}

zx_status_t IovarTest::IovarSet(char* buf, uint32_t buf_len) {
  brcmf_simdev* sim = device_->GetSim();
  return brcmf_send_cmd_to_firmware(sim->drvr, client_ifc_.iface_id_, BRCMF_C_SET_VAR, buf, buf_len,
                                    true);
}

TEST_F(IovarTest, CheckIovarGet) {
  Init();
  ASSERT_EQ(CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT), ZX_OK);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  char buf[32];
  strcpy(buf, "mpc");
  // Get the value through the factory iovar interface
  zx_status_t status = IovarGet(buf, strlen(buf) + 1 + sizeof(uint32_t));
  EXPECT_EQ(status, ZX_OK);
  uint32_t read_val = *(reinterpret_cast<uint32_t*>(buf));
  // Get the value through the public iovar interface and compare
  brcmf_simdev* sim = device_->GetSim();
  uint32_t cur_val;
  sim->sim_fw->IovarsGet(client_ifc_.iface_id_, "mpc", &cur_val, sizeof(cur_val));
  EXPECT_EQ(cur_val, read_val);
}

TEST_F(IovarTest, CheckIovarSet) {
  Init();
  ASSERT_EQ(CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT), ZX_OK);
  EXPECT_EQ(DeviceCount(), 2u);
  brcmf_simdev* sim = device_->GetSim();
  // Get the current value of mpc through the public interface
  uint32_t cur_val;
  sim->sim_fw->IovarsGet(client_ifc_.iface_id_, "mpc", &cur_val, sizeof(cur_val));
  // Change the value and set it through the factory iovar interface
  uint32_t new_val = cur_val ? 0 : 1;
  char buf[32];
  strcpy(buf, "mpc");
  uint32_t* buf_val = reinterpret_cast<uint32_t*>(buf + strlen(buf) + 1);
  *buf_val = new_val;
  // Set the value through the factory iovar interface
  zx_status_t status = IovarSet(buf, strlen(buf) + 1 + sizeof(uint32_t));
  EXPECT_EQ(status, ZX_OK);
  // Get the value again through the public iovar interface and compare
  sim->sim_fw->IovarsGet(client_ifc_.iface_id_, "mpc", &cur_val, sizeof(cur_val));
  printf("new value of mpc is: %d\n", cur_val);
  EXPECT_EQ(cur_val, new_val);
}

TEST_F(IovarTest, CheckIovarWrongBufLen) {
  Init();
  ASSERT_EQ(CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT), ZX_OK);
  EXPECT_EQ(DeviceCount(), 2u);
  char buf[32];
  strcpy(buf, "wsec_key");
  // Get the value through the factory iovar interface with insufficient buflen.
  zx_status_t status = IovarGet(buf, strlen(buf) + 1 + sizeof(uint32_t));
  EXPECT_NE(status, ZX_OK);
  // Set the value through the factory iovar interface with insufficient buflen.
  strcpy(buf, "wsec_key");
  status = IovarSet(buf, strlen(buf) + 1 + sizeof(uint32_t));
  EXPECT_NE(status, ZX_OK);
}
}  // namespace wlan::brcmfmac
