// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_sync/impl/page_communicator_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

// gtest matchers are in gmock and we cannot include the specific header file
// directly as it is private to the library.
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/gtest/test_loop_fixture.h>
#include "gmock/gmock.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/p2p_sync/impl/device_mesh.h"
#include "peridot/bin/ledger/storage/fake/fake_object.h"
#include "peridot/bin/ledger/storage/testing/commit_empty_impl.h"
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

  void GetHeadCommitIds(
      fit::function<void(storage::Status, std::vector<storage::CommitId>)>
          callback) override {
    callback(storage::Status::OK, {"commit_id"});
  }

  void GetPiece(storage::ObjectIdentifier object_identifier,
                fit::function<void(storage::Status,
                                   std::unique_ptr<const storage::Object>)>
                    callback) override {
    async::PostTask(async_, [this, object_identifier,
                             callback = std::move(callback)]() {
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
                std::string contents, bool is_synced = false) {
    objects_[object_identifier] = std::move(contents);
    if (is_synced) {
      synced_objects_.insert(std::move(object_identifier));
    }
  }

  void IsPieceSynced(
      storage::ObjectIdentifier object_identifier,
      fit::function<void(storage::Status, bool)> callback) override {
    async::PostTask(async_, [this, object_identifier,
                             callback = std::move(callback)]() {
      const auto& it = objects_.find(object_identifier);
      if (it == objects_.end()) {
        callback(storage::Status::NOT_FOUND, false);
        return;
      }
      callback(storage::Status::OK, synced_objects_.find(object_identifier) !=
                                        synced_objects_.end());
    });
  }

  void AddCommitsFromSync(
      std::vector<storage::PageStorage::CommitIdAndBytes> ids_and_bytes,
      const storage::ChangeSource /*source*/,
      fit::function<void(storage::Status)> callback) override {
    commits_from_sync_.emplace_back(
        std::piecewise_construct,
        std::forward_as_tuple(std::move(ids_and_bytes)),
        std::forward_as_tuple(std::move(callback)));
  }

  storage::Status AddCommitWatcher(storage::CommitWatcher* watcher) override {
    FXL_DCHECK(!watcher_);
    watcher_ = watcher;
    return storage::Status::OK;
  }

  storage::CommitWatcher* watcher_ = nullptr;
  std::vector<std::pair<std::vector<storage::PageStorage::CommitIdAndBytes>,
                        fit::function<void(storage::Status)>>>
      commits_from_sync_;

 private:
  async_t* const async_;
  const std::string page_id_;
  std::map<storage::ObjectIdentifier, std::string> objects_;
  std::set<storage::ObjectIdentifier> synced_objects_;
};

class FakeCommit : public storage::CommitEmptyImpl {
 public:
  FakeCommit(std::string id, std::string data)
      : id_(std::move(id)), data_(std::move(data)) {}

  const storage::CommitId& GetId() const override { return id_; }

  fxl::StringView GetStorageBytes() const override { return data_; }

  std::unique_ptr<storage::Commit> Clone() const override {
    return std::make_unique<FakeCommit>(id_, data_);
  }

 private:
  const std::string id_;
  const std::string data_;
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

void BuildWatchStopBuffer(flatbuffers::FlatBufferBuilder* buffer,
                          fxl::StringView namespace_id,
                          fxl::StringView page_id) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_WatchStopRequest);
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
  fb_object_ids.reserve(object_ids.size());
  for (const storage::ObjectIdentifier& object_id : object_ids) {
    fb_object_ids.emplace_back(CreateObjectId(
        *buffer, object_id.key_index, object_id.deletion_scope_id,
        convert::ToFlatBufferVector(buffer, object_id.object_digest)));
  }
  flatbuffers::Offset<ObjectRequest> object_request =
      CreateObjectRequest(*buffer, buffer->CreateVector(fb_object_ids));
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
    std::vector<std::tuple<storage::ObjectIdentifier, std::string, bool>>
        data) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  std::vector<flatbuffers::Offset<Object>> fb_objects;
  for (const auto& object_tuple : data) {
    const storage::ObjectIdentifier& object_identifier =
        std::get<0>(object_tuple);
    const std::string& data = std::get<1>(object_tuple);
    bool is_synced = std::get<2>(object_tuple);

    flatbuffers::Offset<ObjectId> fb_object_id = CreateObjectId(
        *buffer, object_identifier.key_index,
        object_identifier.deletion_scope_id,
        convert::ToFlatBufferVector(buffer, object_identifier.object_digest));
    if (!data.empty()) {
      flatbuffers::Offset<Data> fb_data =
          CreateData(*buffer, convert::ToFlatBufferVector(buffer, data));
      fb_objects.emplace_back(
          CreateObject(*buffer, fb_object_id, ObjectStatus_OK, fb_data,
                       is_synced ? ObjectSyncStatus_SYNCED_TO_CLOUD
                                 : ObjectSyncStatus_UNSYNCED));
    } else {
      fb_objects.emplace_back(
          CreateObject(*buffer, fb_object_id, ObjectStatus_UNKNOWN_OBJECT));
    }
  }
  flatbuffers::Offset<ObjectResponse> object_response =
      CreateObjectResponse(*buffer, buffer->CreateVector(fb_objects));
  flatbuffers::Offset<Response> response =
      CreateResponse(*buffer, ResponseStatus_OK, namespace_page_id,
                     ResponseMessage_ObjectResponse, object_response.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Response, response.Union());
  buffer->Finish(message);
}

