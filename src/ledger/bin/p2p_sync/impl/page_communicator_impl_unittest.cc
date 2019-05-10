// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/page_communicator_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

// gtest matchers are in gmock and we cannot include the specific header file
// directly as it is private to the library.
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/p2p_sync/impl/device_mesh.h"
#include "src/ledger/bin/p2p_sync/impl/encoding.h"
#include "src/ledger/bin/p2p_sync/impl/message_generated.h"
#include "src/ledger/bin/storage/fake/fake_object.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"

using testing::IsEmpty;

namespace p2p_sync {
namespace {

// Creates a dummy object identifier.
// |object_digest| need not be valid (wrt. internal storage constraints) as it
// is only used as an opaque identifier for p2p.
storage::ObjectIdentifier MakeObjectIdentifier(std::string object_digest) {
  return storage::ObjectIdentifier(
      0, 0, storage::ObjectDigest(std::move(object_digest)));
}

class FakeCommit : public storage::CommitEmptyImpl {
 public:
  FakeCommit(std::string id, std::string data,
             std::vector<storage::CommitId> parents = {})
      : id_(std::move(id)),
        data_(std::move(data)),
        parents_(std::move(parents)) {}

  const storage::CommitId& GetId() const override { return id_; }

  std::vector<storage::CommitIdView> GetParentIds() const override {
    std::vector<storage::CommitIdView> parent_ids;
    for (const storage::CommitId& id : parents_) {
      parent_ids.emplace_back(id);
    }
    return parent_ids;
  }

  fxl::StringView GetStorageBytes() const override { return data_; }

  std::unique_ptr<const storage::Commit> Clone() const override {
    return std::make_unique<FakeCommit>(id_, data_);
  }

 private:
  const std::string id_;
  const std::string data_;
  const std::vector<storage::CommitId> parents_;
};

class FakePageStorage : public storage::PageStorageEmptyImpl {
 public:
  explicit FakePageStorage(async_dispatcher_t* dispatcher, std::string page_id)
      : dispatcher_(dispatcher), page_id_(std::move(page_id)) {}
  ~FakePageStorage() override {}

  storage::PageId GetId() override { return page_id_; }

  ledger::Status GetHeadCommits(
      std::vector<std::unique_ptr<const storage::Commit>>* head_commits)
      override {
    *head_commits = std::vector<std::unique_ptr<const storage::Commit>>();
    head_commits->push_back(
        std::make_unique<const FakeCommit>("commit_id", "data"));
    return ledger::Status::OK;
  }

  const FakeCommit& AddCommit(std::string id, std::string data) {
    auto commit =
        commits_.emplace(std::piecewise_construct, std::forward_as_tuple(id),
                         std::forward_as_tuple(id, std::move(data)));
    return commit.first->second;
  }

  void GetCommit(storage::CommitIdView commit_id,
                 fit::function<void(ledger::Status,
                                    std::unique_ptr<const storage::Commit>)>
                     callback) override {
    auto it = commits_.find(commit_id);
    if (it == commits_.end()) {
      callback(ledger::Status::INTERNAL_NOT_FOUND, nullptr);
      return;
    }
    callback(ledger::Status::OK, it->second.Clone());
  }

