// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_sync/impl/page_communicator_impl.h"

#include <lib/async/cpp/task.h>

// gtest matchers are in gmock and we cannot include the specific header file
// directly as it is private to the library.
#include "gmock/gmock.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/gtest/test_with_loop.h"
#include "peridot/bin/ledger/p2p_sync/impl/device_mesh.h"
#include "peridot/bin/ledger/storage/fake/fake_object.h"
#include "peridot/bin/ledger/storage/testing/page_storage_empty_impl.h"
#include "peridot/lib/convert/convert.h"

namespace p2p_sync {
namespace {

class FakePageStorage : public storage::PageStorageEmptyImpl {
 public:
  explicit FakePageStorage(async_t* async, std::string page_id)
      : async_(async), page_id_(std::move(page_id)) {}
  ~FakePageStorage() override {}

  storage::PageId GetId() override { return page_id_; }

  void GetPiece(storage::ObjectIdentifier object_identifier,
                std::function<void(storage::Status,
                                   std::unique_ptr<const storage::Object>)>
                    callback) {
    async::PostTask(async_, [this, object_identifier, callback]() {
      const auto& it = objects_.find(object_identifier);
      if (it == objects_.end()) {
        callback(storage::Status::NOT_FOUND, nullptr);
        return;
      }
      callback(storage::Status::OK, std::make_unique<storage::fake::FakeObject>(
                                        object_identifier, it->second));
    });
  }

  void SetPiece(storage::ObjectIdentifier object_identifier,
                std::string contents) {
    objects_[object_identifier] = std::move(contents);
  }

 private:
  async_t* const async_;
  const std::string page_id_;
  std::map<storage::ObjectIdentifier, std::string> objects_;
};

class FakeDeviceMesh : public DeviceMesh {
 public:
  FakeDeviceMesh() {}
  ~FakeDeviceMesh() override {}

  const DeviceSet& GetDeviceList() override { return devices_; }

  void Send(fxl::StringView device_name, fxl::StringView data) override {
    messages_.emplace_back(
        std::forward_as_tuple(device_name.ToString(), data.ToString()));
  }

  DeviceSet devices_;
  std::vector<std::pair<std::string, std::string>> messages_;
};

void BuildWatchStartBuffer(flatbuffers::FlatBufferBuilder* buffer,
                           fxl::StringView namespace_id,
                           fxl::StringView page_id) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_WatchStartRequest);
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void BuildObjectRequestBuffer(
    flatbuffers::FlatBufferBuilder* buffer, fxl::StringView namespace_id,
    fxl::StringView page_id,
    std::vector<storage::ObjectIdentifier> object_ids) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  std::vector<flatbuffers::Offset<ObjectId>> fb_object_ids;
  for (const storage::ObjectIdentifier& object_id : object_ids) {
    fb_object_ids.emplace_back(CreateObjectId(
        *buffer, object_id.key_index, object_id.deletion_scope_id,
        convert::ToFlatBufferVector(buffer, object_id.object_digest)));
  }
  flatbuffers::Offset<ObjectRequest> object_request = CreateObjectRequest(
      *buffer, buffer->CreateVector(std::move(fb_object_ids)));
  flatbuffers::Offset<Request> fb_request =
      CreateRequest(*buffer, namespace_page_id, RequestMessage_ObjectRequest,
                    object_request.Union());
  flatbuffers::Offset<Message> fb_message =
      CreateMessage(*buffer, MessageUnion_Request, fb_request.Union());
  buffer->Finish(fb_message);
}

void BuildObjectResponseBuffer(
    flatbuffers::FlatBufferBuilder* buffer, fxl::StringView namespace_id,
    fxl::StringView page_id,
    std::map<storage::ObjectIdentifier, std::string> data) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  std::vector<flatbuffers::Offset<Object>> fb_objects;
  for (const auto& object_pair : data) {
    flatbuffers::Offset<ObjectId> fb_object_id = CreateObjectId(
        *buffer, object_pair.first.key_index,
        object_pair.first.deletion_scope_id,
        convert::ToFlatBufferVector(buffer, object_pair.first.object_digest));
    if (!object_pair.second.empty()) {
      flatbuffers::Offset<Data> fb_data = CreateData(
          *buffer, convert::ToFlatBufferVector(buffer, object_pair.second));
      fb_objects.emplace_back(
          CreateObject(*buffer, fb_object_id, ObjectStatus_OK, fb_data));
    } else {
      fb_objects.emplace_back(
          CreateObject(*buffer, fb_object_id, ObjectStatus_UNKNOWN_OBJECT));
    }
  }
  flatbuffers::Offset<ObjectResponse> object_response = CreateObjectResponse(
      *buffer, buffer->CreateVector(std::move(fb_objects)));
  flatbuffers::Offset<Response> response =
      CreateResponse(*buffer, ResponseStatus_OK, namespace_page_id,
                     ResponseMessage_ObjectResponse, object_response.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Response, response.Union());
  buffer->Finish(message);
}

