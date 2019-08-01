// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

namespace {

// These tests are testing interfaces that get included via multiple inheritance, and thus we must
// make sure we get all the casts correct. We record the value of the "this" pointer in the
// constructor, and then verify in each call the "this" pointer was the same as the original. (The
// typical way for this to go wrong is to take a EthernetIfc<D>* instead of a D* in a function
// signature.)
#define get_this() reinterpret_cast<uintptr_t>(this)

class TestEthernetIfc : public ddk::Device<TestEthernetIfc>,
                        public ddk::EthernetIfcProtocol<TestEthernetIfc> {
 public:
  TestEthernetIfc() : ddk::Device<TestEthernetIfc>(nullptr) { this_ = get_this(); }

  void DdkRelease() {}

  void EthernetIfcStatus(uint32_t status) {
    status_this_ = get_this();
    status_called_ = true;
  }

  void EthernetIfcRecv(const void* data, size_t length, uint32_t flags) {
    recv_this_ = get_this();
    recv_called_ = true;
  }

  bool VerifyCalls() const {
    BEGIN_HELPER;
    EXPECT_EQ(this_, status_this_, "");
    EXPECT_EQ(this_, recv_this_, "");
    EXPECT_TRUE(status_called_, "");
    EXPECT_TRUE(recv_called_, "");
    END_HELPER;
  }

  ethernet_ifc_protocol_t ethernet_ifc() { return {&ethernet_ifc_protocol_ops_, this}; }

  zx_status_t StartProtocol(ddk::EthernetImplProtocolClient* client) {
    return client->Start(this, &ethernet_ifc_protocol_ops_);
  }

 private:
  uintptr_t this_ = 0u;
  uintptr_t status_this_ = 0u;
  uintptr_t recv_this_ = 0u;
  bool status_called_ = false;
  bool recv_called_ = false;
};

class TestEthernetImplProtocol
    : public ddk::Device<TestEthernetImplProtocol, ddk::GetProtocolable>,
      public ddk::EthernetImplProtocol<TestEthernetImplProtocol, ddk::base_protocol> {
 public:
  TestEthernetImplProtocol()
      : ddk::Device<TestEthernetImplProtocol, ddk::GetProtocolable>(nullptr) {
    this_ = get_this();
  }

  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out) {
    if (proto_id != ZX_PROTOCOL_ETHERNET_IMPL)
      return ZX_ERR_INVALID_ARGS;
    ddk::AnyProtocol* proto = static_cast<ddk::AnyProtocol*>(out);
    proto->ops = &ethernet_impl_protocol_ops_;
    proto->ctx = this;
    return ZX_OK;
  }

  void DdkRelease() {}

  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
    query_this_ = get_this();
    query_called_ = true;
    return ZX_OK;
  }

  void EthernetImplStop() {
    stop_this_ = get_this();
    stop_called_ = true;
  }

  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
    start_this_ = get_this();
    client_ = std::make_unique<ddk::EthernetIfcProtocolClient>(ifc);
    start_called_ = true;
    return ZX_OK;
  }

  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
    queue_tx_this_ = get_this();
    queue_tx_called_ = true;
  }

  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                   size_t data_size) {
    set_param_this_ = get_this();
    set_param_called_ = true;
    return ZX_OK;
  }
  void EthernetImplGetBti(zx::bti* bti) { bti->reset(); }

  bool VerifyCalls() const {
    BEGIN_HELPER;
    EXPECT_EQ(this_, query_this_, "");
    EXPECT_EQ(this_, start_this_, "");
    EXPECT_EQ(this_, stop_this_, "");
    EXPECT_EQ(this_, queue_tx_this_, "");
    EXPECT_EQ(this_, set_param_this_, "");
    EXPECT_TRUE(query_called_, "");
    EXPECT_TRUE(start_called_, "");
    EXPECT_TRUE(stop_called_, "");
    EXPECT_TRUE(queue_tx_called_, "");
    EXPECT_TRUE(set_param_called_, "");
    END_HELPER;
  }

  bool TestIfc() {
    if (!client_)
      return false;
    // Use the provided client to test the ifc client.
    client_->Status(0);
    client_->Recv(nullptr, 0, 0);
    return true;
  }

 private:
  uintptr_t this_ = 0u;
  uintptr_t query_this_ = 0u;
  uintptr_t stop_this_ = 0u;
  uintptr_t start_this_ = 0u;
  uintptr_t queue_tx_this_ = 0u;
  uintptr_t set_param_this_ = 0u;
  bool query_called_ = false;
  bool stop_called_ = false;
  bool start_called_ = false;
  bool queue_tx_called_ = false;
  bool set_param_called_ = false;

  fbl::unique_ptr<ddk::EthernetIfcProtocolClient> client_;
};

static bool test_ethernet_ifc() {
  BEGIN_TEST;

  TestEthernetIfc dev;

  auto ifc = dev.ethernet_ifc();
  ethernet_ifc_status(&ifc, 0);
  ethernet_ifc_recv(&ifc, nullptr, 0, 0);

  EXPECT_TRUE(dev.VerifyCalls(), "");

  END_TEST;
}

