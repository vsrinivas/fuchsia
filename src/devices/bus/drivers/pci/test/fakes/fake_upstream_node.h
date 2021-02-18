// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_UPSTREAM_NODE_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_UPSTREAM_NODE_H_

#include "src/devices/bus/drivers/pci/ref_counted.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_allocator.h"
#include "src/devices/bus/drivers/pci/upstream_node.h"

namespace pci {

class FakeUpstreamNode : public UpstreamNode {
 public:
  FakeUpstreamNode(Type type, uint32_t mbus_id) : UpstreamNode(type, mbus_id) {}

  PciAllocator& pf_mmio_regions() final { return pf_mmio_alloc_; }
  PciAllocator& mmio_regions() final { return mmio_alloc_; }
  PciAllocator& pio_regions() final { return pio_alloc_; }

  void UnplugDownstream() final { UpstreamNode::UnplugDownstream(); }
  void DisableDownstream() final { UpstreamNode::DisableDownstream(); }
  zx_status_t EnableBusMasterUpstream(bool) { return ZX_OK; }

  PCI_IMPLEMENT_REFCOUNTED;

 private:
  FakeAllocator pf_mmio_alloc_;
  FakeAllocator mmio_alloc_;
  FakeAllocator pio_alloc_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_UPSTREAM_NODE_H_