class PageCommunicatorImplTest : public gtest::TestWithLoop {
 public:
  PageCommunicatorImplTest() {}
  ~PageCommunicatorImplTest() override {}

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageCommunicatorImplTest);
};

TEST_F(PageCommunicatorImplTest, ConnectToExistingMesh) {
  FakeDeviceMesh mesh;
  mesh.devices_.emplace("device2");
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&storage, &storage, "ledger", "page",
                                         &mesh);

  EXPECT_TRUE(mesh.messages_.empty());

  page_communicator.Start();

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  if (!VerifyMessageBuffer(verifier)) {
    // Wrong serialization, abort.
    FXL_LOG(ERROR) << "The message received is malformed.";
    return;
  };
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(MessageUnion_Request, message->message_type());
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ("ledger",
            convert::ExtendedStringView(namespace_page_id->namespace_id()));
  EXPECT_EQ("page", convert::ExtendedStringView(namespace_page_id->page_id()));
  EXPECT_EQ(RequestMessage_WatchStartRequest, request->request_type());
}

TEST_F(PageCommunicatorImplTest, ConnectToNewMeshParticipant) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  EXPECT_TRUE(mesh.messages_.empty());

  mesh.devices_.emplace("device2");
  page_communicator.OnDeviceChange("device2",
                                   p2p_provider::DeviceChangeType::NEW);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  if (!VerifyMessageBuffer(verifier)) {
    // Wrong serialization, abort.
    FXL_LOG(ERROR) << "The message received is malformed.";
    return;
  };
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(MessageUnion_Request, message->message_type());
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ("ledger",
            convert::ExtendedStringView(namespace_page_id->namespace_id()));
  EXPECT_EQ("page", convert::ExtendedStringView(namespace_page_id->page_id()));
  EXPECT_EQ(RequestMessage_WatchStartRequest, request->request_type());
}

TEST_F(PageCommunicatorImplTest, GetObject) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  const Message* new_device_message = GetMessage(buffer.GetBufferPointer());
  page_communicator.OnNewRequest(
      "device2", static_cast<const Request*>(new_device_message->message()));

  bool called;
  storage::Status status;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  // Verify the message sent to request the object.
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(MessageUnion_Request, message->message_type());
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ("ledger",
            convert::ExtendedStringView(namespace_page_id->namespace_id()));
  EXPECT_EQ("page", convert::ExtendedStringView(namespace_page_id->page_id()));
  EXPECT_EQ(RequestMessage_ObjectRequest, request->request_type());
  const ObjectRequest* object_request =
      static_cast<const ObjectRequest*>(request->request());
  EXPECT_EQ(1u, object_request->object_ids()->size());
  EXPECT_EQ(0u, object_request->object_ids()->begin()->key_index());
  EXPECT_EQ(0u, object_request->object_ids()->begin()->deletion_scope_id());
  EXPECT_EQ("foo", convert::ExtendedStringView(
                       object_request->object_ids()->begin()->digest()));
}