static bool test_ethernet_ifc_client() {
  BEGIN_TEST;

  TestEthernetIfc dev;
  const ethernet_ifc_protocol_t ifc = dev.ethernet_ifc();
  ddk::EthernetIfcProtocolClient client(&ifc);

  client.Status(0);
  client.Recv(nullptr, 0, 0);

  EXPECT_TRUE(dev.VerifyCalls(), "");

  END_TEST;
}

static bool test_ethernet_impl_protocol() {
  BEGIN_TEST;

  TestEthernetImplProtocol dev;

  // Normally we would use device_op_get_protocol, but we haven't added the device to devmgr so
  // its ops table is currently invalid.
  ethernet_impl_protocol_t proto;
  auto status = dev.DdkGetProtocol(0, reinterpret_cast<void*>(&proto));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "");

  status = dev.DdkGetProtocol(ZX_PROTOCOL_ETHERNET_IMPL, reinterpret_cast<void*>(&proto));
  EXPECT_EQ(ZX_OK, status, "");

  EXPECT_EQ(ZX_OK, ethernet_impl_query(&proto, 0, nullptr), "");
  proto.ops->stop(proto.ctx);
  ethernet_ifc_protocol_t ifc = {nullptr, nullptr};
  EXPECT_EQ(ZX_OK, ethernet_impl_start(&proto, ifc.ctx, ifc.ops), "");
  ethernet_netbuf_t netbuf = {};
  ethernet_impl_queue_tx(&proto, 0, &netbuf, nullptr, nullptr);
  EXPECT_EQ(ZX_OK, ethernet_impl_set_param(&proto, 0, 0, nullptr, 0), "");

  EXPECT_TRUE(dev.VerifyCalls(), "");

  END_TEST;
}

static bool test_ethernet_impl_protocol_client() {
  BEGIN_TEST;

  // The EthernetImplProtocol device to wrap. This would live in the parent device
  // our driver was binding to.
  TestEthernetImplProtocol protocol_dev;

  ethernet_impl_protocol_t proto;
  auto status = protocol_dev.DdkGetProtocol(ZX_PROTOCOL_ETHERNET_IMPL,
                                            reinterpret_cast<void*>(&proto));
  EXPECT_EQ(ZX_OK, status, "");
  // The client device to wrap the ops + device that represent the parent
  // device.
  ddk::EthernetImplProtocolClient client(&proto);
  // The EthernetIfc to hand to the parent device.
  TestEthernetIfc ifc_dev;
  ethernet_ifc_protocol_t ifc = ifc_dev.ethernet_ifc();

  EXPECT_EQ(ZX_OK, client.Query(0, nullptr), "");
  client.Stop();
  EXPECT_EQ(ZX_OK, client.Start(ifc.ctx, ifc.ops), "");
  ethernet_netbuf_t netbuf = {};
  client.QueueTx(0, &netbuf, nullptr, nullptr);
  EXPECT_EQ(ZX_OK, client.SetParam(0, 0, nullptr, 0));

  EXPECT_TRUE(protocol_dev.VerifyCalls(), "");

  END_TEST;
}

static bool test_ethernet_impl_protocol_ifc_client() {
  BEGIN_TEST;

  // We create a protocol device that we will start from an ifc device. The protocol device will
  // then use the pointer passed to it to call methods on the ifc device. This ensures the void*
  // casting is correct.
  TestEthernetImplProtocol protocol_dev;

  ethernet_impl_protocol_t proto;
  auto status =
      protocol_dev.DdkGetProtocol(ZX_PROTOCOL_ETHERNET_IMPL, reinterpret_cast<void*>(&proto));
  EXPECT_EQ(ZX_OK, status, "");

  ddk::EthernetImplProtocolClient client(&proto);
  TestEthernetIfc ifc_dev;
  EXPECT_EQ(ZX_OK, ifc_dev.StartProtocol(&client), "");

  // Execute the EthernetIfc methods
  ASSERT_TRUE(protocol_dev.TestIfc(), "");
  // Verify that they were called
  EXPECT_TRUE(ifc_dev.VerifyCalls(), "");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(ddktl_ethernet_device)
RUN_NAMED_TEST("ddk::EthernetIfcProtocol", test_ethernet_ifc);
RUN_NAMED_TEST("ddk::EthernetIfcProtocolClient", test_ethernet_ifc_client);
RUN_NAMED_TEST("ddk::EthernetImplProtocol", test_ethernet_impl_protocol);
RUN_NAMED_TEST("ddk::EthernetImplProtocolClient", test_ethernet_impl_protocol_client);
RUN_NAMED_TEST("EthernetImplProtocol using EthernetIfcClient",
               test_ethernet_impl_protocol_ifc_client);
END_TEST_CASE(ddktl_ethernet_device)

test_case_element* test_case_ddktl_ethernet_device = TEST_CASE_ELEMENT(ddktl_ethernet_device);
