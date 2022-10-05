// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/composite_assembler.h"

#include <lib/driver2/node_add_args.h>

#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace dfv2 {

namespace fdf {
using namespace fuchsia_driver_framework;
}

fbl::Array<const zx_device_prop_t> NodeToProps(Node* node) {
  std::vector<zx_device_prop_t> props;
  for (auto& prop : node->properties()) {
    if (prop.key().is_int_value() && prop.value().is_int_value()) {
      zx_device_prop_t device_prop;
      device_prop.id = static_cast<uint16_t>(prop.key().int_value());
      device_prop.value = prop.value().int_value();
      props.push_back(device_prop);
    }
  }
  fbl::Array<zx_device_prop_t> props_array(new zx_device_prop_t[props.size()], props.size());
  for (size_t i = 0; i < props.size(); i++) {
    props_array[i] = props[i];
  }
  return props_array;
}

zx::status<CompositeDeviceFragment> CompositeDeviceFragment::Create(
    fuchsia_device_manager::DeviceFragment fragment) {
  auto composite_fragment = CompositeDeviceFragment();

  if (fragment.parts().size() != 1) {
    LOGF(ERROR, "Composite fragments with multiple parts are deprecated. %s has %zd parts.",
         fragment.name().c_str(), fragment.parts().size());
    return zx::error(ZX_ERR_INTERNAL);
  }

  auto& program = fragment.parts()[0].match_program();
  std::vector<zx_bind_inst_t> rules(program.size());
  for (size_t i = 0; i < program.size(); i++) {
    rules[i] = zx_bind_inst_t{
        .op = program[i].op(),
        .arg = program[i].arg(),
    };
  }
  composite_fragment.name_ = fragment.name();
  composite_fragment.bind_rules_ = std::move(rules);

  return zx::ok(std::move(composite_fragment));
}

bool CompositeDeviceFragment::BindNode(std::shared_ptr<Node> node) {
  // If we have a bound node, then don't match.
  if (bound_node_.lock()) {
    return false;
  }

  internal::BindProgramContext context = {};
  context.binding = bind_rules_.data();
  context.binding_size = bind_rules_.size() * sizeof(zx_bind_inst_t);

  auto props = NodeToProps(node.get());
  context.props = &props;

  if (!EvaluateBindProgram(&context)) {
    return false;
  }

  // We matched! Store our node.
  bound_node_ = node;
  return true;
}

void CompositeDeviceFragment::Inspect(inspect::Node& root) const {
  std::string moniker = "<unbound>";
  if (auto node = bound_node_.lock()) {
    // TODO(fxbug.dev/107288): Change this back to node->TopoPath() when inspect is fixed.
    moniker = "bound";
  }

  root.RecordString(name_, moniker);
}

zx::status<std::unique_ptr<CompositeDeviceAssembler>> CompositeDeviceAssembler::Create(
    std::string name, fuchsia_device_manager::CompositeDeviceDescriptor descriptor,
    NodeManager* node_manager, async_dispatcher_t* dispatcher) {
  auto assembler = std::make_unique<CompositeDeviceAssembler>();
  assembler->name_ = std::move(name);
  assembler->node_manager_ = node_manager;
  assembler->dispatcher_ = dispatcher;

  if (descriptor.primary_fragment_index() >= descriptor.fragments().size()) {
    LOGF(ERROR,
         "Composite fragments with bad primary_fragment_index. primary is %ul but composite has "
         "%zd parts.",
         descriptor.primary_fragment_index(), descriptor.fragments().size());
    return zx::error(ZX_ERR_INTERNAL);
  }

  // Create the properties.
  for (auto& prop : descriptor.props()) {
    assembler->properties_.emplace_back(
        driver::MakeProperty(assembler->arena_, prop.id(), prop.value()));
  }

  // Create the string properties.
  for (auto& prop : descriptor.str_props()) {
    switch (prop.value().Which()) {
      case fuchsia_device_manager::PropertyValue::Tag::kBoolValue:
        assembler->properties_.emplace_back(
            driver::MakeProperty(assembler->arena_, prop.key(), prop.value().bool_value().value()));
        break;

      case fuchsia_device_manager::PropertyValue::Tag::kIntValue:
        assembler->properties_.emplace_back(
            driver::MakeProperty(assembler->arena_, prop.key(), prop.value().int_value().value()));
        break;

      case fuchsia_device_manager::PropertyValue::Tag::kStrValue:
        assembler->properties_.emplace_back(
            driver::MakeProperty(assembler->arena_, prop.key(), prop.value().str_value().value()));
        break;

      case fuchsia_device_manager::PropertyValue::Tag::kEnumValue:
        assembler->properties_.emplace_back(driver::MakeEnumProperty(
            assembler->arena_, prop.key(), prop.value().enum_value().value()));
        break;
    }
  }

  // Add the composite value.
  assembler->properties_.emplace_back(driver::MakeProperty(assembler->arena_, BIND_COMPOSITE, 1));

  // Make the primary fragment first.
  auto fragment =
      CompositeDeviceFragment::Create(descriptor.fragments()[descriptor.primary_fragment_index()]);
  if (fragment.is_error()) {
    return fragment.take_error();
  }
  assembler->fragments_.push_back(std::move(fragment.value()));

  // Make the other fragments.
  for (size_t i = 0; i < descriptor.fragments().size(); i++) {
    if (i == descriptor.primary_fragment_index()) {
      continue;
    }
    auto fragment = CompositeDeviceFragment::Create(descriptor.fragments()[i]);
    if (fragment.is_error()) {
      return fragment.take_error();
    }
    assembler->fragments_.push_back(std::move(fragment.value()));
  }

  return zx::ok(std::move(assembler));
}

