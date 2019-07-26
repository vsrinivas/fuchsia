// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "../../ref_counted.h"
#include "../../upstream_node.h"
#include "fake_allocator.h"

namespace pci {

class FakeUpstreamNode : public UpstreamNode {
 public:
  FakeUpstreamNode(Type type, uint32_t mbus_id) : UpstreamNode(type, mbus_id) {}

  PciAllocator& pf_mmio_regions() final { return pf_mmio_alloc_; }
  PciAllocator& mmio_regions() final { return mmio_alloc_; }
  PciAllocator& pio_regions() final { return pio_alloc_; }

  void UnplugDownstream() final { UpstreamNode::UnplugDownstream(); }

  void DisableDownstream() final { UpstreamNode::DisableDownstream(); }

  PCI_IMPLEMENT_REFCOUNTED;

 private:
  FakeAllocator pf_mmio_alloc_;
  FakeAllocator mmio_alloc_;
  FakeAllocator pio_alloc_;
};

}  // namespace pci
