// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethernet.h"

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/virtio/backends/fake.h>

#include <atomic>

#include <zxtest/zxtest.h>

namespace virtio {
namespace {

class FakeBackendForEthernetTest : public FakeBackend {
 public:
  enum Queue { RECEIVE = 0, TRANSMIT = 1, CONTROL = 2 };

  typedef FakeBackend Base;

  FakeBackendForEthernetTest()
      : FakeBackend({{Queue::RECEIVE, 128}, {Queue::TRANSMIT, 128}, {Queue::CONTROL, 128}}) {
    virtio_net_config_t config = {};
    config.status = VIRTIO_NET_S_LINK_UP;
    for (uint16_t i = 0; i < sizeof(config); ++i) {
      AddClassRegister(i, reinterpret_cast<uint8_t*>(&config)[i]);
    }
  }

  bool QueueKicked(uint16_t queue_index) { return Base::QueueKicked(queue_index); }
};

class EthernetDeviceTests : public zxtest::Test {
 public:
  void SetUp() override {
    auto backend = std::make_unique<FakeBackendForEthernetTest>();
    backend_ = backend.get();
    zx::bti bti(ZX_HANDLE_INVALID);
    fake_bti_create(bti.reset_and_get_address());
    ddk_ = std::make_unique<fake_ddk::Bind>();
    device_ = std::make_unique<EthernetDevice>(/*parent=*/fake_ddk::FakeParent(), std::move(bti),
                                               std::move(backend));
    ASSERT_OK(device_->Init());

    ops_.status = [](void* ctx, uint32_t status) {
      reinterpret_cast<EthernetDeviceTests*>(ctx)->set_status(status);
    };
    ops_.recv = [](void* ctx, const void* data_buffer, size_t data_size, uint32_t flags) {
      reinterpret_cast<EthernetDeviceTests*>(ctx)->receive(data_buffer, data_size, flags);
    };
    protocol_.ops = &ops_;
    protocol_.ctx = this;
  }

  void TearDown() override {
    device_->DdkAsyncRemove();
    device_->DdkRelease();
    EXPECT_TRUE(ddk_->Ok());
  }

  void set_status(uint32_t status) { last_status_ = status; }
  void receive(const void* data_buffer, size_t data_size, uint32_t flags) { received_++; }

  std::unique_ptr<fake_ddk::Bind> ddk_;
  std::unique_ptr<EthernetDevice> device_;
  FakeBackendForEthernetTest* backend_;
  ethernet_ifc_protocol_ops_t ops_;
  ethernet_ifc_protocol_t protocol_;

  uint32_t last_status_;
  std::atomic<int> received_;
};

TEST_F(EthernetDeviceTests, Start) {
  EXPECT_OK(device_->EthernetImplStart(&protocol_));
  EXPECT_EQ(last_status_, ETHERNET_STATUS_ONLINE);
  device_->EthernetImplStop();

  EXPECT_TRUE(backend_->QueueKicked(FakeBackendForEthernetTest::Queue::RECEIVE));
  EXPECT_FALSE(backend_->QueueKicked(FakeBackendForEthernetTest::Queue::TRANSMIT));
  EXPECT_FALSE(backend_->QueueKicked(FakeBackendForEthernetTest::Queue::CONTROL));
  EXPECT_EQ(0, received_);  // No spurious packet reception.
}

}  // anonymous namespace
}  // namespace virtio
