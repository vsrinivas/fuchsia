// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_parser.h"

#include "garnet/bin/fidlcat/lib/wire_types.h"

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
bool ParamsToJSON(const std::optional<std::vector<InterfaceMethodParameter>>& p,
                  const fidl::Message& message, rapidjson::Document& result) {
  // TODO: Deal with what happens if p is nullopt
  result.SetObject();
  const fidl::BytePart& bytes = message.bytes();
  uint64_t current_offset = sizeof(message.header());

  // Go in order of offset.
  std::vector<const InterfaceMethodParameter*> params;
  for (size_t i = 0; i < p->size(); i++) {
    params.push_back(&p->at(i));
  }
  std::sort(
      params.begin(), params.end(),
      [](const InterfaceMethodParameter* l, const InterfaceMethodParameter* r) {
        return l->get_offset() < r->get_offset();
      });

  ObjectTracker tracker(bytes.data());
  for (const InterfaceMethodParameter* param : params) {
    current_offset = param->get_offset();

    std::unique_ptr<Type> type = param->GetType();
    ValueGeneratingCallback value_callback;
    type->GetValueCallback(bytes.data() + current_offset, param->get_size(),
                           &tracker, value_callback);

    tracker.ObjectEnqueue(param->name(), std::move(value_callback), result,
                          result.GetAllocator());

    current_offset += param->get_size();
  }
  tracker.RunCallbacksFrom(current_offset);
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

  return ParamsToJSON(params, message, request);
}

}  // namespace fidlcat
