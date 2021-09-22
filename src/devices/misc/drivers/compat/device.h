// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVICE_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVICE_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <unordered_map>

#include <fbl/intrusive_double_list.h>

#include "src/devices/lib/driver2/logger.h"

namespace compat {

// Name of the DFv1 device.
constexpr char kName[] = "compat-name";
// Context for the DFv1 device.
constexpr char kContext[] = "compat-context";
// Ops of the DFv1 device.
constexpr char kOps[] = "compat-ops";
// Address of the parent of the DFv1 device.
constexpr char kParent[] = "compat-parent";

// Device is an implementation of a DFv1 device.
class Device : public fbl::DoublyLinkedListable<std::unique_ptr<Device>> {
 public:
  Device(const char* name, void* context, const zx_protocol_device_t* ops, driver::Logger& logger,
         async_dispatcher_t* dispatcher);

  zx_device_t* ZxDevice();

  // Links two representations of a device across separately instantiated
  // drivers.
  //
  // When a driver creates child devices that are bindable, the driver framework
  // will start a child driver that will then have its own instance of the child
  // device. This function is used to linked the two instances together, if they
  // are in the same driver host, so that operations occur on a single instance
  // of the device.
  void Link(Device* parent);
  // Binds a device to a DFv2 node.
  void Bind(fidl::ClientEnd<fuchsia_driver_framework::Node> client_end);
  // Unbinds a device from a DFv2 node.
  void Unbind();

  const char* Name() const;
  zx_status_t Add(device_add_args_t* zx_args, zx_device_t** out);
  void Remove();
  bool HasChildren() const;

  // Functions to implement the DFv1 device API.
  zx_status_t GetProtocol(uint32_t proto_id, void* out) const;
  zx_status_t AddMetadata(uint32_t type, const void* data, size_t size);
  zx_status_t GetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual);
  zx_status_t GetMetadataSize(uint32_t type, size_t* out_size);

 private:
  using Metadata = std::vector<uint8_t>;

  Device(Device&&) = delete;
  Device& operator=(Device&&) = delete;

  const std::string name_;
  void* const context_;
  const zx_protocol_device_t* const ops_;
  driver::Logger& logger_;
  async_dispatcher_t* const dispatcher_;

  // Used to link two instances of the same device together, or otherwise to
  // operate on the owned node.
  fidl::WireSharedClient<fuchsia_driver_framework::Node>* child_node_;
  fidl::WireSharedClient<fuchsia_driver_framework::Node> node_;
  fidl::WireSharedClient<fuchsia_driver_framework::NodeController> controller_;
  fbl::DoublyLinkedList<std::unique_ptr<Device>> children_;
  std::unordered_map<uint32_t, const Metadata> metadata_;
};

}  // namespace compat

struct zx_device : public compat::Device {
  // NOTE: Intentionally empty, do not add to this.
};

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVICE_H_
