// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_COMPOSITE_ASSEMBLER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_COMPOSITE_ASSEMBLER_H_

#include <fidl/fuchsia.device.composite/cpp/fidl.h>
#include <fidl/fuchsia.device.manager/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include "src/devices/bin/driver_manager/binding.h"
#include "src/devices/bin/driver_manager/v2/node.h"

namespace dfv2 {

fbl::Array<const zx_device_prop_t> NodeToProps(Node* node);

// This class represents a single fragment of a composite device. It will
// match one node.
class CompositeDeviceFragment {
 public:
  static zx::status<CompositeDeviceFragment> Create(
      fuchsia_device_manager::DeviceFragment fragment);

  // Try to bind the node against this fragment. This returns true if the node
  // matches and the fragment is currently unbound.
  bool BindNode(std::shared_ptr<Node> node);

  std::shared_ptr<Node> bound_node() { return bound_node_.lock(); }
  std::string_view name() const { return name_; }

  void Inspect(inspect::Node& root) const;

 private:
  std::string name_;
  std::vector<zx_bind_inst_t> bind_rules_;
  // This is a weak pointer because the node can be freed if its parents are
  // removed.
  std::weak_ptr<Node> bound_node_;
};

// This class will assemble a single composite device. It looks for nodes to
// match its fragments, and will create one composite node when it has all
// of its fragments matched.
class CompositeDeviceAssembler {
 public:
  // Create a CompositeDeviceAssembler. `node_manager` is unowned and must outlive
  // the assembler class.
  static zx::status<std::unique_ptr<CompositeDeviceAssembler>> Create(
      std::string name, fuchsia_device_manager::CompositeDeviceDescriptor descriptor,
      NodeManager* node_manager, async_dispatcher_t* dispatcher);

  // Check the node against each fragment of this composite device. Returns
  // true if it matches a fragment that is currently unbound.
  // If this node is the last node needed for the composite device, this function
  // will also create the composite node.
  bool BindNode(std::shared_ptr<Node> node);

  void Inspect(inspect::Node& root) const;

 private:
  // Check if we have all of our fragments bound. If we do, then create the
  // composite node. If we don't have all fragments bound, this does nothing.
  void TryToAssemble();

  std::string name_;

  async_dispatcher_t* dispatcher_;
  NodeManager* node_manager_;

  fidl::Arena<128> arena_;
  // The properties of the composite device being created. This is backed
  // by `arena_`.
  std::vector<fuchsia_driver_framework::wire::NodeProperty> properties_;

  std::vector<CompositeDeviceFragment> fragments_;
};

// This class manages all of the `CompositeDeviceAssemblers` that exist.
class CompositeDeviceManager : fidl::Server<fuchsia_device_composite::DeprecatedCompositeCreator> {
 public:
  // Create a CompositeDeviceManager. `node_manager` is unowned and must outlive the
  // manager class.
  CompositeDeviceManager(NodeManager* node_manager, async_dispatcher_t* dispatcher,
                         fit::function<void()> rebind_callback);

  zx_status_t AddCompositeDevice(std::string name,
                                 fuchsia_device_manager::CompositeDeviceDescriptor descriptor);

  // Check this node against all of the composite devices that need to be created.
  // Returns true if the node was successfully bound. If the node was bound to
  // a composite device, then there is no need to bind it to a driver.
  bool BindNode(std::shared_ptr<Node> node);

  // Publish capabilities to the outgoing directory.
  // CompositeDeviceManager must outlive |outgoing| because it will be used
  // in callbacks when other components connect to the capabilities.
  void Publish(component::OutgoingDirectory& outgoing);

  void Inspect(inspect::Node& root) const;

  // Trigger a rebind of all the nodes that are currently used in composite
  // devices. This should only be used by tests.
  void RebindNodes();

 private:
  void AddCompositeDevice(AddCompositeDeviceRequest& request,
                          AddCompositeDeviceCompleter::Sync& completer) override;

  NodeManager* node_manager_;
  async_dispatcher_t* dispatcher_;
  fit::function<void()> rebind_callback_;

  // A list of nodes that have been bound to composite devices.
  // In DFv1 a node can be bound to multiple composite devices, so we keep
  // these around for rebinding.
  std::list<std::weak_ptr<Node>> nodes_;
  std::vector<std::unique_ptr<CompositeDeviceAssembler>> assemblers_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_COMPOSITE_ASSEMBLER_H_
