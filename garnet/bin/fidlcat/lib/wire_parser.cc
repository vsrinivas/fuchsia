// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_parser.h"

namespace fidlcat {

#if 0

// Here are some useful debug methods.

// Prints the content of the message (in hex) to fstream.
void PrintPayload(FILE* fstream, const fidl::Message& message) {
  uint8_t* payload = reinterpret_cast<uint8_t*>(message.payload().data());
  uint32_t amt = message.payload().actual();
  for (size_t i = 0; i < amt; i++) {
    fprintf(fstream, "+0x%x\n", payload[i]);
  }
}

// Prints |value| and associated metadata to fstream
void PrintMembers(FILE* fstream, const rapidjson::Value& value) {
  fprintf(fstream, "object = %d array = %d\n", value.IsObject(),
          value.IsArray());
  for (rapidjson::Value::ConstMemberIterator itr = value.MemberBegin();
       itr != value.MemberEnd(); ++itr) {
    fprintf(fstream, "Type of member %s %d\n", itr->name.GetString(),
            itr->value.GetType());
  }
}

#endif  // 0

namespace {

// Takes <request or response> parameters and converts them to JSON.
// |params| is the schema for those parameters.
// |size| is the size of those parameters
// |message| is the FIDL wire format representation of those parameters.
// |result| is where the resulting JSON is stored.
// Returns true on success, false on failure.
bool ParamsToJSON(
    const std::optional<std::vector<InterfaceMethodParameter>>& params,
    const std::optional<uint64_t>& size, const fidl::Message& message,
    rapidjson::Document& result) {
  result.SetObject();
  const fidl::BytePart& bytes = message.bytes();
  uint64_t current_offset = sizeof(message.header());

  // Iterate over the parameters in the order found in the wire format.
  while (current_offset < size) {
    const InterfaceMethodParameter* next;
    uint64_t next_offset = *(size);

    // Find the next parameter. Bad big-O value, but param lists are likely to
    // be too short to bother doing anything more clever.
    bool found_param = false;
    for (size_t i = 0; i < params->size(); i++) {
      if (params->at(i).get_offset() < next_offset &&
          params->at(i).get_offset() >= current_offset) {
        found_param = true;
        next = &(*params)[i];
        next_offset = next->get_offset();
        break;
      }
    }
    if (!found_param) {
      break;
    }
    current_offset = next_offset;

    rapidjson::Value key;
    key.SetString(next->name().c_str(), result.GetAllocator());

    rapidjson::Value value;
    Type type = next->GetType();

    type.MakeValue(bytes.data() + current_offset, next->get_size(), value,
                   result.GetAllocator());

    result.AddMember(key, value, result.GetAllocator());

    current_offset += next->get_size();
  }
  return true;
}

}  // anonymous namespace

bool RequestToJSON(const InterfaceMethod* method, const fidl::Message& message,
                   rapidjson::Document& request) {
  if (!method->request_params().has_value()) {
    return false;
  }
  const std::optional<std::vector<InterfaceMethodParameter>>& params =
      method->request_params();

  const std::optional<uint64_t> request_size = method->request_size();

  return ParamsToJSON(params, request_size, message, request);
}

}  // namespace fidlcat
