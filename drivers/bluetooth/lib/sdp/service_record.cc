// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/service_record.h"

#include <set>

#include "garnet/public/lib/fxl/random/uuid.h"

namespace btlib {
namespace sdp {

namespace {

// Adds all UUIDs that it finds in |elem| to |out|, recursing through
// sequences and alternatives if necessary.
void AddAllUUIDs(const DataElement& elem,
                 std::unordered_set<common::UUID>* out) {
  DataElement::Type type = elem.type();
  if (type == DataElement::Type::kUuid) {
    out->emplace(*elem.Get<common::UUID>());
  } else if (type == DataElement::Type::kSequence ||
             type == DataElement::Type::kAlternative) {
    std::vector<DataElement> seq = *elem.Get<std::vector<DataElement>>();
    for (const auto& item : seq) {
      AddAllUUIDs(item, out);
    }
  }
}

}  // namespace

ServiceRecord::ServiceRecord(ServiceHandle handle) : handle_(handle) {
  SetAttribute(kServiceRecordHandle, DataElement(uint32_t(handle_)));

  common::UUID service_uuid;
  common::StringToUuid(fxl::GenerateUUID(), &service_uuid);
  SetAttribute(kServiceId, DataElement(service_uuid));
}

void ServiceRecord::SetAttribute(AttributeId id, DataElement value) {
  attributes_.erase(id);
  attributes_.emplace(id, std::move(value));
}

const DataElement& ServiceRecord::GetAttribute(AttributeId id) const {
  auto it = attributes_.find(id);
  FXL_DCHECK(it != attributes_.end()) << "Attribute " << id << " not set!";
  return it->second;
}

bool ServiceRecord::HasAttribute(AttributeId id) const {
  return attributes_.count(id) == 1;
}

void ServiceRecord::RemoveAttribute(AttributeId id) { attributes_.erase(id); }

DataElement ServiceRecord::GetAttributes(
    const std::unordered_set<AttributeId>& attributes) const {
  std::set<AttributeId> sorted_attributes(attributes.begin(), attributes.end());
  std::vector<DataElement> attr_seq;
  for (const auto& attr : sorted_attributes) {
    if (!HasAttribute(attr)) {
      continue;
    }
    attr_seq.emplace_back(DataElement(attr));
    attr_seq.emplace_back(GetAttribute(attr));
  }

  return DataElement(std::move(attr_seq));
}

bool ServiceRecord::FindUUID(
    const std::unordered_set<common::UUID>& uuids) const {
  if (uuids.size() == 0) {
    return true;
  }
  // Gather all the UUIDs in the attributes
  std::unordered_set<common::UUID> attribute_uuids;
  for (const auto& it : attributes_) {
    AddAllUUIDs(it.second, &attribute_uuids);
  }
  for (const auto& uuid : uuids) {
    if (attribute_uuids.count(uuid) == 0) {
      return false;
    }
  }
  return true;
}

void ServiceRecord::SetServiceClassUUIDs(
    const std::vector<common::UUID>& classes) {
  std::vector<DataElement> class_uuids;
  for (const auto& uuid : classes) {
    DataElement uuid_elem;
    uuid_elem.Set(uuid);
    class_uuids.emplace_back(std::move(uuid_elem));
  }
  DataElement class_uuids_elem;
  class_uuids_elem.Set(class_uuids);
  SetAttribute(kServiceClassIdList, class_uuids_elem);
}

void ServiceRecord::AddProtocolDescriptor(const ProtocolListId id,
                                          const common::UUID& uuid,
                                          DataElement params) {
  std::vector<DataElement> seq;
  if (id == kPrimaryProtocolList) {
    auto list_it = attributes_.find(kProtocolDescriptorList);
    if (list_it != attributes_.end()) {
      seq = *list_it->second.Get<std::vector<DataElement>>();
    }
  } else if (addl_protocols_.count(id)) {
    seq = *addl_protocols_[id].Get<std::vector<DataElement>>();
  }

  std::vector<DataElement> protocol_desc;
  protocol_desc.emplace_back(DataElement(uuid));
  if (params.type() == DataElement::Type::kSequence) {
    std::vector<DataElement> param_seq =
        *params.Get<std::vector<DataElement>>();
    protocol_desc.insert(protocol_desc.end(), param_seq.begin(),
                         param_seq.end());
  } else if (params.type() != DataElement::Type::kNull) {
    protocol_desc.emplace_back(params);
  }

  seq.emplace_back(DataElement(protocol_desc));

  DataElement protocol_list;
  protocol_list.Set(seq);

  if (id == kPrimaryProtocolList) {
    SetAttribute(kProtocolDescriptorList, DataElement(std::move(seq)));
  } else {
    addl_protocols_[id] = DataElement(std::move(seq));

    std::vector<DataElement> addl_protocol_seq;
    for (const auto& it : addl_protocols_) {
      addl_protocol_seq.emplace_back(it.second);
    }

    SetAttribute(kAdditionalProtocolDescriptorList,
                 DataElement(std::move(addl_protocol_seq)));
  }
}

void ServiceRecord::AddProfile(const common::UUID& uuid, uint8_t major,
                               uint8_t minor) {
  std::vector<DataElement> seq;
  auto list_it = attributes_.find(kBluetoothProfileDescriptorList);
  if (list_it != attributes_.end()) {
    seq = *list_it->second.Get<std::vector<DataElement>>();
  }

  std::vector<DataElement> profile_desc;
  profile_desc.emplace_back(DataElement(uuid));

  uint16_t profile_version = (major << 8) | minor;
  profile_desc.emplace_back(DataElement(profile_version));

  seq.emplace_back(DataElement(std::move(profile_desc)));

  attributes_[kBluetoothProfileDescriptorList] = DataElement(std::move(seq));
}

bool ServiceRecord::AddInfo(const std::string& language_code,
                            const std::string& name,
                            const std::string& description,
                            const std::string& provider) {
  if ((name.empty() && description.empty() && provider.empty()) ||
      (language_code.size() != 2)) {
    return false;
  }
  AttributeId base_attrid = 0x0100;
  std::vector<DataElement> base_attr_list;
  auto it = attributes_.find(kLanguageBaseAttributeIdList);
  if (it != attributes_.end()) {
    base_attr_list = *it->second.Get<std::vector<DataElement>>();
    FXL_DCHECK(base_attr_list.size() % 3 == 0);
    // 0x0100 is guaranteed to be taken, start counting from higher.
    base_attrid = 0x9000;
  }

  // Find the first base_attrid that's not taken
  while (HasAttribute(base_attrid + kServiceNameOffset) ||
         HasAttribute(base_attrid + kServiceDescriptionOffset) ||
         HasAttribute(base_attrid + kProviderNameOffset)) {
    base_attrid++;
    if (base_attrid == 0xFFFF) {
      return false;
    }
  }

  uint16_t lang_encoded = *((uint16_t*)(language_code.data()));
  base_attr_list.emplace_back(DataElement(lang_encoded));
  base_attr_list.emplace_back(DataElement(uint16_t(106)));  // UTF-8
  base_attr_list.emplace_back(DataElement(base_attrid));

  if (!name.empty()) {
    SetAttribute(base_attrid + kServiceNameOffset, DataElement(name));
  }
  if (!description.empty()) {
    SetAttribute(base_attrid + kServiceDescriptionOffset,
                 DataElement(description));
  }
  if (!provider.empty()) {
    SetAttribute(base_attrid + kProviderNameOffset, DataElement(provider));
  }

  SetAttribute(kLanguageBaseAttributeIdList,
               DataElement(std::move(base_attr_list)));
  return true;
}

}  // namespace sdp
}  // namespace btlib
