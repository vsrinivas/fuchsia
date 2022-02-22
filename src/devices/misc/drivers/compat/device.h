// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVICE_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVICE_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.driver.compat/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fpromise/scope.h>
#include <lib/service/llcpp/outgoing_directory.h>

#include <list>
#include <memory>
#include <unordered_map>

#include <fbl/intrusive_double_list.h>

#include "src/devices/lib/compat/symbols.h"
#include "src/devices/lib/driver2/logger.h"
#include "src/devices/misc/drivers/compat/service.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace compat {

class Driver;

// Device is an implementation of a DFv1 device.
class Device : public std::enable_shared_from_this<Device>,
               public fidl::WireServer<fuchsia_driver_compat::Device> {
 public:
  Device(std::string_view name, void* context, const compat_device_proto_ops_t& proto_ops,
         const zx_protocol_device_t* ops, Driver* driver, std::optional<Device*> parent,
         driver::Logger& logger, async_dispatcher_t* dispatcher);

  ~Device();

  zx_device_t* ZxDevice();

  // Binds a device to a DFv2 node.
  void Bind(fidl::WireSharedClient<fuchsia_driver_framework::Node> node);
  // Unbinds a device from a DFv2 node.
  void Unbind();

  const char* Name() const;
  bool HasChildren() const;

  // Functions to implement the DFv1 device API.
  zx_status_t Add(device_add_args_t* zx_args, zx_device_t** out);
  void Remove();
  zx_status_t GetProtocol(uint32_t proto_id, void* out) const;
  zx_status_t AddMetadata(uint32_t type, const void* data, size_t size);
  zx_status_t GetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual);
  zx_status_t GetMetadataSize(uint32_t type, size_t* out_size);
  zx_status_t MessageOp(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  // Set a callback to tear down any Vnodes associated with the Device.
  // This will be called in ~Device, so that the Device will always
  // outlive the Vnode.
  void SetVnodeTeardownCallback(fit::callback<void()> cb) {
    vnode_teardown_callback_ = std::move(cb);
  }

  zx_status_t StartCompatService(ServiceDir dir);

  std::string_view topological_path() const { return topological_path_; }
  void set_topological_path(std::string path) { topological_path_ = std::move(path); }
  Driver* driver() { return driver_; }

  fpromise::scope& scope() { return scope_; }
  driver::Logger& logger() { return logger_; }

 private:
  using Metadata = std::vector<uint8_t>;

  Device(Device&&) = delete;
  Device& operator=(Device&&) = delete;

  zx_status_t CreateNode();

  // fuchsia.driver.compat.Compat
  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& completer) override;
  void GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& completer) override;

  void RemoveChild(std::shared_ptr<Device>& child);

  // This arena backs `properties_`.
  // This should be declared before any objects it backs so it is destructed last.
  fidl::Arena<512> arena_;
  std::vector<fuchsia_driver_framework::wire::NodeProperty> properties_;

  std::string topological_path_;
  const std::string name_;
  void* const context_;
  const zx_protocol_device_t* const ops_;
  driver::Logger& logger_;
  async_dispatcher_t* const dispatcher_;
  uint32_t device_flags_ = 0;

  // This device's driver. The driver owns all of its Device objects, so it
  // is garaunteed to outlive the Device.
  Driver* driver_ = nullptr;

  // The default protocol of the device.
  compat_device_proto_ops_t proto_ops_ = {};

  std::optional<fit::callback<void()>> vnode_teardown_callback_;

  std::optional<ServiceDir> compat_service_;

  // The device's parent. If this field is set then the Device ptr is guaranteed
  // to be non-null. The parent is also guaranteed to outlive its child.
  //
  // This is used by a Device to free itself, by calling parent_.RemoveChild(this).
  //
  // parent_ will be std::nullopt when the Device is the fake device created
  // by the Driver class in the DFv1 shim. When parent_ is std::nullopt, the
  // Device will be freed when the Driver is freed.
  std::optional<Device*> parent_;

  fidl::WireSharedClient<fuchsia_driver_framework::Node> node_;
  fidl::WireSharedClient<fuchsia_driver_framework::NodeController> controller_;
  std::unordered_map<uint32_t, const Metadata> metadata_;

  // The Device's children. The Device has full ownership of the children,
  // but these are shared pointers so that the NodeController can get a weak
  // pointer to the child in order to erase them.
  std::list<std::shared_ptr<Device>> children_;

  async::Executor executor_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace compat

struct zx_device : public compat::Device {
  // NOTE: Intentionally empty, do not add to this.
};

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVICE_H_
