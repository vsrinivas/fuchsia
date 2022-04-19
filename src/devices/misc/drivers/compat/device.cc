// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/device.h"

#include <fidl/fuchsia.driver.framework/cpp/wire_types.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/binding_priv.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/bridge.h>
#include <lib/stdcompat/span.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>

#include "src/devices/lib/compat/symbols.h"

namespace fdf {
using namespace fuchsia_driver_framework;
}
namespace fcd = fuchsia_component_decl;

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

Device::Device(device_t device, const zx_protocol_device_t* ops, Driver* driver,
               std::optional<Device*> parent, driver::Logger& logger,
               async_dispatcher_t* dispatcher)
    : compat_child_(std::string(device.name), device.proto_ops.id, "", MetadataMap()),
      name_(device.name),
      logger_(logger),
      dispatcher_(dispatcher),
      driver_(driver),
      compat_symbol_(device),
      ops_(ops),
      parent_(parent),
      executor_(dispatcher) {}

Device::~Device() {
  // We only shut down the devices that have a parent, since that means that *this* compat driver
  // owns the device. If the device does not have a parent, then ops_ belongs to another driver, and
  // it's that driver's responsibility to be shut down.
  if (parent_) {
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
      ops_->unbind(compat_symbol_.context);
    }

    // Call the parent's pre-release.
    if (HasOp((*parent_)->ops_, &zx_protocol_device_t::child_pre_release)) {
      (*parent_)->ops_->child_pre_release((*parent_)->compat_symbol_.context,
                                          compat_symbol_.context);
    }

    if (HasOp(ops_, &zx_protocol_device_t::release)) {
      ops_->release(compat_symbol_.context);
    }
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
  device_t compat_device = {
      .proto_ops =
          {
              .ops = zx_args->proto_ops,
              .id = zx_args->proto_id,
          },
      .name = zx_args->name,
      .context = zx_args->ctx,
  };

  auto device =
      std::make_shared<Device>(compat_device, zx_args->ops, driver_, this, logger_, dispatcher_);
  // Update the compat symbol name pointer with a pointer the device owns.
  device->compat_symbol_.name = device->name_.data();

  device->topological_path_ = topological_path_;
  if (!device->topological_path_.empty()) {
    device->topological_path_ += "/";
  }
  device->topological_path_ += device->name_;

  device->dev_vnode_ = fbl::MakeRefCounted<DevfsVnode>(device->ZxDevice());
  device->compat_child_ = Child(std::string(zx_args->name), zx_args->proto_id,
                                std::string(device->topological_path()), MetadataMap());

  auto device_ptr = device.get();

  // Add the metadata from add_args:
  for (size_t i = 0; i < zx_args->metadata_count; i++) {
    auto status =
        device->AddMetadata(zx_args->metadata_list[i].type, zx_args->metadata_list[i].data,
                            zx_args->metadata_list[i].length);
    if (status != ZX_OK) {
      return status;
    }
  }

  // +1 for the implicit BIND_PROTOCOL property.
  device->properties_.reserve(zx_args->prop_count + zx_args->str_prop_count + 1);
  bool has_protocol = false;
  for (auto [id, _, value] : cpp20::span(zx_args->props, zx_args->prop_count)) {
    device->properties_.emplace_back(arena_)
        .set_key(arena_, fdf::wire::NodePropertyKey::WithIntValue(id))
        .set_value(arena_, fdf::wire::NodePropertyValue::WithIntValue(value));
    if (id == BIND_PROTOCOL) {
      has_protocol = true;
    }
  }

  for (auto [key, value] : cpp20::span(zx_args->str_props, zx_args->str_prop_count)) {
    auto& ref = device->properties_.emplace_back(arena_).set_key(
        arena_,
        fdf::wire::NodePropertyKey::WithStringValue(arena_, fidl::StringView::FromExternal(key)));
    switch (value.value_type) {
      case ZX_DEVICE_PROPERTY_VALUE_BOOL:
        ref.set_value(arena_, fdf::wire::NodePropertyValue::WithBoolValue(value.value.bool_val));
        break;
      case ZX_DEVICE_PROPERTY_VALUE_STRING:
        ref.set_value(arena_, fdf::wire::NodePropertyValue::WithStringValue(
                                  arena_, fidl::StringView::FromExternal(value.value.str_val)));
        break;
      case ZX_DEVICE_PROPERTY_VALUE_INT:
        ref.set_value(arena_, fdf::wire::NodePropertyValue::WithIntValue(value.value.int_val));
        break;
      case ZX_DEVICE_PROPERTY_VALUE_ENUM:
        ref.set_value(arena_, fdf::wire::NodePropertyValue::WithEnumValue(
                                  arena_, fidl::StringView::FromExternal(value.value.enum_val)));
        break;
      default:
        FDF_LOG(ERROR, "Unsupported property type, key: %s", key);
        break;
    }
  }

  // Some DFv1 devices expect to be able to set their own protocol, without specifying proto_id.
  // If we saw a BIND_PROTOCOL property, don't add our own.
  if (!has_protocol) {
    device->properties_.emplace_back(arena_)
        .set_key(arena_, fdf::wire::NodePropertyKey::WithIntValue(BIND_PROTOCOL))
        .set_value(arena_, fdf::wire::NodePropertyValue::WithIntValue(zx_args->proto_id));
  }
  device->device_flags_ = zx_args->flags;

  children_.push_back(std::move(device));

  if (out) {
    *out = device_ptr->ZxDevice();
  }

  // Emulate fuchsia.device.manager.DeviceController behaviour, and run the
  // init task after adding the device.
  if (HasOp(device_ptr->ops_, &zx_protocol_device_t::init)) {
    executor_.schedule_task(fpromise::make_promise([device_ptr]() {
                              device_ptr->ops_->init(device_ptr->compat_symbol_.context);
                            }).wrap_with(device_ptr->scope_));
  } else {
    device_ptr->InitReply(ZX_OK);
  }

  return ZX_OK;
}

