// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/device.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/binding_priv.h>
#include <lib/fpromise/bridge.h>
#include <lib/stdcompat/span.h>
#include <zircon/errors.h>

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
               std::optional<Device*> parent, std::optional<Device*> linked_device,
               driver::Logger& logger, async_dispatcher_t* dispatcher)
    : name_(name),
      context_(context),
      ops_(ops),
      logger_(logger),
      dispatcher_(dispatcher),
      parent_(parent),
      linked_device_(linked_device ? **linked_device : *this),
      executor_(dispatcher) {}

Device::~Device() {
  if (vnode_teardown_callback_) {
    (*vnode_teardown_callback_)();
  }

  // Technically we shouldn't unbind here, since unbind should go parent to child.
  // However, this is much simpler than going parent to child, and this
  // *technically* upholds the same invariant, because at this point we know
  // the device does not have any children.
  // Also, if a device has unbind, it would be an error to call Release before
  // Unbind.
  // This may be a potential difference in behavior from DFv1, so this needs
  // to be investigated further. For now, it will let us run integration tests.
  // TODO(fxbug.dev/92196)
  if (HasOp(ops_, &zx_protocol_device_t::unbind)) {
    ops_->unbind(context_);
  }
  if (HasOp(ops_, &zx_protocol_device_t::release)) {
    ops_->release(context_);
  }
}

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

bool Device::HasChildren() const { return !children_.empty(); }

zx_status_t Device::Add(device_add_args_t* zx_args, zx_device_t** out) {
  auto device = std::make_shared<Device>(zx_args->name, zx_args->ctx, zx_args->ops, this,
                                         std::nullopt, logger_, dispatcher_);
  auto device_ptr = device.get();
  if (zx_args->proto_id) {
    device_ptr->proto_id_ = zx_args->proto_id;
    device_ptr->proto_ops_ = zx_args->proto_ops;
  }

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
  props.emplace_back(arena)
      .set_key(arena, fdf::wire::NodePropertyKey::WithIntValue(BIND_PROTOCOL))
      .set_value(arena, fdf::wire::NodePropertyValue::WithIntValue(zx_args->proto_id));
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
  device_ptr->controller_.Bind(std::move(controller_ends->client), dispatcher_,
                               fidl::ObserveTeardown([device = device->weak_from_this()] {
                                 // Because the dispatcher can be multi-threaded, we must use a
                                 // `fidl::WireSharedClient`. The `fidl::WireSharedClient` uses a
                                 // two-phase destruction to teardown the client.
                                 //
                                 // Because of this, the teardown might be happening after the
                                 // Device has already been erased. This is likely to occur if the
                                 // Driver is asked to shutdown. If that happens, the Driver will
                                 // free its Devices, the Device will release its NodeController,
                                 // and then this shutdown will occur later. In order to not have a
                                 // Use-After-Free here, only try to remove the Device if the
                                 // weak_ptr still exists.
                                 //
                                 // The weak pointer will be valid here if the NodeController
                                 // representing the Device exits on its own. This represents the
                                 // Device's child Driver exiting, and in that instance we want to
                                 // Remove the Device.
                                 if (auto ptr = device.lock()) {
                                   if (ptr->parent_.has_value()) {
                                     (*ptr->parent_)->RemoveChild(ptr);
                                   }
                                 }
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
  fpromise::bridge<void, std::variant<zx_status_t, fdf::NodeError>> bridge;

  auto callback = [completer = std::move(bridge.completer)](
                      fidl::WireUnownedResult<fdf::Node::AddChild>& result) mutable {
    if (!result.ok()) {
      completer.complete_error(result.error().status());
      return;
    }
    if (result->result.is_err()) {
      completer.complete_error(result->result.err());
      return;
    }
    completer.complete_ok();
  };
  node_->AddChild(std::move(args), std::move(controller_ends->server), std::move(node_server),
                  std::move(callback));
  auto task =
      bridge.consumer.promise_or(fpromise::error(ZX_ERR_UNAVAILABLE))
          .and_then([device_ptr]() {
            // Emulate fuchsia.device.manager.DeviceController behaviour, and run the
            // init task after adding the device.
            if (HasOp(device_ptr->ops_, &zx_protocol_device_t::init)) {
              device_ptr->ops_->init(device_ptr->context_);
            }
          })
          .or_else([device_ptr](std::variant<zx_status_t, fdf::NodeError>& status) {
            if (std::holds_alternative<zx_status_t>(status)) {
              FDF_LOGL(ERROR, device_ptr->logger_, "Failed to add device: status: '%s': %u",
                       device_ptr->Name(), std::get<zx_status_t>(status));
            } else if (std::holds_alternative<fdf::NodeError>(status)) {
              FDF_LOGL(ERROR, device_ptr->logger_, "Failed to add device: NodeError: '%s': %u",
                       device_ptr->Name(), std::get<fdf::NodeError>(status));
            }
          })
          .wrap_with(device_ptr->scope_);
  device_ptr->executor_.schedule_task(std::move(task));

  children_.push_back(std::move(device));

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

void Device::RemoveChild(std::shared_ptr<Device>& child) { children_.remove(child); }

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out) const {
  if (HasOp(linked_device_.ops_, &zx_protocol_device_t::get_protocol)) {
    return linked_device_.ops_->get_protocol(context_, proto_id, out);
  }

  if ((linked_device_.proto_id_ != proto_id) || (linked_device_.proto_ops_ == nullptr)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  struct GenericProtocol {
    void* ops;
    void* ctx;
  };
  auto proto = static_cast<GenericProtocol*>(out);
  proto->ops = linked_device_.proto_ops_;
  proto->ctx = linked_device_.context_;
  return ZX_OK;
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
  auto it = linked_device_.metadata_.find(type);
  if (it == linked_device_.metadata_.end()) {
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
  auto it = linked_device_.metadata_.find(type);
  if (it == linked_device_.metadata_.end()) {
    FDF_LOG(WARNING, "Metadata %#x for device '%s' not found", type, Name());
    return ZX_ERR_NOT_FOUND;
  }
  auto& [_, metadata] = *it;
  *out_size = metadata.size();
  return ZX_OK;
}

zx_status_t Device::MessageOp(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  if (!HasOp(ops_, &zx_protocol_device_t::message)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ops_->message(context_, msg, txn);
}

}  // namespace compat
