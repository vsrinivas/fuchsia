// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/composite_assembler.h"

#include "src/devices/lib/log/log.h"

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

zx::status<std::unique_ptr<CompositeDeviceAssembler>> CompositeDeviceAssembler::Create(
    std::string name, fuchsia_device_manager::CompositeDeviceDescriptor descriptor,
    DriverBinder* binder, async_dispatcher_t* dispatcher) {
  auto assembler = std::make_unique<CompositeDeviceAssembler>();
  assembler->name_ = std::move(name);
  assembler->binder_ = binder;
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
    auto property = fdf::wire::NodeProperty::Builder(assembler->arena_);

    property.key(fdf::wire::NodePropertyKey::WithIntValue(prop.id()));
    property.value(fdf::wire::NodePropertyValue::WithIntValue(prop.value()));
    assembler->properties_.push_back(property.Build());
  }

  // Create the string properties.
  for (auto& prop : descriptor.str_props()) {
    auto property = fdf::wire::NodeProperty::Builder(assembler->arena_);

    property.key(fdf::wire::NodePropertyKey::WithStringValue(assembler->arena_, prop.key()));
    switch (prop.value().Which()) {
      case fuchsia_device_manager::PropertyValue::Tag::kBoolValue:
        property.value(
            fdf::wire::NodePropertyValue::WithBoolValue(prop.value().bool_value().value()));
        break;

      case fuchsia_device_manager::PropertyValue::Tag::kIntValue:
        property.value(
            fdf::wire::NodePropertyValue::WithIntValue(prop.value().int_value().value()));
        break;

      case fuchsia_device_manager::PropertyValue::Tag::kStrValue:
        property.value(fdf::wire::NodePropertyValue::WithStringValue(
            assembler->arena_, prop.value().str_value().value()));
        break;

      case fuchsia_device_manager::PropertyValue::Tag::kEnumValue:
        property.value(fdf::wire::NodePropertyValue::WithEnumValue(
            assembler->arena_, prop.value().enum_value().value()));
        break;
    }
    assembler->properties_.push_back(property.Build());
  }

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
                                        std::move(properties_), binder_, dispatcher_);
  if (node.is_error()) {
    return;
  }

  LOGF(INFO, "Built composite device at '%s'", node->TopoName().c_str());

  // Bind the node we just created.
  binder_->Bind(*node.value(), nullptr);
}

CompositeDeviceManager::CompositeDeviceManager(DriverBinder* binder, async_dispatcher_t* dispatcher)
    : binder_(binder), dispatcher_(dispatcher) {}

zx_status_t CompositeDeviceManager::AddCompositeDevice(
    std::string name, fuchsia_device_manager::CompositeDeviceDescriptor descriptor) {
  auto assembler = CompositeDeviceAssembler::Create(std::move(name), std::move(descriptor), binder_,
                                                    dispatcher_);
  if (assembler.is_error()) {
    return assembler.error_value();
  }
  assemblers_.push_back(std::move(assembler.value()));
  return ZX_OK;
}

bool CompositeDeviceManager::BindNode(std::shared_ptr<Node> node) {
  for (auto& assembler : assemblers_) {
    if (assembler->BindNode(node)) {
      return true;
    }
  }
  return false;
}

}  // namespace dfv2