TEST_F(PageCommunicatorImplTest, ObjectRequest) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  storage.SetPiece(storage::ObjectIdentifier{0, 0, "object_digest"},
                   "some data");
  PageCommunicatorImpl page_communicator(&storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildObjectRequestBuffer(&request_buffer, "ledger", "page",
                           {storage::ObjectIdentifier{0, 0, "object_digest"},
                            storage::ObjectIdentifier{0, 0, "object_digest2"}});
  const Message* message = GetMessage(request_buffer.GetBufferPointer());
  page_communicator.OnNewRequest(
      "device2", static_cast<const Request*>(message->message()));

  RunLoopUntilIdle();

  // Verify the response.
  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  const Message* reply_message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(MessageUnion_Response, reply_message->message_type());
  const Response* response =
      static_cast<const Response*>(reply_message->message());
  const NamespacePageId* response_namespace_page_id =
      response->namespace_page();
  EXPECT_EQ("ledger", convert::ExtendedStringView(
                          response_namespace_page_id->namespace_id()));
  EXPECT_EQ("page",
            convert::ExtendedStringView(response_namespace_page_id->page_id()));
  EXPECT_EQ(ResponseMessage_ObjectResponse, response->response_type());
  const ObjectResponse* object_response =
      static_cast<const ObjectResponse*>(response->response());
  ASSERT_EQ(2u, object_response->objects()->size());
  auto it = object_response->objects()->begin();
  EXPECT_EQ("object_digest", convert::ExtendedStringView(it->id()->digest()));
  EXPECT_EQ(ObjectStatus_OK, it->status());
  EXPECT_EQ("some data", convert::ExtendedStringView(it->data()->bytes()));
  it++;
  EXPECT_EQ("object_digest2", convert::ExtendedStringView(it->id()->digest()));
  EXPECT_EQ(ObjectStatus_UNKNOWN_OBJECT, it->status());
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseSuccess) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  const Message* new_device_message = GetMessage(buffer.GetBufferPointer());
  page_communicator.OnNewRequest(
      "device2", static_cast<const Request*>(new_device_message->message()));

  bool called;
  storage::Status status;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(
      &response_buffer, "ledger", "page",
      {std::make_pair(storage::ObjectIdentifier{0, 0, "foo"}, "foo_data"),
       std::make_pair(storage::ObjectIdentifier{0, 0, "bar"}, "bar_data")});
  const Message* message = GetMessage(response_buffer.GetBufferPointer());
  page_communicator.OnNewResponse(
      "device2", static_cast<const Response*>(message->message()));

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ("foo_data", data->Get());
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseFail) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  const Message* new_device_message = GetMessage(buffer.GetBufferPointer());
  page_communicator.OnNewRequest(
      "device2", static_cast<const Request*>(new_device_message->message()));

  bool called;
  storage::Status status;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(
      &response_buffer, "ledger", "page",
      {std::make_pair(storage::ObjectIdentifier{0, 0, "foo"}, "")});
  const Message* message = GetMessage(response_buffer.GetBufferPointer());
  page_communicator.OnNewResponse(
      "device2", static_cast<const Response*>(message->message()));

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::NOT_FOUND, status);
  EXPECT_FALSE(data);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseMultiDeviceSuccess) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  const Message* new_device_message = GetMessage(buffer.GetBufferPointer());
  page_communicator.OnNewRequest(
      "device2", static_cast<const Request*>(new_device_message->message()));
  page_communicator.OnNewRequest(
      "device3", static_cast<const Request*>(new_device_message->message()));

  bool called;
  storage::Status status;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(2u, mesh.messages_.size());

  flatbuffers::FlatBufferBuilder response_buffer_1;
  BuildObjectResponseBuffer(
      &response_buffer_1, "ledger", "page",
      {std::make_pair(storage::ObjectIdentifier{0, 0, "foo"}, "")});
  const Message* message_1 = GetMessage(response_buffer_1.GetBufferPointer());
  page_communicator.OnNewResponse(
      "device2", static_cast<const Response*>(message_1->message()));
  EXPECT_FALSE(called);

  flatbuffers::FlatBufferBuilder response_buffer_2;
  BuildObjectResponseBuffer(
      &response_buffer_2, "ledger", "page",
      {std::make_pair(storage::ObjectIdentifier{0, 0, "foo"}, "foo_data")});
  const Message* message_2 = GetMessage(response_buffer_2.GetBufferPointer());
  page_communicator.OnNewResponse(
      "device3", static_cast<const Response*>(message_2->message()));

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ("foo_data", data->Get());
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseMultiDeviceFail) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  const Message* new_device_message = GetMessage(buffer.GetBufferPointer());
  page_communicator.OnNewRequest(
      "device2", static_cast<const Request*>(new_device_message->message()));
  page_communicator.OnNewRequest(
      "device3", static_cast<const Request*>(new_device_message->message()));

  bool called;
  storage::Status status;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(2u, mesh.messages_.size());

  flatbuffers::FlatBufferBuilder response_buffer_1;
  BuildObjectResponseBuffer(
      &response_buffer_1, "ledger", "page",
      {std::make_pair(storage::ObjectIdentifier{0, 0, "foo"}, "")});
  const Message* message_1 = GetMessage(response_buffer_1.GetBufferPointer());
  page_communicator.OnNewResponse(
      "device2", static_cast<const Response*>(message_1->message()));
  EXPECT_FALSE(called);

  flatbuffers::FlatBufferBuilder response_buffer_2;
  BuildObjectResponseBuffer(
      &response_buffer_2, "ledger", "page",
      {std::make_pair(storage::ObjectIdentifier{0, 0, "foo"}, "")});
  const Message* message_2 = GetMessage(response_buffer_2.GetBufferPointer());
  page_communicator.OnNewResponse(
      "device3", static_cast<const Response*>(message_2->message()));

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::NOT_FOUND, status);
  EXPECT_FALSE(data);
}

}  // namespace
}  // namespace p2p_sync
