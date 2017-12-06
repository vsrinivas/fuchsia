// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_server_fidl_impl.h"

#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/gatt/connection.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"
#include "garnet/drivers/bluetooth/lib/gatt/server.h"

#include "fidl_helpers.h"

// Make the FIDL namespace explicit.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {
namespace {

::btlib::att::ErrorCode GattErrorCodeFromFidl(
    ::btfidl::gatt::ErrorCode error_code,
    bool is_read) {
  switch (error_code) {
    case ::btfidl::gatt::ErrorCode::NO_ERROR:
      return ::btlib::att::ErrorCode::kNoError;
    case ::btfidl::gatt::ErrorCode::INVALID_OFFSET:
      return ::btlib::att::ErrorCode::kInvalidOffset;
    case ::btfidl::gatt::ErrorCode::INVALID_VALUE_LENGTH:
      return ::btlib::att::ErrorCode::kInvalidAttributeValueLength;
    case ::btfidl::gatt::ErrorCode::NOT_PERMITTED:
      if (is_read)
        return ::btlib::att::ErrorCode::kReadNotPermitted;
      return ::btlib::att::ErrorCode::kWriteNotPermitted;
    default:
      break;
  }
  return ::btlib::att::ErrorCode::kUnlikelyError;
}

void ParseProperties(
    const ::fidl::Array<::btfidl::gatt::CharacteristicProperty>& properties,
    uint8_t* out_props,
    uint16_t* out_ext_props) {
  FXL_DCHECK(out_props);
  FXL_DCHECK(out_ext_props);

  *out_props = 0;
  *out_ext_props = 0;
  if (properties && !properties.empty()) {
    for (const auto& prop : properties) {
      switch (prop) {
        case ::btfidl::gatt::CharacteristicProperty::BROADCAST:
          *out_props |= ::btlib::gatt::Property::kBroadcast;
          break;
        case ::btfidl::gatt::CharacteristicProperty::READ:
          *out_props |= ::btlib::gatt::Property::kRead;
          break;
        case ::btfidl::gatt::CharacteristicProperty::WRITE_WITHOUT_RESPONSE:
          *out_props |= ::btlib::gatt::Property::kWriteWithoutResponse;
          break;
        case ::btfidl::gatt::CharacteristicProperty::WRITE:
          *out_props |= ::btlib::gatt::Property::kWrite;
          break;
        case ::btfidl::gatt::CharacteristicProperty::NOTIFY:
          *out_props |= ::btlib::gatt::Property::kNotify;
          break;
        case ::btfidl::gatt::CharacteristicProperty::INDICATE:
          *out_props |= ::btlib::gatt::Property::kIndicate;
          break;
        case ::btfidl::gatt::CharacteristicProperty::
            AUTHENTICATED_SIGNED_WRITES:
          *out_props |= ::btlib::gatt::Property::kAuthenticatedSignedWrites;
          break;
        case ::btfidl::gatt::CharacteristicProperty::RELIABLE_WRITE:
          *out_props |= ::btlib::gatt::Property::kExtendedProperties;
          *out_ext_props |= ::btlib::gatt::ExtendedProperty::kReliableWrite;
          break;
        case ::btfidl::gatt::CharacteristicProperty::WRITABLE_AUXILIARIES:
          *out_props |= ::btlib::gatt::Property::kExtendedProperties;
          *out_ext_props |=
              ::btlib::gatt::ExtendedProperty::kWritableAuxiliaries;
          break;
      }
    }
  }
}

::btlib::att::AccessRequirements ParseSecurityRequirements(
    const ::btfidl::gatt::SecurityRequirementsPtr& reqs) {
  if (!reqs) {
    return ::btlib::att::AccessRequirements();
  }
  return ::btlib::att::AccessRequirements(reqs->encryption_required,
                                          reqs->authentication_required,
                                          reqs->authorization_required);
}

// Carries a either a successful Result or an error message that can be sent as
// a FIDL response.
template <typename Result,
          typename Error = std::string,
          typename = std::enable_if_t<!std::is_same<Result, Error>::value>>
struct MaybeResult final {
  explicit MaybeResult(Result&& result)
      : result(std::forward<Result>(result)) {}

  explicit MaybeResult(Error&& error) : error(std::forward<Error>(error)) {}

  bool is_error() const { return static_cast<bool>(!result); }

