// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_parser.h"

#include "tools/fidlcat/lib/wire_types.h"

namespace fidlcat {

#if 0

// Here are some useful debug methods.

// Prints the content of the message (in hex) to fstream.
void PrintPayload(FILE* fstream, const fidl::Message& message) {
  uint8_t* payload = reinterpret_cast<uint8_t*>(message.bytes().data());
  uint32_t amt = message.bytes().actual();
  for (size_t i = 0; i < amt; i++) {
    fprintf(fstream, "b %p %5zu: 0x%x\n", payload + i, i, payload[i]);
  }
  if (message.handles().actual() != 0) {
    fprintf(fstream, "======\nhandles\n");
    zx_handle_t* handles =
        reinterpret_cast<zx_handle_t*>(message.handles().data());
    int i = 0;
    for (zx_handle_t* handle = handles; handle < message.handles().end();
         handle++) {
      fprintf(fstream, "h%5d: 0x%x (%d)\n", i++, *handle, *handle);
    }
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
  const fidl::HandlePart& handles = message.handles();

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

  // TODO: This should be exactly the same logic as we use for Struct.  Unite
  // them.
  Marker end(bytes.end(), handles.end());
  ObjectTracker tracker(end);
  Marker marker(bytes.begin(), handles.begin(), tracker.end());
  for (const InterfaceMethodParameter* param : params) {
    marker.AdvanceBytesTo(bytes.begin() + param->get_offset());
    if (!marker.is_valid()) {
      return marker.is_valid();
    }
    std::unique_ptr<Type> type = param->GetType();
    ValueGeneratingCallback value_callback;
    marker = type->GetValueCallback(marker, param->get_size(), &tracker,
                                    value_callback);
    if (!marker.is_valid()) {
      return marker.is_valid();
    }

    tracker.ObjectEnqueue(param->name(), std::move(value_callback), result,
                          result.GetAllocator());

    current_offset += param->get_size();
  }
  return tracker.RunCallbacksFrom(marker);
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

bool ResponseToJSON(const InterfaceMethod* method, const fidl::Message& message,
                    rapidjson::Document& response) {
  if (!method->response_params().has_value()) {
    return false;
  }

  const std::optional<std::vector<InterfaceMethodParameter>>& params =
      method->response_params();
  return ParamsToJSON(params, message, response);
}

}  // namespace fidlcat
