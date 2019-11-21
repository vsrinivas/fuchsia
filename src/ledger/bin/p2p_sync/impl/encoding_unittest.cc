// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/encoding.h"

#include "gtest/gtest.h"
#include "message_generated.h"
#include "src/ledger/bin/p2p_sync/impl/flatbuffer_message_factory.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/macros.h"

namespace p2p_sync {
namespace {

TEST(ParseMessageTest, Valid_WatchStart) {
  flatbuffers::FlatBufferBuilder buffer;
  flatbuffers::Offset<WatchStartRequest> watch_start = CreateWatchStartRequest(buffer);
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(buffer, convert::ToFlatBufferVector(&buffer, "namespace_id"),
                            convert::ToFlatBufferVector(&buffer, "page_id"));
  flatbuffers::Offset<Request> request = CreateRequest(
      buffer, namespace_page_id, RequestMessage_WatchStartRequest, watch_start.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(buffer, MessageUnion_Request, request.Union());
  buffer.Finish(message);
  const Message* message_ptr = ParseMessage(convert::ToStringView(buffer));
  EXPECT_NE(message_ptr, nullptr);
}

TEST(ParseMessageTest, Invalid_NoNamespace) {
  flatbuffers::FlatBufferBuilder buffer;
  flatbuffers::Offset<WatchStartRequest> watch_start = CreateWatchStartRequest(buffer);
  flatbuffers::Offset<NamespacePageId> namespace_page_id;
  flatbuffers::Offset<Request> request = CreateRequest(
      buffer, namespace_page_id, RequestMessage_WatchStartRequest, watch_start.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(buffer, MessageUnion_Request, request.Union());
  buffer.Finish(message);
  const Message* message_ptr = ParseMessage(convert::ToStringView(buffer));
  EXPECT_EQ(message_ptr, nullptr);
}

TEST(ParseMessageTest, Valid_ObjectResponseNoObject) {
  flatbuffers::FlatBufferBuilder buffer;
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(buffer, convert::ToFlatBufferVector(&buffer, "namespace_id"),
                            convert::ToFlatBufferVector(&buffer, "page_id"));
  flatbuffers::Offset<ObjectId> fb_object_id =
      CreateObjectId(buffer, 1, convert::ToFlatBufferVector(&buffer, "digest"));
  std::vector<flatbuffers::Offset<Object>> fb_objects;
  fb_objects.emplace_back(CreateObject(buffer, fb_object_id, ObjectStatus_UNKNOWN_OBJECT));
  flatbuffers::Offset<ObjectResponse> object_response =
      CreateObjectResponse(buffer, buffer.CreateVector(fb_objects));
  flatbuffers::Offset<Response> response =
      CreateResponse(buffer, ResponseStatus_OK, namespace_page_id, ResponseMessage_ObjectResponse,
                     object_response.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(buffer, MessageUnion_Response, response.Union());
  buffer.Finish(message);

  const Message* message_ptr = ParseMessage(convert::ToStringView(buffer));
  EXPECT_NE(message_ptr, nullptr);
}

TEST(ParseMessageTest, Valid_ResponseUnknownNamespace) {
  flatbuffers::FlatBufferBuilder buffer;
  CreateUnknownResponseMessage(&buffer, "namespace", "page", ResponseStatus_UNKNOWN_NAMESPACE);

  const Message* message_ptr = ParseMessage(convert::ToStringView(buffer));
  EXPECT_NE(message_ptr, nullptr);
}

}  // namespace
}  // namespace p2p_sync
