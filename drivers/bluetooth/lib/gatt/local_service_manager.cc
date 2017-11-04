// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_service_manager.h"

#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"

#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace gatt {
namespace {

// Adds characteristic definition attributes to |grouping| for |chrc|.
void InsertCharacteristicAttributes(
    att::AttributeGrouping* grouping,
    const Characteristic& chrc,
    const att::Attribute::ReadHandler& read_handler,
    const att::Attribute::WriteHandler& write_handler) {
  FXL_DCHECK(grouping);
  FXL_DCHECK(!grouping->complete());
  FXL_DCHECK(read_handler);
  FXL_DCHECK(write_handler);

  // Characteristic Declaration (Vol 3, Part G, 3.3.1).
  auto* decl_attr = grouping->AddAttribute(
      types::kCharacteristicDeclaration,
      att::AccessRequirements(false, false, false),  // read (no security)
      att::AccessRequirements());                    // write (not allowed)
  FXL_DCHECK(decl_attr);

  // Characteristic Value Declaration (Vol 3, Part G, 3.3.2)
  auto* value_attr = grouping->AddAttribute(
      chrc.type(), chrc.read_permissions(), chrc.write_permissions());
  FXL_DCHECK(value_attr);

  value_attr->set_read_handler(read_handler);
  value_attr->set_write_handler(write_handler);

  size_t uuid_size = chrc.type().CompactSize(false /* allow_32bit */);
  FXL_DCHECK(uuid_size == 2 || uuid_size == 16);

  // The characteristic declaration value contains:
  // 1 octet: properties
  // 2 octets: value handle
  // 2 or 16 octets: UUID
  common::DynamicByteBuffer decl_value(3 + uuid_size);
  decl_value[0] = chrc.properties();
  decl_value[1] = static_cast<uint8_t>(value_attr->handle());
  decl_value[2] = static_cast<uint8_t>(value_attr->handle() >> 8);

  auto uuid_view = decl_value.mutable_view(3);
  chrc.type().ToBytes(&uuid_view, false /* allow_32bit */);
  decl_attr->SetValue(decl_value);
}

// Adds a characteristic descriptor declaration to |grouping| for |desc|.
void InsertDescriptorAttribute(
    att::AttributeGrouping* grouping,
    const Descriptor& desc,
    const att::Attribute::ReadHandler& read_handler,
    const att::Attribute::WriteHandler& write_handler) {
  FXL_DCHECK(grouping);
  FXL_DCHECK(!grouping->complete());
  FXL_DCHECK(read_handler);
  FXL_DCHECK(write_handler);

  // There is no special declaration attribute type for descriptors.
  auto* attr = grouping->AddAttribute(desc.type(), desc.read_permissions(),
                                      desc.write_permissions());
  FXL_DCHECK(attr);

  attr->set_read_handler(read_handler);
  attr->set_write_handler(write_handler);
}

// Returns false if the given service hierarchy contains repeating identifiers.
// Returns the number of attributes that will be in the service attribute group
// (exluding the service declaration) in |out_attrs|.
bool ValidateService(const Service& service, size_t* out_attr_count) {
  FXL_DCHECK(out_attr_count);

  size_t attr_count = 0u;
  std::unordered_set<IdType> ids;
  for (const auto& chrc_ptr : service.characteristics()) {
    if (ids.count(chrc_ptr->id()) != 0u) {
      FXL_VLOG(2) << "gatt: server: Repeated ID: " << chrc_ptr->id();
      return false;
    }

    ids.insert(chrc_ptr->id());

    // +1: Characteristic Declaration (Vol 3, Part G, 3.3.1)
    // +1: Characteristic Value Declaration (Vol 3, Part G, 3.3.2)
    attr_count += 2;

    for (const auto& desc_ptr : chrc_ptr->descriptors()) {
      if (ids.count(desc_ptr->id()) != 0u) {
        FXL_VLOG(2) << "gatt: server: Repeated ID: " << desc_ptr->id();
        return false;
      }

      // Reject descriptors with types that are internally managed by us.
      if (desc_ptr->type() == types::kCharacteristicExtProperties ||
          desc_ptr->type() == types::kClientCharacteristicConfig ||
          desc_ptr->type() == types::kServerCharacteristicConfig) {
        FXL_VLOG(2) << "gatt: server: Disallowed descriptor type: "
                    << desc_ptr->type().ToString();
        return false;
      }

      ids.insert(desc_ptr->id());

      // +1: Characteristic Descriptor Declaration (Vol 3, Part G, 3.3.3)
      attr_count++;
    }
  }

  *out_attr_count = attr_count;

  return true;
}

}  // namespace