  void GetPiece(
      storage::ObjectIdentifier object_identifier,
      fit::function<void(ledger::Status, std::unique_ptr<const storage::Piece>)>
          callback) override {
    async::PostTask(dispatcher_, [this, object_identifier,
                                  callback = std::move(callback)]() {
      const auto& it = objects_.find(object_identifier);
      if (it == objects_.end()) {
        callback(ledger::Status::INTERNAL_NOT_FOUND, nullptr);
        return;
      }
      callback(ledger::Status::OK, std::make_unique<storage::fake::FakePiece>(
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
      fit::function<void(ledger::Status, bool)> callback) override {
    async::PostTask(dispatcher_, [this, object_identifier,
                                  callback = std::move(callback)]() {
      const auto& it = objects_.find(object_identifier);
      if (it == objects_.end()) {
        callback(ledger::Status::INTERNAL_NOT_FOUND, false);
        return;
      }
      callback(ledger::Status::OK, synced_objects_.find(object_identifier) !=
                                       synced_objects_.end());
    });
  }

  void AddCommitsFromSync(
      std::vector<storage::PageStorage::CommitIdAndBytes> ids_and_bytes,
      const storage::ChangeSource /*source*/,
      fit::function<void(ledger::Status, std::vector<storage::CommitId>)>
          callback) override {
    commits_from_sync_.emplace_back(
        std::piecewise_construct,
        std::forward_as_tuple(std::move(ids_and_bytes)),
        std::forward_as_tuple(std::move(callback)));
  }

  void AddCommitWatcher(storage::CommitWatcher* watcher) override {
    FXL_DCHECK(!watcher_);
    watcher_ = watcher;
  }

  void MarkSyncedToPeer(fit::function<void(ledger::Status)> callback) override {
    callback(mark_synced_to_peer_status);
  }

  storage::CommitWatcher* watcher_ = nullptr;
  std::vector<std::pair<
      std::vector<storage::PageStorage::CommitIdAndBytes>,
      fit::function<void(ledger::Status, std::vector<storage::CommitId>)>>>
      commits_from_sync_;
  ledger::Status mark_synced_to_peer_status = ledger::Status::OK;

 private:
  async_dispatcher_t* const dispatcher_;
  const std::string page_id_;
  std::map<storage::ObjectIdentifier, std::string> objects_;
  std::set<storage::ObjectIdentifier> synced_objects_;
  std::map<storage::CommitId, FakeCommit, convert::StringViewComparator>
      commits_;
};

class FakeDeviceMesh : public DeviceMesh {
 public:
  FakeDeviceMesh() {}
  ~FakeDeviceMesh() override {}

  void OnNextSend(std::string device_name, fit::closure callback) {
    callbacks_[device_name] = std::move(callback);
  }

  DeviceSet GetDeviceList() override { return devices_; }

  void Send(fxl::StringView device_name,
            convert::ExtendedStringView data) override {
    messages_.emplace_back(
        std::forward_as_tuple(device_name.ToString(), data.ToString()));
    auto it = callbacks_.find(device_name);
    if (it != callbacks_.end()) {
      it->second();
      callbacks_.erase(it);
    }
  }

  DeviceSet devices_;
  std::vector<std::pair<std::string, std::string>> messages_;
  std::map<std::string, fit::closure, convert::StringViewComparator> callbacks_;
};

void BuildWatchStartBuffer(flatbuffers::FlatBufferBuilder* buffer,
                           fxl::StringView namespace_id,
                           fxl::StringView page_id) {
  flatbuffers::Offset<WatchStartRequest> watch_start =
      CreateWatchStartRequest(*buffer);
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  flatbuffers::Offset<Request> request =
      CreateRequest(*buffer, namespace_page_id,
                    RequestMessage_WatchStartRequest, watch_start.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void BuildWatchStopBuffer(flatbuffers::FlatBufferBuilder* buffer,
                          fxl::StringView namespace_id,
                          fxl::StringView page_id) {
  flatbuffers::Offset<WatchStopRequest> watch_stop =
      CreateWatchStopRequest(*buffer);
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  flatbuffers::Offset<Request> request =
      CreateRequest(*buffer, namespace_page_id, RequestMessage_WatchStopRequest,
                    watch_stop.Union());
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
        *buffer, object_id.key_index(), object_id.deletion_scope_id(),
        convert::ToFlatBufferVector(buffer,
                                    object_id.object_digest().Serialize())));
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
        *buffer, object_identifier.key_index(),
        object_identifier.deletion_scope_id(),
        convert::ToFlatBufferVector(
            buffer, object_identifier.object_digest().Serialize()));
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

void BuildCommitRequestBuffer(flatbuffers::FlatBufferBuilder* buffer,
                              fxl::StringView namespace_id,
                              fxl::StringView page_id,
                              std::vector<storage::CommitId> commit_ids) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  std::vector<flatbuffers::Offset<CommitId>> fb_commit_ids;
  fb_commit_ids.reserve(commit_ids.size());
  for (const storage::CommitId& commit_id : commit_ids) {
    fb_commit_ids.emplace_back(CreateCommitId(
        *buffer, convert::ToFlatBufferVector(buffer, commit_id)));
  }
  flatbuffers::Offset<CommitRequest> commit_request =
      CreateCommitRequest(*buffer, buffer->CreateVector(fb_commit_ids));
  flatbuffers::Offset<Request> fb_request =
      CreateRequest(*buffer, namespace_page_id, RequestMessage_CommitRequest,
                    commit_request.Union());
  flatbuffers::Offset<Message> fb_message =
      CreateMessage(*buffer, MessageUnion_Request, fb_request.Union());
  buffer->Finish(fb_message);
}

void ConnectToDevice(PageCommunicatorImpl* page_communicator,
                     fxl::StringView device, fxl::StringView ledger,
                     fxl::StringView page) {
  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, ledger, page);
  MessageHolder<Message> message = *CreateMessageHolder<Message>(
      convert::ToStringView(buffer), &ParseMessage);
  page_communicator->OnNewRequest(
      device,
      std::move(message).TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));
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

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &is_object_synced, &data));
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

TEST_F(PageCommunicatorImplTest, DontGetObjectsIfMarkPageSyncedToPeerFailed) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  // If storage fails to mark the page as synced to a peer, the mesh should not
  // be updated.
  storage.mark_synced_to_peer_status = ledger::Status::IO_ERROR;
  ConnectToDevice(&page_communicator, "device2", "ledger", "page");

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_THAT(mesh.messages_, IsEmpty());
}

TEST_F(PageCommunicatorImplTest, ObjectRequest) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  storage.SetPiece(MakeObjectIdentifier("object_digest"), "some data");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildObjectRequestBuffer(&request_buffer, "ledger", "page",
                           {MakeObjectIdentifier("object_digest"),
                            MakeObjectIdentifier("object_digest2")});
  MessageHolder<Message> request_message = *CreateMessageHolder<Message>(
      convert::ToStringView(request_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      "device2", std::move(request_message)
                     .TakeAndMap<Request>([](const Message* message) {
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
  storage.SetPiece(MakeObjectIdentifier("object_digest"), "some data", true);
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildObjectRequestBuffer(&request_buffer, "ledger", "page",
                           {MakeObjectIdentifier("object_digest")});
  MessageHolder<Message> request_message = *CreateMessageHolder<Message>(
      convert::ToStringView(request_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      "device2", std::move(request_message)
                     .TakeAndMap<Request>([](const Message* message) {
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

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(
      &response_buffer, "ledger", "page",
      {std::make_tuple(MakeObjectIdentifier("foo"), "foo_data", false),
       std::make_tuple(MakeObjectIdentifier("bar"), "bar_data", false)});
  MessageHolder<Message> response_message = *CreateMessageHolder<Message>(
      convert::ToStringView(response_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      "device2", std::move(response_message)
                     .TakeAndMap<Response>([](const Message* message) {
                       return static_cast<const Response*>(message->message());
                     }));

  EXPECT_TRUE(called);
  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ("foo_data", data->Get());
  EXPECT_EQ(storage::IsObjectSynced::NO, is_object_synced);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseSynced) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(
      &response_buffer, "ledger", "page",
      {std::make_tuple(MakeObjectIdentifier("foo"), "foo_data", true)});
  MessageHolder<Message> response_message = *CreateMessageHolder<Message>(
      convert::ToStringView(response_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      "device2", std::move(response_message)
                     .TakeAndMap<Response>([](const Message* message) {
                       return static_cast<const Response*>(message->message());
                     }));

  EXPECT_TRUE(called);
  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ("foo_data", data->Get());
  EXPECT_EQ(storage::IsObjectSynced::YES, is_object_synced);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseFail) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(
      &response_buffer, "ledger", "page",
      {std::make_tuple(MakeObjectIdentifier("foo"), "", false)});
  MessageHolder<Message> response_message = *CreateMessageHolder<Message>(
      convert::ToStringView(response_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      "device2", std::move(response_message)
                     .TakeAndMap<Response>([](const Message* message) {
                       return static_cast<const Response*>(message->message());
                     }));

  EXPECT_TRUE(called);
  EXPECT_EQ(ledger::Status::INTERNAL_NOT_FOUND, status);
  EXPECT_FALSE(data);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseMultiDeviceSuccess) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");
  ConnectToDevice(&page_communicator, "device3", "ledger", "page");

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(2u, mesh.messages_.size());

  flatbuffers::FlatBufferBuilder response_buffer_1;
  BuildObjectResponseBuffer(
      &response_buffer_1, "ledger", "page",
      {std::make_tuple(MakeObjectIdentifier("foo"), "", false)});
  MessageHolder<Message> message_1 = *CreateMessageHolder<Message>(
      convert::ToStringView(response_buffer_1), &ParseMessage);
  page_communicator.OnNewResponse(
      "device2",
      std::move(message_1).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));
  EXPECT_FALSE(called);

  flatbuffers::FlatBufferBuilder response_buffer_2;
  BuildObjectResponseBuffer(
      &response_buffer_2, "ledger", "page",
      {std::make_tuple(MakeObjectIdentifier("foo"), "foo_data", false)});
  MessageHolder<Message> message_2 = *CreateMessageHolder<Message>(
      convert::ToStringView(response_buffer_2), &ParseMessage);
  page_communicator.OnNewResponse(
      "device3",
      std::move(message_2).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(ledger::Status::OK, status);
  EXPECT_EQ("foo_data", data->Get());
  EXPECT_EQ(storage::ChangeSource::P2P, source);
  EXPECT_EQ(storage::IsObjectSynced::NO, is_object_synced);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseMultiDeviceFail) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");
  ConnectToDevice(&page_communicator, "device3", "ledger", "page");

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(2u, mesh.messages_.size());

  flatbuffers::FlatBufferBuilder response_buffer_1;
  BuildObjectResponseBuffer(
      &response_buffer_1, "ledger", "page",
      {std::make_tuple(MakeObjectIdentifier("foo"), "", false)});
  MessageHolder<Message> message_1 = *CreateMessageHolder<Message>(
      convert::ToStringView(response_buffer_1), &ParseMessage);
  page_communicator.OnNewResponse(
      "device2",
      std::move(message_1).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));
  EXPECT_FALSE(called);

  flatbuffers::FlatBufferBuilder response_buffer_2;
  BuildObjectResponseBuffer(
      &response_buffer_2, "ledger", "page",
      {std::make_tuple(MakeObjectIdentifier("foo"), "", false)});
  MessageHolder<Message> message_2 = *CreateMessageHolder<Message>(
      convert::ToStringView(response_buffer_2), &ParseMessage);
  page_communicator.OnNewResponse(
      "device3",
      std::move(message_2).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(ledger::Status::INTERNAL_NOT_FOUND, status);
  EXPECT_FALSE(data);
}

TEST_F(PageCommunicatorImplTest, GetObjectMultipleCalls) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, "ledger", "page");
  MessageHolder<Message> new_device_message = *CreateMessageHolder<Message>(
      convert::ToStringView(buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      "device2", std::move(new_device_message)
                     .TakeAndMap<Request>([](const Message* message) {
                       return static_cast<const Request*>(message->message());
                     }));

  bool called1, called2;
  storage::Status status1, status2;
  storage::ChangeSource source1, source2;
  storage::IsObjectSynced is_object_synced1, is_object_synced2;
  std::unique_ptr<storage::DataSource::DataChunk> data1, data2;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called1), &status1, &source1,
                        &is_object_synced1, &data1));
  RunLoopUntilIdle();
  EXPECT_FALSE(called1);

  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  page_communicator.GetObject(
      MakeObjectIdentifier("foo"),
      callback::Capture(callback::SetWhenCalled(&called2), &status2, &source2,
                        &is_object_synced2, &data2));
  RunLoopUntilIdle();
  EXPECT_FALSE(called2);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(
      &response_buffer, "ledger", "page",
      {std::make_tuple(MakeObjectIdentifier("foo"), "foo_data", true)});
  MessageHolder<Message> response_message = *CreateMessageHolder<Message>(
      convert::ToStringView(response_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      "device2", std::move(response_message)
                     .TakeAndMap<Response>([](const Message* message) {
                       return static_cast<const Response*>(message->message());
                     }));

  EXPECT_TRUE(called1);
  EXPECT_TRUE(called2);
  EXPECT_EQ(storage::Status::OK, status1);
  EXPECT_EQ(storage::Status::OK, status2);
  EXPECT_EQ("foo_data", data1->Get());
  EXPECT_EQ("foo_data", data2->Get());
  EXPECT_EQ(storage::IsObjectSynced::YES, is_object_synced1);
  EXPECT_EQ(storage::IsObjectSynced::YES, is_object_synced2);
}

TEST_F(PageCommunicatorImplTest, CommitUpdate) {
  FakeDeviceMesh mesh;
  FakePageStorage storage_1(dispatcher(), "page");
  PageCommunicatorImpl page_communicator_1(&coroutine_service_, &storage_1,
                                           &storage_1, "ledger", "page", &mesh);
  page_communicator_1.Start();

  ConnectToDevice(&page_communicator_1, "device2", "ledger", "page");
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

  MessageHolder<Message> reply_message =
      *CreateMessageHolder<Message>(mesh.messages_[0].second, &ParseMessage);
  ASSERT_EQ(MessageUnion_Response, reply_message->message_type());
  MessageHolder<Response> response =
      std::move(reply_message).TakeAndMap<Response>([](const Message* message) {
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
  storage_2.commits_from_sync_[0].second(ledger::Status::OK, {});
  RunLoopUntilIdle();
}

TEST_F(PageCommunicatorImplTest, GetObjectDisconnect) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");

  bool called1, called2, called3, called4;
  ledger::Status status1, status2, status3, status4;
  storage::ChangeSource source1, source2, source3, source4;
  storage::IsObjectSynced is_object_synced1, is_object_synced2,
      is_object_synced3, is_object_synced4;
  std::unique_ptr<storage::DataSource::DataChunk> data1, data2, data3, data4;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo1"),
      callback::Capture(callback::SetWhenCalled(&called1), &status1, &source1,
                        &is_object_synced1, &data1));
  page_communicator.GetObject(
      MakeObjectIdentifier("foo2"),
      callback::Capture(callback::SetWhenCalled(&called2), &status2, &source2,
                        &is_object_synced2, &data2));
  page_communicator.GetObject(
      MakeObjectIdentifier("foo3"),
      callback::Capture(callback::SetWhenCalled(&called3), &status3, &source3,
                        &is_object_synced3, &data3));
  page_communicator.GetObject(
      MakeObjectIdentifier("foo4"),
      callback::Capture(callback::SetWhenCalled(&called4), &status4, &source4,
                        &is_object_synced4, &data4));
  RunLoopUntilIdle();
  EXPECT_FALSE(called1);
  EXPECT_FALSE(called2);
  EXPECT_FALSE(called3);
  EXPECT_FALSE(called4);
  EXPECT_EQ(4u, mesh.messages_.size());

  flatbuffers::FlatBufferBuilder stop_buffer;
  BuildWatchStopBuffer(&stop_buffer, "ledger", "page");
  MessageHolder<Message> watch_stop_message = *CreateMessageHolder<Message>(
      convert::ToStringView(stop_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      "device2", std::move(watch_stop_message)
                     .TakeAndMap<Request>([](const Message* message) {
                       return static_cast<const Request*>(message->message());
                     }));
  RunLoopUntilIdle();

  // All requests are terminated with a not found status.
  EXPECT_TRUE(called1);
  EXPECT_EQ(ledger::Status::INTERNAL_NOT_FOUND, status1);
  EXPECT_EQ(storage::ChangeSource::P2P, source1);
  EXPECT_FALSE(data1);

  EXPECT_TRUE(called2);
  EXPECT_EQ(ledger::Status::INTERNAL_NOT_FOUND, status2);
  EXPECT_EQ(storage::ChangeSource::P2P, source2);
  EXPECT_FALSE(data2);

  EXPECT_TRUE(called3);
  EXPECT_EQ(ledger::Status::INTERNAL_NOT_FOUND, status3);
  EXPECT_EQ(storage::ChangeSource::P2P, source3);
  EXPECT_FALSE(data3);

  EXPECT_TRUE(called4);
  EXPECT_EQ(ledger::Status::INTERNAL_NOT_FOUND, status4);
  EXPECT_EQ(storage::ChangeSource::P2P, source4);
  EXPECT_FALSE(data4);
}

TEST_F(PageCommunicatorImplTest, CommitRequest) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  const storage::Commit& commit_1 = storage.AddCommit("commit1", "data1");

  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildCommitRequestBuffer(&request_buffer, "ledger", "page",
                           {storage::CommitId(commit_1.GetId()),
                            storage::CommitId("missing_commit")});
  MessageHolder<Message> request_message = *CreateMessageHolder<Message>(
      convert::ToStringView(request_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      "device2", std::move(request_message)
                     .TakeAndMap<Request>([](const Message* message) {
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
  EXPECT_EQ(ResponseMessage_CommitResponse, response->response_type());
  const CommitResponse* commit_response =
      static_cast<const CommitResponse*>(response->response());
  ASSERT_EQ(2u, commit_response->commits()->size());
  auto it = commit_response->commits()->begin();
  EXPECT_EQ("commit1", convert::ExtendedStringView(it->id()->id()));
  EXPECT_EQ(CommitStatus_OK, it->status());
  EXPECT_EQ("data1", convert::ExtendedStringView(it->commit()->bytes()));
  it++;
  EXPECT_EQ("missing_commit", convert::ExtendedStringView(it->id()->id()));
  EXPECT_EQ(CommitStatus_UNKNOWN_COMMIT, it->status());
}

// Sends an update for new commits that triggers a backlog sync.
TEST_F(PageCommunicatorImplTest, CommitBatchUpdate) {
  FakeDeviceMesh mesh;
  FakePageStorage storage_1(dispatcher(), "page");
  storage_1.AddCommit("id 0", "data 0");
  PageCommunicatorImpl page_communicator_1(&coroutine_service_, &storage_1,
                                           &storage_1, "ledger", "page", &mesh);
  page_communicator_1.Start();

  ConnectToDevice(&page_communicator_1, "device2", "ledger", "page");
  RunLoopUntilIdle();

  FakePageStorage storage_2(dispatcher(), "page");
  PageCommunicatorImpl page_communicator_2(&coroutine_service_, &storage_2,
                                           &storage_2, "ledger", "page", &mesh);
  page_communicator_2.Start();

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.emplace_back(std::make_unique<FakeCommit>(
      "id 1", "data 1", std::vector<storage::CommitId>({"id 0"})));
  commits.emplace_back(std::make_unique<FakeCommit>(
      "id 2", "data 2", std::vector<storage::CommitId>({"id 1"})));

  storage_1.watcher_->OnNewCommits(commits, storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();

  // Local commit: a message is sent.
  ASSERT_EQ(1u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[0].first);

  {
    MessageHolder<Message> reply_message =
        *CreateMessageHolder<Message>(mesh.messages_[0].second, &ParseMessage);
    ASSERT_EQ(MessageUnion_Response, reply_message->message_type());
    MessageHolder<Response> response =
        std::move(reply_message)
            .TakeAndMap<Response>([](const Message* message) {
              return static_cast<const Response*>(message->message());
            });
    const NamespacePageId* response_namespace_page_id =
        response->namespace_page();
    EXPECT_EQ("ledger", convert::ExtendedStringView(
                            response_namespace_page_id->namespace_id()));
    EXPECT_EQ("page", convert::ExtendedStringView(
                          response_namespace_page_id->page_id()));
    EXPECT_EQ(ResponseMessage_CommitResponse, response->response_type());

    // Send it to the other side.
    page_communicator_2.OnNewResponse("device1", std::move(response));
  }
  RunLoopUntilIdle();

  // PageCommunicator should have tried to add the commit.
  ASSERT_EQ(1u, storage_2.commits_from_sync_.size());
  EXPECT_EQ(2u, storage_2.commits_from_sync_[0].first.size());
  // Return that we miss one commit
  storage_2.commits_from_sync_[0].second(ledger::Status::INTERNAL_NOT_FOUND,
                                         {"id 0"});

  // |page_communicator_2| should ask for the base, "id 0" commit.
  ASSERT_EQ(2u, mesh.messages_.size());
  EXPECT_EQ("device1", mesh.messages_[1].first);

  {
    MessageHolder<Message> request_message =
        *CreateMessageHolder<Message>(mesh.messages_[1].second, &ParseMessage);
    ASSERT_EQ(MessageUnion_Request, request_message->message_type());
    MessageHolder<Request> request =
        std::move(request_message)
            .TakeAndMap<Request>([](const Message* message) {
              return static_cast<const Request*>(message->message());
            });
    const NamespacePageId* request_namespace_page_id =
        request->namespace_page();
    EXPECT_EQ("ledger", convert::ExtendedStringView(
                            request_namespace_page_id->namespace_id()));
    EXPECT_EQ("page", convert::ExtendedStringView(
                          request_namespace_page_id->page_id()));
    EXPECT_EQ(RequestMessage_CommitRequest, request->request_type());

    // Send it to the other side.
    page_communicator_1.OnNewRequest("device2", std::move(request));
  }
  RunLoopUntilIdle();

  // |page_communicator_1| sends commit "id 0" to device 2.
  ASSERT_EQ(3u, mesh.messages_.size());
  EXPECT_EQ("device2", mesh.messages_[2].first);

  {
    MessageHolder<Message> reply_message =
        *CreateMessageHolder<Message>(mesh.messages_[2].second, &ParseMessage);
    ASSERT_EQ(MessageUnion_Response, reply_message->message_type());
    MessageHolder<Response> response =
        std::move(reply_message)
            .TakeAndMap<Response>([](const Message* message) {
              return static_cast<const Response*>(message->message());
            });
    const NamespacePageId* response_namespace_page_id =
        response->namespace_page();
    EXPECT_EQ("ledger", convert::ExtendedStringView(
                            response_namespace_page_id->namespace_id()));
    EXPECT_EQ("page", convert::ExtendedStringView(
                          response_namespace_page_id->page_id()));
    EXPECT_EQ(ResponseMessage_CommitResponse, response->response_type());

    // Send it to the other side.
    page_communicator_2.OnNewResponse("device1", std::move(response));
  }
  RunLoopUntilIdle();

  // Verify that we are truely adding the whole commit batch.
  ASSERT_EQ(2u, storage_2.commits_from_sync_.size());
  EXPECT_EQ(3u, storage_2.commits_from_sync_[1].first.size());
  EXPECT_EQ("id 0", storage_2.commits_from_sync_[1].first[0].id);
  EXPECT_EQ("data 0", storage_2.commits_from_sync_[1].first[0].bytes);
  EXPECT_EQ("id 1", storage_2.commits_from_sync_[1].first[1].id);
  EXPECT_EQ("data 1", storage_2.commits_from_sync_[1].first[1].bytes);
  EXPECT_EQ("id 2", storage_2.commits_from_sync_[1].first[2].id);
  EXPECT_EQ("data 2", storage_2.commits_from_sync_[1].first[2].bytes);

  // Verify we don't crash on response from storage
  storage_2.commits_from_sync_[1].second(ledger::Status::OK, {});
}

// Removes a device while we are performing the GetObject call.
TEST_F(PageCommunicatorImplTest, GetObjectRemoveDevice) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");
  ConnectToDevice(&page_communicator, "device3", "ledger", "page");
  ConnectToDevice(&page_communicator, "device4", "ledger", "page");

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;

  mesh.OnNextSend("device3", [&page_communicator]() {
    page_communicator.OnDeviceChange("device3",
                                     p2p_provider::DeviceChangeType::DELETED);
  });

  page_communicator.GetObject(
      MakeObjectIdentifier("foo1"),
      callback::Capture(callback::SetWhenCalled(&called), &status, &source,
                        &is_object_synced, &data));

  // The previous call to GetObject should return and not result in an
  // exception. Note that it is expected for GetObject callback to not be
  // called.
}

// Removes a device while we are performing the OnNewCommits call.
TEST_F(PageCommunicatorImplTest, OnNewCommitsRemoveDevice) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");
  ConnectToDevice(&page_communicator, "device3", "ledger", "page");
  ConnectToDevice(&page_communicator, "device4", "ledger", "page");

  mesh.OnNextSend("device3", [&page_communicator]() {
    page_communicator.OnDeviceChange("device3",
                                     p2p_provider::DeviceChangeType::DELETED);
  });

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.emplace_back(std::make_unique<FakeCommit>("id 1", "data 1"));
  commits.emplace_back(std::make_unique<FakeCommit>("id 2", "data 2"));
  ASSERT_NE(nullptr, storage.watcher_);
  storage.watcher_->OnNewCommits(commits, storage::ChangeSource::LOCAL);

  // The previous call to OnNewCommits should return and not result in an
  // exception.
}

// Removes a device while destroying PageCommunicatorImpl.
TEST_F(PageCommunicatorImplTest, DestructionRemoveDevice) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&coroutine_service_, &storage,
                                         &storage, "ledger", "page", &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, "device2", "ledger", "page");
  ConnectToDevice(&page_communicator, "device3", "ledger", "page");
  ConnectToDevice(&page_communicator, "device4", "ledger", "page");

  mesh.OnNextSend("device3", [&page_communicator]() {
    page_communicator.OnDeviceChange("device3",
                                     p2p_provider::DeviceChangeType::DELETED);
  });

  // The destructor of PageCommunicatorImpl sends messages to connected devices.
  // This test succeeds if this destructor completes without throwing an
  // exception.
}

}  // namespace
}  // namespace p2p_sync
