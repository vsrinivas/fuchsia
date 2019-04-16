// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "../config.h"
#include "../device.h"
#include "fake_bus.h"
#include "fake_pciroot.h"
#include "fake_upstream_node.h"
#include <ddktl/protocol/pciroot.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <zircon/limits.h>
#include <zxtest/zxtest.h>

namespace pci {

class PciDeviceTests : public zxtest::Test {
public:
    FakePciroot& pciroot_proto() { return *pciroot_; }
    ddk::PcirootProtocolClient& pciroot_client() { return *client_; }
    FakeBus& bus() { return bus_; }
    FakeUpstreamNode& upstream() { return upstream_; }

protected:
    PciDeviceTests()
        : upstream_(UpstreamNode::Type::ROOT, 0) {}
    void SetUp() {
        ASSERT_EQ(ZX_OK, FakePciroot::Create(0, 1, &pciroot_));
        client_ = std::make_unique<ddk::PcirootProtocolClient>(pciroot_->proto());
    }
    void TearDown() {
        upstream_.DisableDownstream();
        upstream_.UnplugDownstream();
    }

private:
    std::unique_ptr<FakePciroot> pciroot_;
    std::unique_ptr<ddk::PcirootProtocolClient> client_;
    FakeBus bus_;
    FakeUpstreamNode upstream_;
};

TEST_F(PciDeviceTests, CreationTest) {
    pci_bdf_t bdf = {1, 2, 3};
    fbl::RefPtr<Config> cfg;

    // This test creates a device, goes through its init sequence, links it into
    // the toplogy, and then has it linger. It will be cleaned up by TearDown()
    // releasing all objects of upstream(). If creation succeeds here and no
    // asserts happen following the test it means the fakes are built properly
    // enough and the basic interface is fulfilled.
    ASSERT_OK(MmioConfig::Create(bdf, &pciroot_proto().ecam().get_mmio(), 0, 1, &cfg));
    ASSERT_OK(Device::Create(fake_ddk::kFakeParent, std::move(cfg), &upstream(), &bus()));

    // Verify the created device's BDF.
    auto& dev = bus().get_device(bdf);
    ASSERT_EQ(bdf.bus_id, dev.bus_id());
    ASSERT_EQ(bdf.device_id, dev.dev_id());
    ASSERT_EQ(bdf.function_id, dev.func_id());
}

} // namespace pci
