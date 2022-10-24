// Copyright (c) 2022 The Fuchsia Authors
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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/key_ring.h"

#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"

namespace {

using wlan::nxpfmac::KeyRing;

constexpr uint32_t kBssIndex = 4;

struct KeyRingTest : public zxtest::Test {
  void SetUp() override {
    zx::result<std::unique_ptr<wlan::nxpfmac::IoctlAdapter>> adapter =
        wlan::nxpfmac::IoctlAdapter::Create(mlan_mock_.GetAdapter(), &mock_bus_);
    ASSERT_OK(adapter.status_value());
    ioctl_adapter_ = std::move(adapter.value());
    key_ring_ = std::make_unique<KeyRing>(ioctl_adapter_.get(), kBssIndex);
  }

  void TearDown() override {
    // Set a new ioctl callback for the destruction of keyring. It's going to remove all keys as
    // part of its destruction. Since this will almost certainly happen for every test do it here in
    // the teardown.
    mlan_mock_.SetOnMlanIoctl([&](void*, pmlan_ioctl_req req) -> mlan_status {
      EXPECT_EQ(MLAN_ACT_SET, req->action);
      EXPECT_EQ(MLAN_IOCTL_SEC_CFG, req->req_id);
      EXPECT_EQ(kBssIndex, req->bss_index);
      auto sec_cfg = reinterpret_cast<mlan_ds_sec_cfg*>(req->pbuf);
      EXPECT_EQ(MLAN_OID_SEC_CFG_ENCRYPT_KEY, sec_cfg->sub_command);
      auto& encrypt_key = sec_cfg->param.encrypt_key;
      // key_disable = true and KEY_FLAG_REMOVE_KEY indicates the removal of all keys
      EXPECT_TRUE(encrypt_key.key_disable);
      EXPECT_EQ(KEY_FLAG_REMOVE_KEY, encrypt_key.key_flags);
      ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
      return MLAN_STATUS_PENDING;
    });
  }

