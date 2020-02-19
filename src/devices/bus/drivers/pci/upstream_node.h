// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_UPSTREAM_NODE_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_UPSTREAM_NODE_H_

#include <sys/types.h>
#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>

#include "allocation.h"
#include "device.h"
#include "ref_counted.h"

// UpstreamNode
//
// A class responsible for maintaining the state of a node in the graph of
// PCI/PCIe devices which can have downstream children.  UpstreamNodes are
// not instantiated directly, instead they serve as the base class of
// PCI/PCIe bridges and roots.

namespace pci {

class FakeBusDriver;
class PciAllocator;
class UpstreamNode {
 public:
  enum class Type { ROOT, BRIDGE };
  // UpstreamNode must have refcounting implemented by its derived classes Root or Bridge
  PCI_REQUIRE_REFCOUNTED;

  // Disallow copying, assigning and moving.
  UpstreamNode(const UpstreamNode&) = delete;
  UpstreamNode(UpstreamNode&&) = delete;
  UpstreamNode& operator=(const UpstreamNode&) = delete;
  UpstreamNode& operator=(UpstreamNode&&) = delete;

  Type type() const { return type_; }
  uint32_t managed_bus_id() const { return managed_bus_id_; }

  virtual PciAllocator& pf_mmio_regions() = 0;
  virtual PciAllocator& mmio_regions() = 0;
  virtual PciAllocator& pio_regions() = 0;

  void LinkDevice(pci::Device* device) { downstream_.push_back(device); }
  void UnlinkDevice(pci::Device* device) { downstream_.erase(*device); }

 protected:
  friend FakeBusDriver;
  UpstreamNode(Type type, uint32_t mbus_id) : type_(type), managed_bus_id_(mbus_id) {}
  virtual ~UpstreamNode() = default;

  // Configure / late-initialization any devices downstream of this node.
  virtual void ConfigureDownstreamDevices();
  // Disable all devices directly connected to this bridge.
  virtual void DisableDownstream();
  // Unplug all devices directly connected to this bridge.
  virtual void UnplugDownstream();
  // The list of all devices immediately under this root/bridge.
  fbl::DoublyLinkedList<pci::Device*> downstream_;

 private:
  const Type type_;
  const uint32_t managed_bus_id_;  // The ID of the downstream bus which this node manages.
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_UPSTREAM_NODE_H_