  Result result;
  Error error;
};

using DescriptorResult = MaybeResult<::btlib::gatt::DescriptorPtr>;
DescriptorResult NewDescriptor(const ::btfidl::gatt::Descriptor& fidl_desc) {
  auto read_reqs = ParseSecurityRequirements(fidl_desc.permissions->read);
  auto write_reqs = ParseSecurityRequirements(fidl_desc.permissions->write);

  ::btlib::common::UUID type;
  if (!::btlib::common::StringToUuid(fidl_desc.type.get(), &type)) {
    return DescriptorResult("Invalid descriptor UUID");
  }

  return DescriptorResult(std::make_unique<::btlib::gatt::Descriptor>(
      fidl_desc.id, type, read_reqs, write_reqs));
}

using CharacteristicResult = MaybeResult<::btlib::gatt::CharacteristicPtr>;
CharacteristicResult NewCharacteristic(
    const ::btfidl::gatt::Characteristic& fidl_chrc) {
  uint8_t props;
  uint16_t ext_props;
  ParseProperties(fidl_chrc.properties, &props, &ext_props);

  if (!fidl_chrc.permissions) {
    return CharacteristicResult("Characteristic permissions missing");
  }

  bool supports_update = (props & ::btlib::gatt::Property::kNotify) ||
                         (props & ::btlib::gatt::Property::kIndicate);
  if (supports_update != static_cast<bool>(fidl_chrc.permissions->update)) {
    return CharacteristicResult(
        supports_update ? "Characteristic update permission required"
                        : "Characteristic update permission must be null");
  }

  auto read_reqs = ParseSecurityRequirements(fidl_chrc.permissions->read);
  auto write_reqs = ParseSecurityRequirements(fidl_chrc.permissions->write);
  auto update_reqs = ParseSecurityRequirements(fidl_chrc.permissions->update);

  ::btlib::common::UUID type;
  if (!::btlib::common::StringToUuid(fidl_chrc.type.get(), &type)) {
    return CharacteristicResult("Invalid characteristic UUID");
  }

  auto chrc = std::make_unique<::btlib::gatt::Characteristic>(
      fidl_chrc.id, type, props, ext_props, read_reqs, write_reqs, update_reqs);
  if (fidl_chrc.descriptors && !fidl_chrc.descriptors.empty()) {
    for (const auto& fidl_desc : fidl_chrc.descriptors) {
      if (!fidl_desc) {
        return CharacteristicResult("null descriptor");
      }
      auto desc_result = NewDescriptor(*fidl_desc);
      if (desc_result.is_error()) {
        return CharacteristicResult(std::move(desc_result.error));
      }

      chrc->AddDescriptor(std::move(desc_result.result));
    }
  }

  return CharacteristicResult(std::move(chrc));
}

}  // namespace

// Implements the gatt::Service FIDL interface. Instances of this class are only
// created by a GattServerFidlImpl.
class GattServerFidlImpl::ServiceImpl : public ::bluetooth::gatt::Service {
 public:
  ServiceImpl(GattServerFidlImpl* owner,
              uint64_t id,
              ::bluetooth::gatt::ServiceDelegatePtr delegate,
              ::fidl::InterfaceRequest<::bluetooth::gatt::Service> request,
              fxl::WeakPtr<::btlib::gap::Adapter> adapter,
              const fxl::Closure& connection_error_handler)
      : owner_(owner),
        id_(id),
        binding_(this, std::move(request)),
        delegate_(std::move(delegate)),
        adapter_(adapter) {
    FXL_DCHECK(owner_);
    binding_.set_connection_error_handler(connection_error_handler);
    delegate_.set_connection_error_handler(connection_error_handler);
  }

  // The destructor removes the GATT service
  ~ServiceImpl() override {
    CleanUp();

    // Do not notify the owner in this case. If we got here it means that
    // |owner_| deleted us.
  }

  // Returns the current delegate. Returns nullptr if the delegate was
  // disconnected (e.g. due to a call to RemoveService()).
  ::bluetooth::gatt::ServiceDelegate* delegate() { return delegate_.get(); }

 private:
  // ::bluetooth::gatt::Service overrides:
  void RemoveService() override {
    CleanUp();
    owner_->RemoveService(id_);
  }