class LocalServiceManager::ServiceData final {
 public:
  ServiceData(IdType id,
              att::AttributeGrouping* grouping,
              Service* service,
              ReadHandler&& read_handler,
              WriteHandler&& write_handler)
      : id_(id),
        read_handler_(std::forward<ReadHandler>(read_handler)),
        write_handler_(std::forward<WriteHandler>(write_handler)),
        weak_ptr_factory_(this) {
    FXL_DCHECK(read_handler_);
    FXL_DCHECK(write_handler_);
    FXL_DCHECK(grouping);

    start_handle_ = grouping->start_handle();

    // Sort characteristics by UUID size (see Vol 3, Part G, 3.3.1).
    auto chrcs = service->ReleaseCharacteristics();
    std::sort(chrcs.begin(), chrcs.end(),
              [](const auto& chrc_ptr1, const auto& chrc_ptr2) {
                return chrc_ptr1->type().CompactSize(false /* allow_32bit */) <
                       chrc_ptr2->type().CompactSize(false /* allow_32bit */);
              });
    for (auto& chrc : chrcs) {
      AddCharacteristic(grouping, std::move(chrc));
    }
  }

  inline IdType id() const { return id_; }
  inline att::Handle start_handle() const { return start_handle_; }

 private:
  void AddCharacteristic(att::AttributeGrouping* grouping,
                         CharacteristicPtr chrc) {
    // Set up the characteristic callbacks.
    // TODO(armansito): Consider tracking a transaction timeout here (NET-338).
    auto self = weak_ptr_factory_.GetWeakPtr();
    auto read_handler = [self, id = chrc->id(), props = chrc->properties()](
                            att::Handle handle, uint16_t offset,
                            const auto& result_cb) {
      if (!self) {
        result_cb(att::ErrorCode::kUnlikelyError, common::BufferView());
        return;
      }

      // ATT permissions checks passed if we got here; also check the
      // characteristic property.
      if (!(props & Property::kRead)) {
        // TODO(armansito): Return kRequestNotSupported?
        result_cb(att::ErrorCode::kReadNotPermitted, common::BufferView());
        return;
      }

      self->read_handler_(self->id_, id, offset, result_cb);
    };

    auto write_handler = [self, id = chrc->id(), props = chrc->properties()](
                             att::Handle handle, uint16_t offset,
                             const auto& value, const auto& result_cb) {
      if (!self) {
        if (result_cb)
          result_cb(att::ErrorCode::kUnlikelyError);
        return;
      }

      // If |result_cb| was provided, then this is a write request and the
      // characteristic must support the "write" procedure.
      if (result_cb && !(props & Property::kWrite)) {
        // TODO(armansito): Return kRequestNotSupported?
        result_cb(att::ErrorCode::kWriteNotPermitted);
        return;
      }

      if (!result_cb && !(props & Property::kWriteWithoutResponse))
        return;

      self->write_handler_(self->id_, id, offset, value, result_cb);
    };

    InsertCharacteristicAttributes(grouping, *chrc, read_handler,
                                   write_handler);

    // TODO(armansito): Inject a CEP descriptor if the characteristic has
    // extended properties.
    // TODO(armansito): Inject a CCC descriptor if the characteristic supports
    // notifications or indications.
    // TODO(armansito): Inject a SCC descriptor if the characteristic has the
    // broadcast property and if we ever support configured broadcasts.

    // Sort descriptors by UUID size. This is not required by the specification
    // but we do this to return as many descriptors as possible in a ATT Find
    // Information response.
    auto descs = chrc->ReleaseDescriptors();
    std::sort(descs.begin(), descs.end(),
              [](const auto& desc_ptr1, const auto& desc_ptr2) {
                return desc_ptr1->type().CompactSize(false /* allow_32bit */) <
                       desc_ptr2->type().CompactSize(false /* allow_32bit */);
              });
    for (auto& desc : descs) {
      AddDescriptor(grouping, std::move(desc));
    }
  }

