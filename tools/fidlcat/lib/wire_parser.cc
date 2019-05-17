// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_parser.h"

#include "tools/fidlcat/lib/wire_object.h"
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

// Takes a Message which holds either a request or a response and extracts a
// JSON object which represents the message. The format of the message is
// specified by str.
// Returns true on success, false on failure.
bool MessageToJSON(const Struct& str, const fidl::Message& message,
                   rapidjson::Document& result) {
  MessageDecoder decoder(message);
  std::unique_ptr<Object> object = decoder.DecodeMessage(str);
  object->ExtractJson(result.GetAllocator(), result);
  return !decoder.HasError();
}

}  // anonymous namespace

bool RequestToJSON(const InterfaceMethod* method, const fidl::Message& message,
                   rapidjson::Document& request) {
  if (method->request() == nullptr) {
    return false;
  }
  return MessageToJSON(*method->request(), message, request);
}

bool ResponseToJSON(const InterfaceMethod* method, const fidl::Message& message,
                    rapidjson::Document& response) {
  if (method->response() == nullptr) {
    return false;
  }
  return MessageToJSON(*method->response(), message, response);
}

}  // namespace fidlcat