  wlan::nxpfmac::MlanMockAdapter mlan_mock_;
  wlan::nxpfmac::MockBus mock_bus_;
  std::unique_ptr<wlan::nxpfmac::IoctlAdapter> ioctl_adapter_;
  std::unique_ptr<KeyRing> key_ring_;
};

TEST_F(KeyRingTest, AddPrivateKey) {
  // Test adding a private key

  constexpr uint8_t kKeyId = 1;
  constexpr uint8_t kKeyData[]{0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
                               0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0};
  constexpr uint64_t kRsc = 0x23847387efa;

  const set_key_descriptor_t key{.key_list = kKeyData,
                                 .key_count = sizeof(kKeyData),
                                 .key_id = kKeyId,
                                 .address{0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                                 .rsc = kRsc,
                                 .cipher_suite_type = CIPHER_SUITE_TYPE_CCMP_128};

  sync_completion_t completion;
  mlan_mock_.SetOnMlanIoctl([&](void*, pmlan_ioctl_req req) -> mlan_status {
    EXPECT_EQ(MLAN_ACT_SET, req->action);
    EXPECT_EQ(MLAN_IOCTL_SEC_CFG, req->req_id);
    EXPECT_EQ(kBssIndex, req->bss_index);
    auto& encrypt_key = reinterpret_cast<mlan_ds_sec_cfg*>(req->pbuf)->param.encrypt_key;
    EXPECT_EQ(kKeyId, encrypt_key.key_index);
    EXPECT_EQ(sizeof(kKeyData), encrypt_key.key_len);
    EXPECT_BYTES_EQ(kKeyData, encrypt_key.key_material, sizeof(kKeyData));
    EXPECT_BYTES_EQ(key.address, encrypt_key.mac_addr, sizeof(key.address));
    // It's a CCMP 128 so no specific key flags needed, we provided an RSC so RX_SEQ_VALID should be
    // set. It's not a group key (because the address is not the broadcast address) so the group key
    // flag should not be set. Currently all keys are tx keys (rx is always implied).
    EXPECT_EQ(KEY_FLAG_SET_TX_KEY | KEY_FLAG_RX_SEQ_VALID, encrypt_key.key_flags);
    sync_completion_signal(&completion);
    return MLAN_STATUS_SUCCESS;
  });

  ASSERT_OK(key_ring_->AddKey(key));

  ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
}

TEST_F(KeyRingTest, AddGroupKey) {
  // Test adding a group key

  constexpr uint8_t kKeyId = 7;
  constexpr uint8_t kKeyData[]{0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
                               0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0};
  constexpr uint64_t kRsc = 0x23847387efa;

  const set_key_descriptor_t key{.key_list = kKeyData,
                                 .key_count = sizeof(kKeyData),
                                 .key_id = kKeyId,
                                 .address{0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
                                 .rsc = kRsc,
                                 .cipher_suite_type = CIPHER_SUITE_TYPE_CCMP_128};

  mlan_mock_.SetOnMlanIoctl([&](void*, pmlan_ioctl_req req) -> mlan_status {
    EXPECT_EQ(MLAN_ACT_SET, req->action);
    EXPECT_EQ(MLAN_IOCTL_SEC_CFG, req->req_id);
    EXPECT_EQ(kBssIndex, req->bss_index);
    auto sec_cfg = reinterpret_cast<mlan_ds_sec_cfg*>(req->pbuf);
    EXPECT_EQ(MLAN_OID_SEC_CFG_ENCRYPT_KEY, sec_cfg->sub_command);
    auto& encrypt_key = sec_cfg->param.encrypt_key;
    EXPECT_EQ(kKeyId, encrypt_key.key_index);
    EXPECT_EQ(sizeof(kKeyData), encrypt_key.key_len);
    EXPECT_BYTES_EQ(kKeyData, encrypt_key.key_material, sizeof(kKeyData));
    EXPECT_BYTES_EQ(key.address, encrypt_key.mac_addr, sizeof(key.address));
    // Verify that the group key flag is set
    EXPECT_EQ(KEY_FLAG_SET_TX_KEY | KEY_FLAG_GROUP_KEY | KEY_FLAG_RX_SEQ_VALID,
              encrypt_key.key_flags);
    ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
    return MLAN_STATUS_PENDING;
  });

  ASSERT_OK(key_ring_->AddKey(key));
}

TEST_F(KeyRingTest, RemoveKey) {
  constexpr uint8_t kKeyId = 15;
  constexpr uint8_t kMacAddr[] = {0x13, 0x45, 0x56, 0x43, 0x98, 0xa4};

  mlan_mock_.SetOnMlanIoctl([&](void*, pmlan_ioctl_req req) -> mlan_status {
    EXPECT_EQ(MLAN_ACT_SET, req->action);
    EXPECT_EQ(MLAN_IOCTL_SEC_CFG, req->req_id);
    EXPECT_EQ(kBssIndex, req->bss_index);
    auto sec_cfg = reinterpret_cast<mlan_ds_sec_cfg*>(req->pbuf);
    EXPECT_EQ(MLAN_OID_SEC_CFG_ENCRYPT_KEY, sec_cfg->sub_command);
    auto& encrypt_key = sec_cfg->param.encrypt_key;
    EXPECT_EQ(kKeyId, encrypt_key.key_index);
    // Removal is indicated by key_remove being true and flags being set to KEY_FLAG_REMOVE_KEY
    EXPECT_TRUE(encrypt_key.key_remove);
    EXPECT_EQ(KEY_FLAG_REMOVE_KEY, encrypt_key.key_flags);
    ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
    return MLAN_STATUS_PENDING;
  });

  ASSERT_OK(key_ring_->RemoveKey(kKeyId, kMacAddr));
}

TEST_F(KeyRingTest, RemoveAllKeys) {
  mlan_mock_.SetOnMlanIoctl([&](void*, pmlan_ioctl_req req) -> mlan_status {
    EXPECT_EQ(MLAN_ACT_SET, req->action);
    EXPECT_EQ(MLAN_IOCTL_SEC_CFG, req->req_id);
    EXPECT_EQ(kBssIndex, req->bss_index);
    auto sec_cfg = reinterpret_cast<mlan_ds_sec_cfg*>(req->pbuf);
    EXPECT_EQ(MLAN_OID_SEC_CFG_ENCRYPT_KEY, sec_cfg->sub_command);
    auto& encrypt_key = sec_cfg->param.encrypt_key;
    // key_disable = true and KEY_FLAG_REMOVE_KEY indicates the removal of all keys
    EXPECT_TRUE(encrypt_key.key_disable);
    EXPECT_EQ(KEY_FLAG_REMOVE_KEY, encrypt_key.key_flags);
    ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
    return MLAN_STATUS_PENDING;
  });

  ASSERT_OK(key_ring_->RemoveAllKeys());
}

TEST_F(KeyRingTest, EnableWepKey) {
  // Test that the EnableWepKey call correctly enables the given WEP key.

  constexpr uint8_t kKeyId = 12;

  mlan_mock_.SetOnMlanIoctl([&](void*, pmlan_ioctl_req req) -> mlan_status {
    EXPECT_EQ(MLAN_ACT_SET, req->action);
    EXPECT_EQ(MLAN_IOCTL_SEC_CFG, req->req_id);
    EXPECT_EQ(kBssIndex, req->bss_index);
    auto sec_cfg = reinterpret_cast<mlan_ds_sec_cfg*>(req->pbuf);
    EXPECT_EQ(MLAN_OID_SEC_CFG_ENCRYPT_KEY, sec_cfg->sub_command);
    auto& encrypt_key = sec_cfg->param.encrypt_key;
    EXPECT_TRUE(encrypt_key.is_current_wep_key);
    EXPECT_EQ(kKeyId, encrypt_key.key_index);
    ioctl_adapter_->OnIoctlComplete(req, wlan::nxpfmac::IoctlStatus::Success);
    return MLAN_STATUS_PENDING;
  });

  ASSERT_OK(key_ring_->EnableWepKey(kKeyId));
}

}  // namespace