  void AddDescriptor(att::AttributeGrouping* grouping, DescriptorPtr desc) {
    auto self = weak_ptr_factory_.GetWeakPtr();
    auto read_handler = [self, id = desc->id()](att::Handle handle,
                                                uint16_t offset,
                                                const auto& result_cb) {
      if (!self) {
        result_cb(att::ErrorCode::kUnlikelyError, common::BufferView());
        return;
      }

      self->read_handler_(self->id_, id, offset, result_cb);
    };

    auto write_handler = [self, id = desc->id()](
                             att::Handle handle, uint16_t offset,
                             const auto& value, const auto& result_cb) {
      // Descriptors cannot be written using the "write without response"
      // procedure.
      if (!result_cb)
        return;

      if (!self) {
        result_cb(att::ErrorCode::kUnlikelyError);
        return;
      }

      self->write_handler_(self->id_, id, offset, value, result_cb);
    };

    InsertDescriptorAttribute(grouping, *desc, read_handler, write_handler);
  }

  IdType id_;
  att::Handle start_handle_;
  ReadHandler read_handler_;
  WriteHandler write_handler_;

  fxl::WeakPtrFactory<ServiceData> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceData);
};

LocalServiceManager::LocalServiceManager()
    : db_(att::Database::Create()), next_service_id_(1ull) {
  FXL_DCHECK(db_);
}

LocalServiceManager::~LocalServiceManager() {}

IdType LocalServiceManager::RegisterService(ServicePtr service,
                                            ReadHandler read_handler,
                                            WriteHandler write_handler) {
  FXL_DCHECK(service);
  FXL_DCHECK(read_handler);
  FXL_DCHECK(write_handler);

  if (services_.find(next_service_id_) != services_.end()) {
    FXL_VLOG(2) << "gatt: server: Ran out of service IDs";
    return kInvalidId;
  }

  size_t attr_count;
  if (!ValidateService(*service, &attr_count))
    return kInvalidId;

  // GATT does not support 32-bit UUIDs.
  const common::BufferView service_decl_value =
      service->type().CompactView(false /* allow_32bit */);

  // TODO(armansito): Cluster services with 16-bit and 128-bit together inside
  // |db_| (Vol 3, Part G, 3.1).

  att::AttributeGrouping* grouping = db_->NewGrouping(
      service->primary() ? types::kPrimaryService : types::kSecondaryService,
      attr_count, service_decl_value);
  if (!grouping) {
    FXL_VLOG(1)
        << "gatt: server: Failed to allocate attribute grouping for service";
    return kInvalidId;
  }

  // Creating a ServiceData will populate the attribute grouping.
  auto service_data = std::make_unique<ServiceData>(
      next_service_id_, grouping, service.get(), std::move(read_handler),
      std::move(write_handler));
  FXL_DCHECK(grouping->complete());
  grouping->set_active(true);

  // TODO(armansito): Handle potential 64-bit unsigned overflow?
  IdType id = next_service_id_++;

  services_[id] = std::move(service_data);

  return id;
}

bool LocalServiceManager::UnregisterService(IdType service_id) {
  auto iter = services_.find(service_id);
  if (iter == services_.end())
    return false;

  // TODO(armansito): Trigger a "Service Changed" event with the removed handle
  // range.

  db_->RemoveGrouping(iter->second->start_handle());
  services_.erase(iter);

  return true;
}

}  // namespace gatt
}  // namespace btlib