class PageCommunicatorImplTest : public gtest::TestLoopFixture {
 public:
  PageCommunicatorImplTest() {}
  ~PageCommunicatorImplTest() override {}

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }

  coroutine::CoroutineServiceImpl coroutine_service_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageCommunicatorImplTest);
};

TEST_F(PageCommunicatorImplTest, ConnectToExistingMesh) {
  FakeDeviceMesh mesh;
  mesh.devices_.emplace("device2");
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);

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
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
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
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  MessageHolder<Message> new_device_message(convert::ToStringView(buffer),
                                            &GetMessage);
  page_communicator.OnNewRequest(
      "device2",
      new_device_message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &data));
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
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildObjectRequestBuffer(&request_buffer, "ledger", "page",
                           {storage::ObjectIdentifier{0, 0, "object_digest"},
                            storage::ObjectIdentifier{0, 0, "object_digest2"}});
  MessageHolder<Message> request_message(convert::ToStringView(request_buffer),
                                         &GetMessage);
  page_communicator.OnNewRequest(
      "device2",
      request_message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

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
  EXPECT_EQ(ObjectSyncStatus_UNSYNCED, it->sync_status());
  it++;
  EXPECT_EQ("object_digest2", convert::ExtendedStringView(it->id()->digest()));
  EXPECT_EQ(ObjectStatus_UNKNOWN_OBJECT, it->status());
}