  void NotifyValue(uint64_t characteristic_id,
                   const ::fidl::String& peer_id,
                   ::fidl::Array<uint8_t> value,
                   bool confirm) override {
    if (!adapter_)
      return;

    auto* connmgr = adapter_->le_connection_manager();
    ::btlib::gatt::LocalServiceManager::ClientCharacteristicConfig config;
    if (!connmgr->gatt_registry()->GetCharacteristicConfig(
            id_, characteristic_id, peer_id, &config)) {
      FXL_VLOG(2) << "Client has not configured characteristic (id: " << peer_id
                  << ")";
      return;
    }

    // Make sure that the client has subscribed to the requested protocol
    // method.
    if ((confirm && !config.indicate) || (!confirm && !config.notify)) {
      FXL_VLOG(2) << "Client has not subscribed to "
                  << (confirm ? "indications" : "notifications")
                  << " (id: " << peer_id << ")";
      return;
    }

    auto* gatt = adapter_->le_connection_manager()->GetGattConnection(peer_id);
    if (!gatt) {
      FXL_VLOG(2) << "Client not connected (id: " << peer_id << ")";
      return;
    }

    gatt->server()->SendNotification(
        config.handle, ::btlib::common::BufferView(value.data(), value.size()),
        confirm);
  }

  // Unregisters the underlying service if it is still active.
  void CleanUp() {
    if (!adapter_)
      return;

    adapter_->le_connection_manager()->gatt_registry()->UnregisterService(id_);
    adapter_.reset();
    delegate_ = nullptr;
  }

  // |owner_| owns this instance and is expected to outlive it.
  GattServerFidlImpl* owner_;  // weak
  uint64_t id_;

  ::fidl::Binding<::bluetooth::gatt::Service> binding_;

  // The delegate connection for the corresponding service instance. This gets
  // cleared when the service is unregistered (via RemoveService() or
  // destruction).
  ::bluetooth::gatt::ServiceDelegatePtr delegate_;

  // The adapter that the service was registered with. This gets cleared when
  // the service is unregistered (via RemoveService() or destruction).
  fxl::WeakPtr<::btlib::gap::Adapter> adapter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceImpl);
};

GattServerFidlImpl::GattServerFidlImpl(
    AdapterManager* adapter_manager,
    ::fidl::InterfaceRequest<::btfidl::gatt::Server> request,
    const ConnectionErrorHandler& connection_error_handler)
    : adapter_manager_(adapter_manager),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(adapter_manager_);
  FXL_DCHECK(connection_error_handler);
  adapter_manager_->AddObserver(this);
  binding_.set_connection_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

GattServerFidlImpl::~GattServerFidlImpl() {
  adapter_manager_->RemoveObserver(this);

  // This will remove all of our services from their adapter.
  services_.clear();
}

void GattServerFidlImpl::RemoveService(uint64_t id) {
  if (services_.erase(id)) {
    FXL_VLOG(1) << "GattServerFidlImpl: service removed (id: " << id << ")";
  } else {
    FXL_VLOG(1) << "GattServerFidlImpl: service id not found: " << id;
  }
}

void GattServerFidlImpl::PublishService(
    ::btfidl::gatt::ServiceInfoPtr service_info,
    ::fidl::InterfaceHandle<::btfidl::gatt::ServiceDelegate> delegate,
    ::fidl::InterfaceRequest<::btfidl::gatt::Service> service_iface,
    const PublishServiceCallback& callback) {
  auto adapter = adapter_manager_->GetActiveAdapter();
  if (!adapter) {
    auto error = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::BLUETOOTH_NOT_AVAILABLE,
        "Bluetooth not available on the current system");
    callback(std::move(error));
    return;
  }

  if (!service_info) {
    auto error = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::INVALID_ARGUMENTS, "A service is required");
    callback(std::move(error));
    return;
  }

  if (!delegate) {
    auto error = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::INVALID_ARGUMENTS, "A delegate is required");
    callback(std::move(error));
    return;
  }

  if (!service_iface) {
    auto error =
        fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::INVALID_ARGUMENTS,
                                     "Service interface is required");
    callback(std::move(error));
    return;
  }

  ::btlib::common::UUID service_type;
  if (!::btlib::common::StringToUuid(service_info->type.get(), &service_type)) {
    auto error = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::INVALID_ARGUMENTS, "Invalid service UUID");
    callback(std::move(error));
    return;
  }

  // Process the FIDL service tree.
  auto service = std::make_unique<::btlib::gatt::Service>(service_info->primary,
                                                          service_type);
  if (service_info->characteristics) {
    for (const auto& fidl_chrc : service_info->characteristics) {
      if (!fidl_chrc) {
        auto error = fidl_helpers::NewErrorStatus(
            ::btfidl::ErrorCode::INVALID_ARGUMENTS, "null characteristic");
        callback(std::move(error));
        return;
      }

      auto chrc_result = NewCharacteristic(*fidl_chrc);
      if (chrc_result.is_error()) {
        auto error = fidl_helpers::NewErrorStatus(
            ::btfidl::ErrorCode::INVALID_ARGUMENTS, chrc_result.error);
        callback(std::move(error));
        return;
      }

      service->AddCharacteristic(std::move(chrc_result.result));
    }
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto read_handler = [self](auto svc_id, auto id, auto offset,
                             const auto& responder) {
    if (self) {
      self->OnReadRequest(svc_id, id, offset, responder);
    } else {
      responder(::btlib::att::ErrorCode::kUnlikelyError,
                ::btlib::common::BufferView());
    }
  };
  auto write_handler = [self](auto svc_id, auto id, auto offset,
                              const auto& value, const auto& responder) {
    if (self) {
      self->OnWriteRequest(svc_id, id, offset, value, responder);
    } else {
      responder(::btlib::att::ErrorCode::kUnlikelyError);
    }
  };
  auto ccc_callback = [self](auto svc_id, auto id, const std::string& peer_id,
                             bool notify, bool indicate) {
    if (self)
      self->OnCharacteristicConfig(svc_id, id, peer_id, notify, indicate);
  };

  auto id = adapter->le_connection_manager()->gatt_registry()->RegisterService(
      std::move(service), read_handler, write_handler, ccc_callback);
  if (!id) {
    // TODO(armansito): Report a more detailed string if registration fails due
    // to duplicate ids.
    auto error = fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::FAILED,
                                              "Failed to publish service");
    callback(std::move(error));
    return;
  }

  // TODO(armansito): IDs are unique per-adapter and not global, however,
  // since we unregister all services when an adapter changes, the IDs should
  // never clash.
  //
  // That said, we should consider making all services global and not tied to a
  // single adapter. The layering will make more sense once this FIDL impl is
  // provided by a specific bt-adapter device.
  FXL_DCHECK(services_.find(id) == services_.end());

  auto connection_error_cb = [self, id] {
    FXL_VLOG(1) << "Removing GATT service (id: " << id << ")";
    if (self)
      self->RemoveService(id);
  };

  services_[id] = std::make_unique<ServiceImpl>(
      this, id, ::btfidl::gatt::ServiceDelegatePtr::Create(std::move(delegate)),
      std::move(service_iface), adapter, connection_error_cb);

  callback(::btfidl::Status::New());
}

