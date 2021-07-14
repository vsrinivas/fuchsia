// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/partition.h"

#include <sstream>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {

bool Partition::LessThan::operator()(const Partition& lhs, const Partition& rhs) const {
  return std::tie(lhs.volume().name, lhs.volume().instance) <
         std::tie(rhs.volume().name, rhs.volume().instance);
}

fpromise::result<Partition, std::string> Partition::Create(std::string_view serialized_volume_image,
                                                           std::unique_ptr<Reader> reader) {
  rapidjson::Document document;
  rapidjson::ParseResult result =
      document.Parse(reinterpret_cast<const char*>(serialized_volume_image.data()),
                     serialized_volume_image.size());

  if (result.IsError()) {
    std::ostringstream error;
    error << "Error parsing serialized VolumeDescriptor. "
          << rapidjson::GetParseError_En(result.Code()) << std::endl;
    return fpromise::error(error.str());
  }

  if (!document.HasMember("volume")) {
    return fpromise::error("volume_image missing volume_descriptor field 'volume'.");
  }

  if (!document.HasMember("address")) {
    return fpromise::error("volume_image missing address_descriptor field 'address'.");
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document["volume"].Accept(writer);
  auto volume_descriptor_result =
      VolumeDescriptor::Deserialize(std::string_view(buffer.GetString()));
  if (volume_descriptor_result.is_error()) {
    return volume_descriptor_result.take_error_result();
  }

  buffer.Clear();
  writer.Reset(buffer);
  document["address"].Accept(writer);
  auto address_descriptor_result =
      AddressDescriptor::Deserialize(std::string_view(buffer.GetString()));
  if (address_descriptor_result.is_error()) {
    return address_descriptor_result.take_error_result();
  }

  return fpromise::ok(Partition(volume_descriptor_result.take_value(),
                                address_descriptor_result.take_value(), std::move(reader)));
}

}  // namespace storage::volume_image