TEST_F(PageCommunicatorImplTest, ObjectRequestSynced) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  storage.SetPiece(storage::ObjectIdentifier{0, 0, "object_digest"},
                   "some data", true);
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildObjectRequestBuffer(&request_buffer, "ledger", "page",
                           {storage::ObjectIdentifier{0, 0, "object_digest"}});
  MessageHolder<Message> request_message(convert::ToStringView(request_buffer),
                                         &GetMessage);
  page_communicator.OnNewRequest(
      "device2",
      request_message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

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
  ASSERT_EQ(1u, object_response->objects()->size());
  auto it = object_response->objects()->begin();
  EXPECT_EQ("object_digest", convert::ExtendedStringView(it->id()->digest()));
  EXPECT_EQ(ObjectStatus_OK, it->status());
  EXPECT_EQ("some data", convert::ExtendedStringView(it->data()->bytes()));
  EXPECT_EQ(ObjectSyncStatus_SYNCED_TO_CLOUD, it->sync_status());
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseSuccess) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  MessageHolder<Message> new_device_message(convert::ToStringView(buffer),
                                            &GetMessage);
  page_communicator.OnNewRequest(
      "device2",
      new_device_message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(
      &response_buffer, "ledger", "page",
      {std::make_tuple(storage::ObjectIdentifier{0, 0, "foo"}, "foo_data",
                       false),
       std::make_tuple(storage::ObjectIdentifier{0, 0, "bar"}, "bar_data",
                       false)});
  MessageHolder<Message> response_message(
      convert::ToStringView(response_buffer), &GetMessage);
  page_communicator.OnNewResponse(
      "device2",
      response_message.TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ("foo_data", data->Get());
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseFail) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  MessageHolder<Message> message(convert::ToStringView(buffer), &GetMessage);
  page_communicator.OnNewRequest(
      "device2", message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(
      &response_buffer, "ledger", "page",
      {std::make_tuple(storage::ObjectIdentifier{0, 0, "foo"}, "", false)});
  MessageHolder<Message> response_message(
      convert::ToStringView(response_buffer), &GetMessage);
  page_communicator.OnNewResponse(
      "device2",
      response_message.TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::NOT_FOUND, status);
  EXPECT_FALSE(data);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseMultiDeviceSuccess) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  MessageHolder<Message> message(convert::ToStringView(buffer), &GetMessage);
  page_communicator.OnNewRequest(
      "device2", message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));
  message = MessageHolder<Message>(convert::ToStringView(buffer), &GetMessage);
  page_communicator.OnNewRequest(
      "device3", message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(2u, mesh.messages_.size());

  flatbuffers::FlatBufferBuilder response_buffer_1;
  BuildObjectResponseBuffer(
      &response_buffer_1, "ledger", "page",
      {std::make_tuple(storage::ObjectIdentifier{0, 0, "foo"}, "", false)});
  MessageHolder<Message> message_1(convert::ToStringView(response_buffer_1),
                                   &GetMessage);
  page_communicator.OnNewResponse(
      "device2", message_1.TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));
  EXPECT_FALSE(called);

  flatbuffers::FlatBufferBuilder response_buffer_2;
  BuildObjectResponseBuffer(
      &response_buffer_2, "ledger", "page",
      {std::make_tuple(storage::ObjectIdentifier{0, 0, "foo"}, "foo_data",
                       false)});
  MessageHolder<Message> message_2(convert::ToStringView(response_buffer_2),
                                   &GetMessage);
  page_communicator.OnNewResponse(
      "device3", message_2.TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ("foo_data", data->Get());
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseMultiDeviceFail) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  MessageHolder<Message> message(convert::ToStringView(buffer), &GetMessage);
  page_communicator.OnNewRequest(
      "device2", message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));
  message = MessageHolder<Message>(convert::ToStringView(buffer), &GetMessage);
  page_communicator.OnNewRequest(
      "device3", message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo"},
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(2u, mesh.messages_.size());

  flatbuffers::FlatBufferBuilder response_buffer_1;
  BuildObjectResponseBuffer(
      &response_buffer_1, "ledger", "page",
      {std::make_tuple(storage::ObjectIdentifier{0, 0, "foo"}, "", false)});
  MessageHolder<Message> message_1(convert::ToStringView(response_buffer_1),
                                   &GetMessage);
  page_communicator.OnNewResponse(
      "device2", message_1.TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));
  EXPECT_FALSE(called);

  flatbuffers::FlatBufferBuilder response_buffer_2;
  BuildObjectResponseBuffer(
      &response_buffer_2, "ledger", "page",
      {std::make_tuple(storage::ObjectIdentifier{0, 0, "foo"}, "", false)});
  MessageHolder<Message> message_2(convert::ToStringView(response_buffer_2),
                                   &GetMessage);
  page_communicator.OnNewResponse(
      "device3", message_2.TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::NOT_FOUND, status);
  EXPECT_FALSE(data);
}