bool CompositeDeviceAssembler::BindNode(std::shared_ptr<Node> node) {
  bool matched = false;
  for (auto& fragment : fragments_) {
    if (fragment.BindNode(node)) {
      matched = true;
      LOGF(INFO, "Found a match for composite device '%s': fragment %s: device '%s'", name_.c_str(),
           std::string(fragment.name()).c_str(), node->TopoName().c_str());
      break;
    }
  }

  if (!matched) {
    return false;
  }

  TryToAssemble();
  return true;
}

void CompositeDeviceAssembler::TryToAssemble() {
  std::vector<std::shared_ptr<Node>> strong_parents;
  std::vector<Node*> parents;
  std::vector<std::string> parents_names;
  for (auto& fragment : fragments_) {
    auto node = fragment.bound_node();
    // A fragment is missing a node, don't assemble.
    if (!node) {
      return;
    }
    parents.push_back(node.get());
    parents_names.emplace_back(fragment.name());
    strong_parents.push_back(std::move(node));
  }

  auto node = Node::CreateCompositeNode(name_, std::move(parents), parents_names,
                                        std::move(properties_), node_manager_, dispatcher_);
  if (node.is_error()) {
    return;
  }

  LOGF(INFO, "Built composite device at '%s'", node->TopoName().c_str());

  // Bind the node we just created.
  node_manager_->Bind(*node.value(), nullptr);
}

void CompositeDeviceAssembler::Inspect(inspect::Node& root) const {
  auto node = root.CreateChild(root.UniqueName("assembler-"));
  node.RecordString("name", name_);

  for (auto& fragment : fragments_) {
    fragment.Inspect(node);
  }

  root.Record(std::move(node));
}

CompositeDeviceManager::CompositeDeviceManager(NodeManager* node_manager,
                                               async_dispatcher_t* dispatcher,
                                               fit::function<void()> rebind_callback)
    : node_manager_(node_manager),
      dispatcher_(dispatcher),
      rebind_callback_(std::move(rebind_callback)) {}

zx_status_t CompositeDeviceManager::AddCompositeDevice(
    std::string name, fuchsia_device_manager::CompositeDeviceDescriptor descriptor) {
  auto assembler = CompositeDeviceAssembler::Create(std::move(name), std::move(descriptor),
                                                    node_manager_, dispatcher_);
  if (assembler.is_error()) {
    return assembler.error_value();
  }
  assemblers_.push_back(std::move(assembler.value()));

  RebindNodes();
  return ZX_OK;
}

void CompositeDeviceManager::RebindNodes() {
  // Take our composite nodes and run them through the device groups again.
  std::list<std::weak_ptr<Node>> nodes = std::move(nodes_);
  for (auto& weak_node : nodes) {
    auto node = weak_node.lock();
    if (!node) {
      continue;
    }
    // Try and bind our node again. If this is successful, it was already re-added
    // to nodes_. If it is not successful, we will manually add it.
    if (!BindNode(node)) {
      nodes_.push_back(node);
    }
  }

  rebind_callback_();
}

bool CompositeDeviceManager::BindNode(std::shared_ptr<Node> node) {
  bool did_match = false;
  for (auto& assembler : assemblers_) {
    if (assembler->BindNode(node)) {
      // We do not break here because DFv1 composites allow for MULTIBIND.
      // For example, the sysmem fragment can match multiple composite devices.
      // To support that, nodes can bind to multiple composite devices.
      did_match = true;
    }
  }

  if (did_match) {
    nodes_.push_back(node->weak_from_this());
  }
  return did_match;
}

void CompositeDeviceManager::Publish(component::OutgoingDirectory& outgoing) {
  auto result = outgoing.AddProtocol<fuchsia_device_composite::DeprecatedCompositeCreator>(this);
  ZX_ASSERT(result.is_ok());
}

void CompositeDeviceManager::Inspect(inspect::Node& root) const {
  for (auto& assembler : assemblers_) {
    assembler->Inspect(root);
  }
}

void CompositeDeviceManager::AddCompositeDevice(AddCompositeDeviceRequest& request,
                                                AddCompositeDeviceCompleter::Sync& completer) {
  zx_status_t status = AddCompositeDevice(request.name(), request.args());
  completer.Reply(zx::make_status(status));
}

}  // namespace dfv2
