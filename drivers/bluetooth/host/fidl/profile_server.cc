// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "profile_server.h"

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/sdp/status.h"

#include "helpers.h"

using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Status;

namespace fidlbredr = fuchsia::bluetooth::bredr;
using fidlbredr::DataElementType;
using fidlbredr::Profile;

namespace bthost {

namespace {

bool FidlToDataElement(const fidlbredr::DataElement& fidl,
                       btlib::sdp::DataElement* out) {
  ZX_DEBUG_ASSERT(out);
  switch (fidl.type) {
    case DataElementType::NOTHING:
      out->Set(nullptr);
      return true;
    case DataElementType::UNSIGNED_INTEGER: {
      if (!fidl.data.is_integer()) {
        return false;
      }
      if (fidl.size == 1) {
        out->Set(uint8_t(fidl.data.integer()));
      } else if (fidl.size == 2) {
        out->Set(uint16_t(fidl.data.integer()));
      } else if (fidl.size == 4) {
        out->Set(uint32_t(fidl.data.integer()));
      } else if (fidl.size == 8) {
        out->Set(uint64_t(fidl.data.integer()));
      } else {
        return false;
      }
      return true;
    }
    case DataElementType::SIGNED_INTEGER: {
      if (!fidl.data.is_integer()) {
        return false;
      }
      if (fidl.size == 1) {
        out->Set(int8_t(fidl.data.integer()));
      } else if (fidl.size == 2) {
        out->Set(int16_t(fidl.data.integer()));
      } else if (fidl.size == 4) {
        out->Set(int32_t(fidl.data.integer()));
      } else if (fidl.size == 8) {
        out->Set(int64_t(fidl.data.integer()));
      } else {
        return false;
      }
      return true;
    }
    case DataElementType::UUID: {
      if (!fidl.data.is_uuid()) {
        return false;
      }
      btlib::common::UUID uuid;
      bool success = StringToUuid(fidl.data.uuid(), &uuid);
      if (!success) {
        return false;
      }
      out->Set(uuid);
      return true;
    }
    case DataElementType::STRING: {
      if (!fidl.data.is_str()) {
        return false;
      }
      out->Set(*fidl.data.str());
      return true;
    }
    case DataElementType::BOOLEAN: {
      if (!fidl.data.is_b()) {
        return false;
      }
      out->Set(fidl.data.b());
      return true;
    }
    case DataElementType::SEQUENCE: {
      if (!fidl.data.is_sequence()) {
        return false;
      }
      bool success = true;
      std::vector<btlib::sdp::DataElement> elems;
      for (const auto& fidl_elem : *fidl.data.sequence()) {
        btlib::sdp::DataElement it;
        success = FidlToDataElement(*fidl_elem, &it);
        if (!success) {
          return false;
        }
        elems.emplace_back(std::move(it));
      }
      out->Set(std::move(elems));
      return true;
    }
    default:
      return false;
  }
}

void AddProtocolDescriptorList(
    btlib::sdp::ServiceRecord* rec,
    btlib::sdp::ServiceRecord::ProtocolListId id,
    const ::fidl::VectorPtr<fidlbredr::ProtocolDescriptor>& descriptor_list) {
  bt_log(SPEW, "profile_server", "ProtocolDescriptorList %d", id);
  for (auto& descriptor : *descriptor_list) {
    btlib::sdp::DataElement protocol_params;
    if (descriptor.params->size() > 1) {
      std::vector<btlib::sdp::DataElement> params;
      for (auto& fidl_param : *descriptor.params) {
        btlib::sdp::DataElement bt_param;
        FidlToDataElement(fidl_param, &bt_param);
        params.emplace_back(std::move(bt_param));
      }
      protocol_params.Set(std::move(params));
    } else if (descriptor.params->size() == 1) {
      FidlToDataElement(descriptor.params->front(), &protocol_params);
    }

    bt_log(SPEW, "profile_server", "%d : %s",
           fidl::ToUnderlying(descriptor.protocol),
           protocol_params.ToString().c_str());
    rec->AddProtocolDescriptor(
        id, btlib::common::UUID(static_cast<uint16_t>(descriptor.protocol)),
        std::move(protocol_params));
  }
}

}  // namespace

ProfileServer::ProfileServer(fxl::WeakPtr<::btlib::gap::Adapter> adapter,
                             fidl::InterfaceRequest<Profile> request)
    : AdapterServerBase(adapter, this, std::move(request)),
      weak_ptr_factory_(this) {}

ProfileServer::~ProfileServer() {
  // Unregister anything that we have registered.
  auto sdp = adapter()->sdp_server();
  for (const auto& it : registered_) {
    sdp->UnregisterService(it.second);
  }
}

void ProfileServer::AddService(fidlbredr::ServiceDefinition definition,
                               fidlbredr::SecurityLevel sec_level, bool devices,
                               AddServiceCallback callback) {
  // TODO: check that the service definition is valid for useful error messages

  auto sdp = adapter()->sdp_server();
  ZX_DEBUG_ASSERT(sdp);

  btlib::sdp::ServiceRecord rec;
  std::vector<btlib::common::UUID> classes;
  for (auto& uuid_str : *definition.service_class_uuids) {
    btlib::common::UUID uuid;
    bt_log(SPEW, "profile_server", "Setting Service Class UUID %s",
           uuid_str->c_str());
    bool success = btlib::common::StringToUuid(uuid_str, &uuid);
    ZX_DEBUG_ASSERT(success);
    classes.emplace_back(std::move(uuid));
  }

  rec.SetServiceClassUUIDs(classes);

  AddProtocolDescriptorList(&rec,
                            btlib::sdp::ServiceRecord::kPrimaryProtocolList,
                            definition.protocol_descriptors);

  size_t protocol_list_id = 1;
  for (const auto& descriptor_list :
       *definition.additional_protocol_descriptors) {
    AddProtocolDescriptorList(&rec, protocol_list_id, descriptor_list);
    protocol_list_id++;
  }

  for (const auto& profile : *definition.profile_descriptors) {
    bt_log(SPEW, "profile_server", "Adding Profile %#x v%d.%d",
           profile.profile_id, profile.major_version, profile.minor_version);
    rec.AddProfile(btlib::common::UUID(uint16_t(profile.profile_id)),
                   profile.major_version, profile.minor_version);
  }

  for (const auto& info : *definition.information) {
    bt_log(SPEW, "profile_server", "Adding Info (%s): (%s, %s, %s)",
           info.language->c_str(), info.name->c_str(),
           info.description->c_str(), info.provider->c_str());
    rec.AddInfo(info.language, info.name, info.description, info.provider);
  }

  for (const auto& attribute : *definition.additional_attributes) {
    btlib::sdp::DataElement elem;
    FidlToDataElement(attribute.element, &elem);
    bt_log(SPEW, "profile_server", "Adding attribute %#x : %s", attribute.id,
           elem.ToString().c_str());
    rec.SetAttribute(attribute.id, std::move(elem));
  }

  auto handle =
      sdp->RegisterService(std::move(rec), [](auto, auto, const auto&) {});

  if (!handle) {
    callback(fidl_helpers::NewFidlError(ErrorCode::INVALID_ARGUMENTS,
                                        "Service definition was not valid"),
             0);
    return;
  };

  registered_.emplace(handle, handle);

  callback(fidl_helpers::StatusToFidl(btlib::sdp::Status()), handle);
}

void ProfileServer::DisconnectClient(::fidl::StringPtr remote_device,
                                     uint64_t service_id) {
  // TODO: implement
}

void ProfileServer::RemoveService(uint64_t service_id) {
  auto it = registered_.find(service_id);
  if (it != registered_.end()) {
    auto server = adapter()->sdp_server();
    ZX_DEBUG_ASSERT(server);
    server->UnregisterService(it->second);
    registered_.erase(it);
  }
}

}  // namespace bthost