zx_status_t Device::CreateNode() {
  // Create NodeAddArgs from `zx_args`.
  fidl::Arena arena;

  auto offers = compat_child_.CreateOffers(arena);

  std::vector<fdf::wire::NodeSymbol> symbols;
  symbols.emplace_back(arena)
      .set_name(arena, kDeviceSymbol)
      .set_address(arena, reinterpret_cast<uint64_t>(&compat_symbol_));
  symbols.emplace_back(arena)
      .set_name(arena, kOps)
      .set_address(arena, reinterpret_cast<uint64_t>(ops_));

  fdf::wire::NodeAddArgs args(arena);
  auto valid_name = MakeValidName(name_);
  args.set_name(arena, fidl::StringView::FromExternal(valid_name))
      .set_symbols(arena, fidl::VectorView<fdf::wire::NodeSymbol>::FromExternal(symbols))
      .set_offers(arena,
                  fidl::VectorView<fcd::wire::Offer>::FromExternal(offers.data(), offers.size()))
      .set_properties(arena, fidl::VectorView<fdf::wire::NodeProperty>::FromExternal(properties_));

  // Create NodeController, so we can control the device.
  auto controller_ends = fidl::CreateEndpoints<fdf::NodeController>();
  if (controller_ends.is_error()) {
    return controller_ends.status_value();
  }

  fpromise::bridge<> teardown_bridge;
  controller_teardown_finished_.emplace(teardown_bridge.consumer.promise());
  controller_.Bind(
      std::move(controller_ends->client), dispatcher_,
      fidl::ObserveTeardown(
          [device = weak_from_this(), completer = std::move(teardown_bridge.completer)]() mutable {
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
              // If there's a pending rebind, don't remove our parent's reference to us.
              if (ptr->parent_.has_value() && !ptr->pending_rebind_) {
                (*ptr->parent_)->RemoveChild(ptr);
              }
              completer.complete_ok();
            }
          }));

  // If the node is not bindable, we own the node.
  fidl::ServerEnd<fdf::Node> node_server;
  if ((device_flags_ & DEVICE_ADD_NON_BINDABLE) != 0) {
    auto node_ends = fidl::CreateEndpoints<fdf::Node>();
    if (node_ends.is_error()) {
      return node_ends.status_value();
    }
    node_.Bind(std::move(node_ends->client), dispatcher_);
    node_server = std::move(node_ends->server);
  }

  // Add the device node.
  if (!(*parent_)->node_.is_valid()) {
    FDF_LOG(ERROR, "Cannot add device, as parent '%s' is not marked NON_BINDABLE.",
            (*parent_)->topological_path_.data());
    return ZX_ERR_NOT_SUPPORTED;
  }

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
  (*parent_)
      ->node_->AddChild(args, std::move(controller_ends->server), std::move(node_server))
      .ThenExactlyOnce(std::move(callback));

  auto task = bridge.consumer.promise()
                  .or_else([this](std::variant<zx_status_t, fdf::NodeError>& status) {
                    if (std::holds_alternative<zx_status_t>(status)) {
                      FDF_LOG(ERROR, "Failed to add device: status: '%s': %u", Name(),
                              std::get<zx_status_t>(status));
                    } else if (std::holds_alternative<fdf::NodeError>(status)) {
                      FDF_LOG(ERROR, "Failed to add device: NodeError: '%s': %u", Name(),
                              std::get<fdf::NodeError>(status));
                    }
                  })
                  .wrap_with(scope_);
  executor_.schedule_task(std::move(task));
  return ZX_OK;
}