void GattServerFidlImpl::OnActiveAdapterChanged(
    ::btlib::gap::Adapter* adapter) {
  // This will close all services and notify their connection error handlers.
  // TODO(armansito): Make this remove services based on their adapter.
  services_.clear();
}

void GattServerFidlImpl::OnReadRequest(
    ::btlib::gatt::IdType service_id,
    ::btlib::gatt::IdType id,
    uint16_t offset,
    const ::btlib::gatt::ReadResponder& responder) {
  auto iter = services_.find(service_id);
  if (iter == services_.end()) {
    responder(::btlib::att::ErrorCode::kUnlikelyError,
              ::btlib::common::BufferView());
    return;
  }

  auto cb = [responder](::fidl::Array<uint8_t> value, auto error_code) {
    responder(GattErrorCodeFromFidl(error_code, true /* is_read */),
              ::btlib::common::BufferView(value.data(), value.size()));
  };

  auto* delegate = iter->second->delegate();
  FXL_DCHECK(delegate);
  delegate->OnReadValue(id, offset, cb);
}

void GattServerFidlImpl::OnWriteRequest(
    ::btlib::gatt::IdType service_id,
    ::btlib::gatt::IdType id,
    uint16_t offset,
    const ::btlib::common::ByteBuffer& value,
    const ::btlib::gatt::WriteResponder& responder) {
  auto iter = services_.find(service_id);
  if (iter == services_.end()) {
    responder(::btlib::att::ErrorCode::kUnlikelyError);
    return;
  }

  auto fidl_value = fidl::Array<uint8_t>::From(value);
  auto* delegate = iter->second->delegate();
  FXL_DCHECK(delegate);

  if (!responder) {
    delegate->OnWriteWithoutResponse(id, offset, std::move(fidl_value));
    return;
  }

  auto cb = [responder](auto error_code) {
    responder(GattErrorCodeFromFidl(error_code, false /* is_read */));
  };

  delegate->OnWriteValue(id, offset, std::move(fidl_value), cb);
}

void GattServerFidlImpl::OnCharacteristicConfig(
    ::btlib::gatt::IdType service_id,
    ::btlib::gatt::IdType chrc_id,
    const std::string& peer_id,
    bool notify,
    bool indicate) {
  auto iter = services_.find(service_id);
  if (iter != services_.end()) {
    auto* delegate = iter->second->delegate();
    FXL_DCHECK(delegate);
    delegate->OnCharacteristicConfiguration(chrc_id, peer_id, notify, indicate);
  }
}

}  // namespace bluetooth_service
