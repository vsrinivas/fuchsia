// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/device.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/binding_priv.h>
#include <lib/stdcompat/span.h>

namespace fdf = fuchsia_driver_framework;

namespace {

// Makes a valid name. This must be a valid component framework instance name.
std::string MakeValidName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (auto ch : name) {
    switch (ch) {
      case ':':
      case '.':
        out.push_back('_');
        break;
      default:
        out.push_back(ch);
    }
  }
  return out;
}

template <typename T>
bool HasOp(const zx_protocol_device_t* ops, T member) {
  return ops != nullptr && ops->*member != nullptr;
}

}  // namespace

namespace compat {

Device::Device(const char* name, void* context, const zx_protocol_device_t* ops,
               driver::Logger& logger, async_dispatcher_t* dispatcher)
    : name_(name),
      context_(context),
      ops_(ops),
      logger_(logger),
      dispatcher_(dispatcher),
      child_node_(&node_) {}

zx_device_t* Device::ZxDevice() { return static_cast<zx_device_t*>(this); }

void Device::Link(Device* parent) { parent->child_node_ = &node_; }

void Device::Bind(fidl::ClientEnd<fdf::Node> client_end) {
  node_.Bind(std::move(client_end), dispatcher_);
}

void Device::Unbind() {
  // This closes the client-end of the node to signal to the driver framework
  // that node should be removed.
  //
  // `fidl::WireClient` does not provide a direct way to unbind a client, so we
  // assign a default client to unbind the existing client.
  node_ = {};
}

const char* Device::Name() const { return name_.data(); }

zx_status_t Device::Add(device_add_args_t* zx_args, zx_device_t** out) {
  if (!*child_node_) {
    FDF_LOG(ERROR, "Failed to add device '%s' (to parent '%s'), invalid node", zx_args->name,
            Name());
    return ZX_ERR_BAD_STATE;
  }
  auto child =
      std::make_unique<Device>(zx_args->name, zx_args->ctx, zx_args->ops, logger_, dispatcher_);

  // Create NodeAddArgs from `zx_args`.
  fidl::Arena arena;
  std::vector<fdf::wire::NodeSymbol> symbols;
  symbols.emplace_back(arena)
      .set_name(arena, fidl::StringView::FromExternal(kName))
      .set_address(arena, reinterpret_cast<uint64_t>(child->Name()));
  symbols.emplace_back(arena)
      .set_name(arena, fidl::StringView::FromExternal(kContext))
      .set_address(arena, reinterpret_cast<uint64_t>(zx_args->ctx));
  symbols.emplace_back(arena)
      .set_name(arena, fidl::StringView::FromExternal(kOps))
      .set_address(arena, reinterpret_cast<uint64_t>(zx_args->ops));
  symbols.emplace_back(arena)
      .set_name(arena, fidl::StringView::FromExternal(kParent))
      .set_address(arena, reinterpret_cast<uint64_t>(child.get()));
  std::vector<fdf::wire::NodeProperty> props;
  props.reserve(zx_args->prop_count);
  for (auto [id, _, value] : cpp20::span(zx_args->props, zx_args->prop_count)) {
    props.emplace_back(arena)
        .set_key(arena, fdf::wire::NodePropertyKey::WithIntValue(arena, id))
        .set_value(arena, fdf::wire::NodePropertyValue::WithIntValue(arena, value));
  }
  fdf::wire::NodeAddArgs args(arena);
  auto valid_name = MakeValidName(zx_args->name);
  args.set_name(arena, fidl::StringView::FromExternal(valid_name))
      .set_symbols(arena, fidl::VectorView<fdf::wire::NodeSymbol>::FromExternal(symbols))
      .set_properties(arena, fidl::VectorView<fdf::wire::NodeProperty>::FromExternal(props));

  // Create NodeController, so we can control the child.
  auto controller_ends = fidl::CreateEndpoints<fdf::NodeController>();
  if (controller_ends.is_error()) {
    return controller_ends.status_value();
  }
  child->controller_.Bind(
      std::move(controller_ends->client), dispatcher_,
      fidl::ObserveTeardown([this, child = child.get()] { children_.erase(*child); }));

  // If the node is not bindable, we own the node.
  fidl::ServerEnd<fdf::Node> node_server;
  if ((zx_args->flags & DEVICE_ADD_NON_BINDABLE) != 0) {
    auto node_ends = fidl::CreateEndpoints<fdf::Node>();
    if (node_ends.is_error()) {
      return node_ends.status_value();
    }
    child->node_.Bind(std::move(node_ends->client), dispatcher_);
    node_server = std::move(node_ends->server);
  }

  // Add the child node.
  auto callback = [child = child.get()](fidl::WireResponse<fdf::Node::AddChild>* response) {
    if (response->result.is_err()) {
      FDF_LOGL(ERROR, child->logger_, "Failed to add device '%s': %u", child->Name(),
               response->result.err());
      return;
    }
    // Emulate fuchsia.device.manager.DeviceController behaviour, and run the
    // init task after adding the device.
    if (HasOp(child->ops_, &zx_protocol_device_t::init)) {
      child->ops_->init(child->context_);
    }
  };
  (*child_node_)
      ->AddChild(std::move(args), std::move(controller_ends->server), std::move(node_server),
                 std::move(callback));

  *out = child->ZxDevice();
  children_.push_back(std::move(child));
  return ZX_OK;
}

void Device::Remove() {
  if (!controller_) {
    FDF_LOG(ERROR, "Failed to remove device '%s', invalid node controller", Name());
    return;
  }
  auto result = controller_->Remove();
  if (!result.ok()) {
    FDF_LOG(ERROR, "Failed to remove device '%s': %s", Name(), result.FormatDescription().data());
  }
}

bool Device::HasChildren() const { return !children_.is_empty(); }

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out) const {
  if (!HasOp(ops_, &zx_protocol_device_t::get_protocol)) {
    FDF_LOG(WARNING, "Protocol %#x for device '%s' unavailable", proto_id, Name());
    return ZX_ERR_UNAVAILABLE;
  }
  return ops_->get_protocol(context_, proto_id, out);
}

zx_status_t Device::AddMetadata(uint32_t type, const void* data, size_t size) {
  Metadata metadata(size);
  auto begin = static_cast<const uint8_t*>(data);
  std::copy(begin, begin + size, metadata.begin());
  auto [_, inserted] = metadata_.emplace(type, std::move(metadata));
  if (!inserted) {
    FDF_LOG(WARNING, "Metadata %#x for device '%s' already exists", type, Name());
    return ZX_ERR_ALREADY_EXISTS;
  }
  return ZX_OK;
}

zx_status_t Device::GetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) {
  auto it = metadata_.find(type);
  if (it == metadata_.end()) {
    FDF_LOG(WARNING, "Metadata %#x for device '%s' not found", type, Name());
    return ZX_ERR_NOT_FOUND;
  }
  auto& [_, metadata] = *it;

  auto size = std::min(buflen, metadata.size());
  auto begin = metadata.begin();
  std::copy(begin, begin + size, static_cast<uint8_t*>(buf));

  *actual = metadata.size();
  return ZX_OK;
}

zx_status_t Device::GetMetadataSize(uint32_t type, size_t* out_size) {
  auto it = metadata_.find(type);
  if (it == metadata_.end()) {
    FDF_LOG(WARNING, "Metadata %#x for device '%s' not found", type, Name());
    return ZX_ERR_NOT_FOUND;
  }
  auto& [_, metadata] = *it;
  *out_size = metadata.size();
  return ZX_OK;
}

}  // namespace compat