void Device::Remove() {
  executor_.schedule_task(
      WaitForInitToComplete().then([this](fpromise::result<void, zx_status_t>& init) {
        // This should be called if we hit an error trying to remove the controller.
        auto schedule_removal = fit::defer([this]() {
          if (parent_.has_value()) {
            auto shared = shared_from_this();
            // We schedule our removal on our parent's executor because we can't be removed
            // while being run in a promise on our own executor.
            (*parent_)->executor_.schedule_task(
                fpromise::make_promise([parent = *parent_, shared = std::move(shared)]() mutable {
                  parent->RemoveChild(shared);
                }));
          }
        });

        if (!controller_) {
          FDF_LOG(ERROR, "Failed to remove device '%s', invalid node controller", Name());
          return;
        }
        auto result = controller_->Remove();
        if (!result.ok() && !result.is_peer_closed()) {
          FDF_LOG(ERROR, "Failed to remove device '%s': %s", Name(),
                  result.FormatDescription().data());
          if (parent_.has_value()) {
          }
        }
        schedule_removal.cancel();
      }));
}

void Device::RemoveChild(std::shared_ptr<Device>& child) { children_.remove(child); }

void Device::InsertOrUpdateProperty(fuchsia_driver_framework::wire::NodePropertyKey key,
                                    fuchsia_driver_framework::wire::NodePropertyValue value) {
  bool found = false;
  for (auto& prop : properties_) {
    if (!prop.has_key()) {
      continue;
    }

    if (prop.key().Which() != key.Which()) {
      continue;
    }

    if (key.is_string_value()) {
      std::string_view prop_key_view(prop.key().string_value().data(),
                                     prop.key().string_value().size());
      std::string_view key_view(key.string_value().data(), key.string_value().size());
      if (key_view == prop_key_view) {
        found = true;
      }
    } else if (key.is_int_value()) {
      if (key.int_value() == prop.key().int_value()) {
        found = true;
      }
    }

    if (found) {
      prop.value() = value;
      break;
    }
  }
  if (!found) {
    properties_.emplace_back(arena_).set_key(arena_, key).set_value(arena_, value);
  }
}

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out) const {
  if (HasOp(ops_, &zx_protocol_device_t::get_protocol)) {
    return ops_->get_protocol(compat_symbol_.context, proto_id, out);
  }

  if ((compat_symbol_.proto_ops.id != proto_id) || (compat_symbol_.proto_ops.ops == nullptr)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!out) {
    return ZX_OK;
  }

  struct GenericProtocol {
    void* ops;
    void* ctx;
  };

  auto proto = static_cast<GenericProtocol*>(out);
  proto->ops = compat_symbol_.proto_ops.ops;
  proto->ctx = compat_symbol_.context;
  return ZX_OK;
}

