// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/device.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/binding_priv.h>
#include <lib/stdcompat/span.h>

#include "src/devices/lib/compat/symbols.h"

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

Device::Device(std::string_view name, void* context, const zx_protocol_device_t* ops,
               std::optional<Device*> parent, driver::Logger& logger,
               async_dispatcher_t* dispatcher)
    : name_(name),
      context_(context),
      ops_(ops),
      logger_(logger),
      dispatcher_(dispatcher),
      parent_(parent ? **parent : *this) {}

zx_device_t* Device::ZxDevice() { return static_cast<zx_device_t*>(this); }

void Device::Bind(fidl::WireSharedClient<fdf::Node> node) { node_ = std::move(node); }

void Device::Unbind() {
  // This closes the client-end of the node to signal to the driver framework
  // that node should be removed.
  //
  // `fidl::WireClient` does not provide a direct way to unbind a client, so we
  // assign a default client to unbind the existing client.
  node_ = {};
}

const char* Device::Name() const { return name_.data(); }

bool Device::HasChildren() const { return child_counter_.use_count() > 1; }

zx_status_t Device::Add(device_add_args_t* zx_args, zx_device_t** out) {
  auto device = std::make_unique<Device>(zx_args->name, zx_args->ctx, zx_args->ops, std::nullopt,
                                         logger_, dispatcher_);
  auto device_ptr = device.get();

  // Create NodeAddArgs from `zx_args`.
  fidl::Arena arena;
  std::vector<fdf::wire::NodeSymbol> symbols;
  symbols.emplace_back(arena)
      .set_name(arena, kName)
      .set_address(arena, reinterpret_cast<uint64_t>(device_ptr->Name()));
  symbols.emplace_back(arena)
      .set_name(arena, kContext)
      .set_address(arena, reinterpret_cast<uint64_t>(zx_args->ctx));
  symbols.emplace_back(arena)
      .set_name(arena, kOps)
      .set_address(arena, reinterpret_cast<uint64_t>(zx_args->ops));
  symbols.emplace_back(arena)
      .set_name(arena, kParent)
      .set_address(arena, reinterpret_cast<uint64_t>(device_ptr));
  std::vector<fdf::wire::NodeProperty> props;
  props.reserve(zx_args->prop_count);
  for (auto [id, _, value] : cpp20::span(zx_args->props, zx_args->prop_count)) {
    props.emplace_back(arena)
        .set_key(arena, fdf::wire::NodePropertyKey::WithIntValue(id))
        .set_value(arena, fdf::wire::NodePropertyValue::WithIntValue(value));
  }
  fdf::wire::NodeAddArgs args(arena);
  auto valid_name = MakeValidName(zx_args->name);
  args.set_name(arena, fidl::StringView::FromExternal(valid_name))
      .set_symbols(arena, fidl::VectorView<fdf::wire::NodeSymbol>::FromExternal(symbols))
      .set_properties(arena, fidl::VectorView<fdf::wire::NodeProperty>::FromExternal(props));

  // Create NodeController, so we can control the device.
  auto controller_ends = fidl::CreateEndpoints<fdf::NodeController>();
  if (controller_ends.is_error()) {
    return controller_ends.status_value();
  }
  device_ptr->controller_.Bind(
      std::move(controller_ends->client), dispatcher_,
      fidl::ObserveTeardown([device = std::move(device), counter = child_counter_] {
        // When we observe teardown, we will destroy both the device and the
        // shared counter associated with it.
        //
        // Because the dispatcher is multi-threaded, we must use a
        // `fidl::WireSharedClient`. Because we use a `fidl::WireSharedClient`,
        // a two-phase destruction must occur to safely teardown the client.
        //
        // Here, we follow the FIDL recommendation to make the observer be in
        // charge of deallocation by taking ownership of the unique_ptr. See:
        // https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/llcpp-threading#custom_teardown_observer
      }));

  // If the node is not bindable, we own the node.
  fidl::ServerEnd<fdf::Node> node_server;
  if ((zx_args->flags & DEVICE_ADD_NON_BINDABLE) != 0) {
    auto node_ends = fidl::CreateEndpoints<fdf::Node>();
    if (node_ends.is_error()) {
      return node_ends.status_value();
    }
    device_ptr->node_.Bind(std::move(node_ends->client), dispatcher_);
    node_server = std::move(node_ends->server);
  }

  // Add the device node.
  auto callback = [device_ptr](fidl::WireUnownedResult<fdf::Node::AddChild>& result) {
    if (!result.ok()) {
      FDF_LOGL(ERROR, device_ptr->logger_, "Failed to add device '%s': %s", device_ptr->Name(),
               result.error().FormatDescription().data());
      return;
    }
    if (result->result.is_err()) {
      FDF_LOGL(ERROR, device_ptr->logger_, "Failed to add device '%s': %u", device_ptr->Name(),
               result->result.err());
      return;
    }
    // Emulate fuchsia.device.manager.DeviceController behaviour, and run the
    // init task after adding the device.
    if (HasOp(device_ptr->ops_, &zx_protocol_device_t::init)) {
      device_ptr->ops_->init(device_ptr->context_);
    }
  };
  node_->AddChild(std::move(args), std::move(controller_ends->server), std::move(node_server),
                  std::move(callback));

  *out = device_ptr->ZxDevice();
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

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out) const {
  if (!HasOp(ops_, &zx_protocol_device_t::get_protocol)) {
    FDF_LOG(WARNING, "Protocol %u for device '%s' unavailable", proto_id, Name());
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
  auto it = parent_.metadata_.find(type);
  if (it == parent_.metadata_.end()) {
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
  auto it = parent_.metadata_.find(type);
  if (it == parent_.metadata_.end()) {
    FDF_LOG(WARNING, "Metadata %#x for device '%s' not found", type, Name());
    return ZX_ERR_NOT_FOUND;
  }
  auto& [_, metadata] = *it;
  *out_size = metadata.size();
  return ZX_OK;
}

}  // namespace compat
