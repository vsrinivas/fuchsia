// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_parser.h"

#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

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
bool DecodeMessage(const Struct& str, const uint8_t* bytes, uint32_t num_bytes,
                   const zx_handle_info_t* handles, uint32_t num_handles,
                   std::unique_ptr<StructValue>* decoded_object, std::ostream& error_stream) {
  MessageDecoder decoder(bytes, num_bytes, handles, num_handles, error_stream);
  *decoded_object = decoder.DecodeMessage(str);
  return !decoder.HasError();
}

}  // anonymous namespace

bool DecodeRequest(const InterfaceMethod* method, const uint8_t* bytes, uint32_t num_bytes,
                   const zx_handle_info_t* handles, uint32_t num_handles,
                   std::unique_ptr<StructValue>* decoded_object, std::ostream& error_stream) {
  if (method->request() == nullptr) {
    return false;
  }
  return DecodeMessage(*method->request(), bytes, num_bytes, handles, num_handles, decoded_object,
                       error_stream);
}

bool DecodeResponse(const InterfaceMethod* method, const uint8_t* bytes, uint32_t num_bytes,
                    const zx_handle_info_t* handles, uint32_t num_handles,
                    std::unique_ptr<StructValue>* decoded_object, std::ostream& error_stream) {
  if (method->response() == nullptr) {
    return false;
  }
  return DecodeMessage(*method->response(), bytes, num_bytes, handles, num_handles, decoded_object,
                       error_stream);
}

}  // namespace fidl_codec