zx_status_t Device::AddMetadata(uint32_t type, const void* data, size_t size) {
  return compat_child_.compat_device().AddMetadata(type, data, size);
}

zx_status_t Device::GetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) {
  return compat_child_.compat_device().GetMetadata(type, buf, buflen, actual);
}

zx_status_t Device::GetMetadataSize(uint32_t type, size_t* out_size) {
  return compat_child_.compat_device().GetMetadataSize(type, out_size);
}

zx_status_t Device::MessageOp(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  if (!HasOp(ops_, &zx_protocol_device_t::message)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ops_->message(compat_symbol_.context, msg, txn);
}

void Device::InitReply(zx_status_t status) {
  std::scoped_lock lock(init_lock_);
  init_is_finished_ = true;
  init_status_ = status;
  for (auto& waiter : init_waiters_) {
    if (status == ZX_OK) {
      waiter.complete_ok();
    } else {
      waiter.complete_error(init_status_);
    }
  }
  init_waiters_.clear();
}

zx_status_t Device::ReadOp(void* data, size_t len, size_t off, size_t* out_actual) {
  if (!HasOp(ops_, &zx_protocol_device_t::read)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ops_->read(compat_symbol_.context, data, len, off, out_actual);
}

zx_status_t Device::WriteOp(const void* data, size_t len, size_t off, size_t* out_actual) {
  if (!HasOp(ops_, &zx_protocol_device_t::write)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ops_->write(compat_symbol_.context, data, len, off, out_actual);
}

fpromise::promise<void, zx_status_t> Device::WaitForInitToComplete() {
  std::scoped_lock lock(init_lock_);
  if (init_is_finished_) {
    if (init_status_ == ZX_OK) {
      return fpromise::make_result_promise<void, zx_status_t>(fpromise::ok());
    }
    return fpromise::make_result_promise<void, zx_status_t>(fpromise::error(init_status_));
  }
  fpromise::bridge<void, zx_status_t> bridge;
  init_waiters_.push_back(std::move(bridge.completer));

  return bridge.consumer.promise_or(fpromise::error(ZX_ERR_UNAVAILABLE));
}

constexpr char kCompatKey[] = "fuchsia.compat.LIBNAME";
fpromise::promise<void, zx_status_t> Device::RebindToLibname(std::string_view libname) {
  if (controller_teardown_finished_ == std::nullopt) {
    FDF_LOG(ERROR, "Calling rebind before device is set up?");
    return fpromise::make_error_promise(ZX_ERR_BAD_STATE);
  }
  InsertOrUpdateProperty(
      fdf::wire::NodePropertyKey::WithStringValue(arena_,
                                                  fidl::StringView::FromExternal(kCompatKey)),
      fdf::wire::NodePropertyValue::WithStringValue(arena_, fidl::StringView(arena_, libname)));
  // Once the controller teardown is finished (and the device is safely deleted),
  // we re-create the device.
  pending_rebind_ = true;
  auto promise =
      std::move(controller_teardown_finished_.value())
          .or_else([]() -> fpromise::result<void, zx_status_t> {
            ZX_ASSERT_MSG(false, "Unbind should always succeed");
          })
          .and_then([weak = weak_from_this()]() mutable -> fpromise::result<void, zx_status_t> {
            auto ptr = weak.lock();
            if (!ptr) {
              return fpromise::error(ZX_ERR_CANCELED);
            }
            // Reset FIDL clients so they don't complain when rebound.
            ptr->controller_ = {};
            ptr->node_ = {};
            zx_status_t status = ptr->CreateNode();
            ptr->pending_rebind_ = false;
            if (status != ZX_OK) {
              FDF_LOGL(ERROR, ptr->logger(), "Failed to recreate node: %s",
                       zx_status_get_string(status));
              return fpromise::error(status);
            }

            return fpromise::ok();
          })
          .wrap_with(scope_);
  Remove();
  return promise;
}

}  // namespace compat