TEST_F(PageCommunicatorImplTest, CommitUpdate) {
  FakeDeviceMesh mesh;
  FakePageStorage storage_1(dispatcher(), "page");
  PageCommunicatorImpl page_communicator_1(&coroutine_service_, &storage_1,
                                           &storage_1, "ledger", "page", &mesh);
  page_communicator_1.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  MessageHolder<Message> message(convert::ToStringView(buffer), &GetMessage);
  page_communicator_1.OnNewRequest(
      "device2", message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));
  RunLoopUntilIdle();

  FakePageStorage storage_2(dispatcher(), "page");
  PageCommunicatorImpl page_communicator_2(&coroutine_service_, &storage_2,
                                           &storage_2, "ledger", "page", &mesh);
  page_communicator_2.Start();

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.emplace_back(std::make_unique<FakeCommit>("id 1", "data 1"));
  commits.emplace_back(std::make_unique<FakeCommit>("id 2", "data 2"));
  ASSERT_NE(nullptr, storage_1.watcher_);
  storage_1.watcher_->OnNewCommits(commits, storage::ChangeSource::CLOUD);

  RunLoopUntilIdle();
  // No new message is sent on commits from CLOUD.
  ASSERT_EQ(0u, mesh.messages_.size());

  storage_1.watcher_->OnNewCommits(commits, storage::ChangeSource::P2P);

  RunLoopUntilIdle();
  // No new message is sent on commits from P2P either.
  ASSERT_EQ(0u, mesh.messages_.size());

  storage_1.watcher_->OnNewCommits(commits, storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();

  // Local commit: a message is sent.
  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  MessageHolder<Message> reply_message(mesh.messages_[0].second, &GetMessage);
  ASSERT_EQ(MessageUnion_Response, reply_message->message_type());
  MessageHolder<Response> response =
      reply_message.TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      });
  const NamespacePageId* response_namespace_page_id =
      response->namespace_page();
  EXPECT_EQ("ledger", convert::ExtendedStringView(
                          response_namespace_page_id->namespace_id()));
  EXPECT_EQ("page",
            convert::ExtendedStringView(response_namespace_page_id->page_id()));
  EXPECT_EQ(ResponseMessage_CommitResponse, response->response_type());

  // Send it to the other side.
  page_communicator_2.OnNewResponse("device1", std::move(response));
  RunLoopUntilIdle();

  // The other side's storage has the commit.
  ASSERT_EQ(1u, storage_2.commits_from_sync_.size());
  ASSERT_EQ(2u, storage_2.commits_from_sync_[0].first.size());
  EXPECT_EQ("id 1", storage_2.commits_from_sync_[0].first[0].id);
  EXPECT_EQ("data 1", storage_2.commits_from_sync_[0].first[0].bytes);
  EXPECT_EQ("id 2", storage_2.commits_from_sync_[0].first[1].id);
  EXPECT_EQ("data 2", storage_2.commits_from_sync_[0].first[1].bytes);

  // Verify we don't crash on response from storage
  storage_2.commits_from_sync_[0].second(storage::Status::OK);
  RunLoopUntilIdle();
}

TEST_F(PageCommunicatorImplTest, GetObjectDisconnect) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  MessageHolder<Message> message(convert::ToStringView(buffer), &GetMessage);
  page_communicator.OnNewRequest(
      "device2", message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  bool called1, called2, called3, called4;
  storage::Status status1, status2, status3, status4;
  storage::ChangeSource source1, source2, source3, source4;
  std::unique_ptr<storage::DataSource::DataChunk> data1, data2, data3, data4;
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo1"},
      callback::Capture(callback::SetWhenCalled(&called1), &status1, &source1,
                        &data1));
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo2"},
      callback::Capture(callback::SetWhenCalled(&called2), &status2, &source2,
                        &data2));
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo3"},
      callback::Capture(callback::SetWhenCalled(&called3), &status3, &source3,
                        &data3));
  page_communicator.GetObject(
      storage::ObjectIdentifier{0, 0, "foo4"},
      callback::Capture(callback::SetWhenCalled(&called4), &status4, &source4,
                        &data4));
  RunLoopUntilIdle();
  EXPECT_FALSE(called1);
  EXPECT_FALSE(called2);
  EXPECT_FALSE(called3);
  EXPECT_FALSE(called4);
  EXPECT_EQ(4u, mesh.messages_.size());

  flatbuffers::FlatBufferBuilder stop_buffer;
  BuildWatchStopBuffer(&stop_buffer, "ledger", "page");
  MessageHolder<Message> watch_stop_message(convert::ToStringView(stop_buffer),
                                            &GetMessage);
  page_communicator.OnNewRequest(
      "device2",
      watch_stop_message.TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));
  RunLoopUntilIdle();

  // All requests are terminated with a not found status.
  EXPECT_TRUE(called1);
  EXPECT_EQ(storage::Status::NOT_FOUND, status1);
  EXPECT_EQ(storage::ChangeSource::P2P, source1);
  EXPECT_FALSE(data1);

  EXPECT_TRUE(called2);
  EXPECT_EQ(storage::Status::NOT_FOUND, status2);
  EXPECT_EQ(storage::ChangeSource::P2P, source2);
  EXPECT_FALSE(data2);

  EXPECT_TRUE(called3);
  EXPECT_EQ(storage::Status::NOT_FOUND, status3);
  EXPECT_EQ(storage::ChangeSource::P2P, source3);
  EXPECT_FALSE(data3);

  EXPECT_TRUE(called4);
  EXPECT_EQ(storage::Status::NOT_FOUND, status4);
  EXPECT_EQ(storage::ChangeSource::P2P, source4);
  EXPECT_FALSE(data4);
}

}  // namespace
}  // namespace p2p_sync
