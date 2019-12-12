// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/page_communicator_impl.h"

#include <lib/async/cpp/task.h>
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
#include "src/ledger/bin/p2p_sync/impl/device_mesh.h"
#include "src/ledger/bin/p2p_sync/impl/encoding.h"
#include "src/ledger/bin/p2p_sync/impl/message_generated.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/storage/fake/fake_object.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/storage/testing/storage_matcher.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

using storage::MatchesCommitIdAndBytes;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::SizeIs;

namespace p2p_sync {
namespace {
p2p_provider::P2PClientId MakeP2PClientId(uint8_t id) { return p2p_provider::P2PClientId({id}); }

// Creates a dummy object identifier.
// |object_digest| does not need to be valid (wrt. internal storage constraints) as it is only used
// as an opaque identifier for p2p. It does not need to be tracked either because we are using a
// fake PageStorage that does not perform garbage collection.
storage::ObjectIdentifier MakeObjectIdentifier(std::string object_digest) {
  return storage::ObjectIdentifier(0, storage::ObjectDigest(std::move(object_digest)), nullptr);
}

class FakeCommit : public storage::CommitEmptyImpl {
 public:
  FakeCommit(std::string id, std::string data, std::vector<storage::CommitId> parents = {})
      : id_(std::move(id)), data_(std::move(data)), parents_(std::move(parents)) {}

  const storage::CommitId& GetId() const override { return id_; }

  std::vector<storage::CommitIdView> GetParentIds() const override {
    std::vector<storage::CommitIdView> parent_ids;
    for (const storage::CommitId& id : parents_) {
      parent_ids.emplace_back(id);
    }
    return parent_ids;
  }

  absl::string_view GetStorageBytes() const override { return data_; }

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
  ~FakePageStorage() override = default;

  storage::PageId GetId() override { return page_id_; }

  ledger::Status GetHeadCommits(
      std::vector<std::unique_ptr<const storage::Commit>>* head_commits) override {
    *head_commits = std::vector<std::unique_ptr<const storage::Commit>>();
    head_commits->push_back(std::make_unique<const FakeCommit>("commit_id", "data"));
    return ledger::Status::OK;
  }

  const FakeCommit& AddCommit(std::string id, std::string data) {
    auto commit = commits_.try_emplace(id, id, std::move(data));
    return commit.first->second;
  }

  void GetCommit(storage::CommitIdView commit_id,
                 fit::function<void(ledger::Status, std::unique_ptr<const storage::Commit>)>
                     callback) override {
    auto it = commits_.find(commit_id);
    if (it == commits_.end()) {
      callback(ledger::Status::INTERNAL_NOT_FOUND, nullptr);
      return;
    }
    callback(ledger::Status::OK, it->second.Clone());
  }

  void GetPiece(storage::ObjectIdentifier object_identifier,
                fit::function<void(ledger::Status, std::unique_ptr<const storage::Piece>)> callback)
      override {
    async::PostTask(dispatcher_, [this, object_identifier, callback = std::move(callback)]() {
      const auto& it = objects_.find(object_identifier);
      if (it == objects_.end()) {
        callback(ledger::Status::INTERNAL_NOT_FOUND, nullptr);
        return;
      }
      callback(ledger::Status::OK,
               std::make_unique<storage::fake::FakePiece>(object_identifier, it->second));
    });
  }

  void SetPiece(storage::ObjectIdentifier object_identifier, std::string contents,
                bool is_synced = false) {
    objects_[object_identifier] = std::move(contents);
    if (is_synced) {
      synced_objects_.insert(std::move(object_identifier));
    }
  }

  void IsPieceSynced(storage::ObjectIdentifier object_identifier,
                     fit::function<void(ledger::Status, bool)> callback) override {
    async::PostTask(dispatcher_, [this, object_identifier, callback = std::move(callback)]() {
      const auto& it = objects_.find(object_identifier);
      if (it == objects_.end()) {
        callback(ledger::Status::INTERNAL_NOT_FOUND, false);
        return;
      }
      callback(ledger::Status::OK,
               synced_objects_.find(object_identifier) != synced_objects_.end());
    });
  }

  void AddCommitsFromSync(std::vector<storage::PageStorage::CommitIdAndBytes> ids_and_bytes,
                          const storage::ChangeSource /*source*/,
                          fit::function<void(ledger::Status)> callback) override {
    commits_from_sync_.emplace_back(std::move(ids_and_bytes), std::move(callback));
  }

  void GetGenerationAndMissingParents(
      const storage::PageStorage::CommitIdAndBytes& ids_and_bytes,
      fit::function<void(ledger::Status, uint64_t, std::vector<storage::CommitId>)> callback)
      override {
    const auto& [generation, missing_parents] = generation_and_missing_parents_[ids_and_bytes.id];
    callback(ledger::Status::OK, generation, missing_parents);
  }

  void AddCommitWatcher(storage::CommitWatcher* watcher) override {
    LEDGER_DCHECK(!watcher_);
    watcher_ = watcher;
  }

  void MarkSyncedToPeer(fit::function<void(ledger::Status)> callback) override {
    callback(mark_synced_to_peer_status);
  }

  storage::ObjectIdentifierFactory* GetObjectIdentifierFactory() override {
    return &object_identifier_factory_;
  }

  storage::CommitWatcher* watcher_ = nullptr;
  std::vector<std::pair<std::vector<storage::PageStorage::CommitIdAndBytes>,
                        fit::function<void(ledger::Status)>>>
      commits_from_sync_;
  ledger::Status mark_synced_to_peer_status = ledger::Status::OK;
  std::map<storage::CommitId, std::pair<uint64_t, std::vector<storage::CommitId>>>
      generation_and_missing_parents_;

 private:
  async_dispatcher_t* const dispatcher_;
  const std::string page_id_;
  std::map<storage::ObjectIdentifier, std::string> objects_;
  std::set<storage::ObjectIdentifier> synced_objects_;
  std::map<storage::CommitId, FakeCommit, convert::StringViewComparator> commits_;
  storage::fake::FakeObjectIdentifierFactory object_identifier_factory_;
};

class FakeDeviceMesh : public DeviceMesh {
 public:
  FakeDeviceMesh() = default;
  ~FakeDeviceMesh() override = default;

  void OnNextSend(p2p_provider::P2PClientId device_name, fit::closure callback) {
    callbacks_[device_name] = std::move(callback);
  }

  DeviceSet GetDeviceList() override { return devices_; }

  void Send(const p2p_provider::P2PClientId& device_name,
            convert::ExtendedStringView data) override {
    messages_.emplace_back(std::forward_as_tuple(device_name, convert::ToString(data)));
    auto it = callbacks_.find(device_name);
    if (it != callbacks_.end()) {
      it->second();
      callbacks_.erase(it);
    }
  }

  DeviceSet devices_;
  std::vector<std::pair<p2p_provider::P2PClientId, std::string>> messages_;
  std::map<p2p_provider::P2PClientId, fit::closure> callbacks_;
};

void BuildWatchStartBuffer(flatbuffers::FlatBufferBuilder* buffer, absl::string_view namespace_id,
                           absl::string_view page_id) {
  flatbuffers::Offset<WatchStartRequest> watch_start = CreateWatchStartRequest(*buffer);
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_WatchStartRequest, watch_start.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void BuildWatchStopBuffer(flatbuffers::FlatBufferBuilder* buffer, absl::string_view namespace_id,
                          absl::string_view page_id) {
  flatbuffers::Offset<WatchStopRequest> watch_stop = CreateWatchStopRequest(*buffer);
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_WatchStopRequest, watch_stop.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void BuildObjectRequestBuffer(flatbuffers::FlatBufferBuilder* buffer,
                              absl::string_view namespace_id, absl::string_view page_id,
                              std::vector<storage::ObjectIdentifier> object_ids) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  std::vector<flatbuffers::Offset<ObjectId>> fb_object_ids;
  fb_object_ids.reserve(object_ids.size());
  for (const storage::ObjectIdentifier& object_id : object_ids) {
    fb_object_ids.emplace_back(
        CreateObjectId(*buffer, object_id.key_index(),
                       convert::ToFlatBufferVector(buffer, object_id.object_digest().Serialize())));
  }
  flatbuffers::Offset<ObjectRequest> object_request =
      CreateObjectRequest(*buffer, buffer->CreateVector(fb_object_ids));
  flatbuffers::Offset<Request> fb_request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_ObjectRequest, object_request.Union());
  flatbuffers::Offset<Message> fb_message =
      CreateMessage(*buffer, MessageUnion_Request, fb_request.Union());
  buffer->Finish(fb_message);
}

void BuildObjectResponseBuffer(
    flatbuffers::FlatBufferBuilder* buffer, absl::string_view namespace_id,
    absl::string_view page_id,
    std::vector<std::tuple<storage::ObjectIdentifier, std::string, bool>> data) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  std::vector<flatbuffers::Offset<Object>> fb_objects;
  for (const auto& object_tuple : data) {
    const storage::ObjectIdentifier& object_identifier = std::get<0>(object_tuple);
    const std::string& data = std::get<1>(object_tuple);
    bool is_synced = std::get<2>(object_tuple);

    flatbuffers::Offset<ObjectId> fb_object_id = CreateObjectId(
        *buffer, object_identifier.key_index(),
        convert::ToFlatBufferVector(buffer, object_identifier.object_digest().Serialize()));
    if (!data.empty()) {
      flatbuffers::Offset<Data> fb_data =
          CreateData(*buffer, convert::ToFlatBufferVector(buffer, data));
      fb_objects.emplace_back(
          CreateObject(*buffer, fb_object_id, ObjectStatus_OK, fb_data,
                       is_synced ? ObjectSyncStatus_SYNCED_TO_CLOUD : ObjectSyncStatus_UNSYNCED));
    } else {
      fb_objects.emplace_back(CreateObject(*buffer, fb_object_id, ObjectStatus_UNKNOWN_OBJECT));
    }
  }
  flatbuffers::Offset<ObjectResponse> object_response =
      CreateObjectResponse(*buffer, buffer->CreateVector(fb_objects));
  flatbuffers::Offset<Response> response =
      CreateResponse(*buffer, ResponseStatus_OK, namespace_page_id, ResponseMessage_ObjectResponse,
                     object_response.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Response, response.Union());
  buffer->Finish(message);
}

void BuildCommitRequestBuffer(flatbuffers::FlatBufferBuilder* buffer,
                              absl::string_view namespace_id, absl::string_view page_id,
                              std::vector<storage::CommitId> commit_ids) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  std::vector<flatbuffers::Offset<CommitId>> fb_commit_ids;
  fb_commit_ids.reserve(commit_ids.size());
  for (const storage::CommitId& commit_id : commit_ids) {
    fb_commit_ids.emplace_back(
        CreateCommitId(*buffer, convert::ToFlatBufferVector(buffer, commit_id)));
  }
  flatbuffers::Offset<CommitRequest> commit_request =
      CreateCommitRequest(*buffer, buffer->CreateVector(fb_commit_ids));
  flatbuffers::Offset<Request> fb_request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_CommitRequest, commit_request.Union());
  flatbuffers::Offset<Message> fb_message =
      CreateMessage(*buffer, MessageUnion_Request, fb_request.Union());
  buffer->Finish(fb_message);
}

void BuildCommitBuffer(flatbuffers::FlatBufferBuilder* buffer, absl::string_view namespace_id,
                       absl::string_view page_id,
                       const std::vector<std::unique_ptr<const storage::Commit>>& commits) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  std::vector<flatbuffers::Offset<Commit>> fb_commits;
  for (const auto& commit : commits) {
    flatbuffers::Offset<CommitId> fb_commit_id =
        CreateCommitId(*buffer, convert::ToFlatBufferVector(buffer, commit->GetId()));
    flatbuffers::Offset<Data> fb_commit_data =
        CreateData(*buffer, convert::ToFlatBufferVector(buffer, commit->GetStorageBytes()));
    fb_commits.emplace_back(CreateCommit(*buffer, fb_commit_id, CommitStatus_OK, fb_commit_data));
  }

  flatbuffers::Offset<CommitResponse> commit_response =
      CreateCommitResponse(*buffer, buffer->CreateVector(fb_commits));
  flatbuffers::Offset<Response> response =
      CreateResponse(*buffer, ResponseStatus_OK, namespace_page_id, ResponseMessage_CommitResponse,
                     commit_response.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Response, response.Union());
  buffer->Finish(message);
}

void ConnectToDevice(PageCommunicatorImpl* page_communicator, p2p_provider::P2PClientId device,
                     absl::string_view ledger, absl::string_view page) {
  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer, ledger, page);
  MessageHolder<Message> message =
      *CreateMessageHolder<Message>(convert::ToStringView(buffer), &ParseMessage);
  page_communicator->OnNewRequest(
      device, std::move(message).TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));
}

using PageCommunicatorImplTest = ledger::TestWithEnvironment;

TEST_F(PageCommunicatorImplTest, ConnectToExistingMesh) {
  FakeDeviceMesh mesh;
  mesh.devices_.emplace(MakeP2PClientId(2u));
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);

  EXPECT_TRUE(mesh.messages_.empty());

  page_communicator.Start();

  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  if (!VerifyMessageBuffer(verifier)) {
    // Wrong serialization, abort.
    LEDGER_LOG(ERROR) << "The message received is malformed.";
    return;
  };
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(message->message_type(), MessageUnion_Request);
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->page_id()), "page");
  EXPECT_EQ(request->request_type(), RequestMessage_WatchStartRequest);
}

TEST_F(PageCommunicatorImplTest, ConnectToNewMeshParticipant) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  EXPECT_TRUE(mesh.messages_.empty());

  mesh.devices_.emplace(MakeP2PClientId(2u));
  page_communicator.OnDeviceChange(MakeP2PClientId(2u), p2p_provider::DeviceChangeType::NEW);

  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  if (!VerifyMessageBuffer(verifier)) {
    // Wrong serialization, abort.
    LEDGER_LOG(ERROR) << "The message received is malformed.";
    return;
  };
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(message->message_type(), MessageUnion_Request);
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->page_id()), "page");
  EXPECT_EQ(request->request_type(), RequestMessage_WatchStartRequest);
}

TEST_F(PageCommunicatorImplTest, SendHeadOnWatchStartRequest) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();

  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  // Verify that a CommitResponse message has been sent.
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(message->message_type(), MessageUnion_Response);
  const Response* response = static_cast<const Response*>(message->message());
  ASSERT_NE(response, nullptr);
  const NamespacePageId* namespace_page_id = response->namespace_page();
  ASSERT_NE(namespace_page_id, nullptr);
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->page_id()), "page");
  ASSERT_EQ(response->response_type(), ResponseMessage_CommitResponse);
  const CommitResponse* commit_response = static_cast<const CommitResponse*>(response->response());
  ASSERT_NE(commit_response, nullptr);
  ASSERT_EQ(commit_response->commits()->size(), 1u);
  const Commit* commit = *commit_response->commits()->begin();
  ASSERT_NE(commit->id(), nullptr);
  EXPECT_EQ(convert::ExtendedStringView(commit->id()->id()), "commit_id");
  EXPECT_EQ(commit->status(), CommitStatus_OK);
  ASSERT_NE(commit->commit(), nullptr);
  EXPECT_EQ(convert::ExtendedStringView(commit->commit()->bytes()), "data");
}

class FakePageStorageWithTwoHeads : public FakePageStorage {
 public:
  FakePageStorageWithTwoHeads(async_dispatcher_t* dispatcher, std::string page_id)
      : FakePageStorage(dispatcher, page_id){};
  ~FakePageStorageWithTwoHeads() override = default;

  ledger::Status GetHeadCommits(
      std::vector<std::unique_ptr<const storage::Commit>>* head_commits) override {
    *head_commits = std::vector<std::unique_ptr<const storage::Commit>>();
    head_commits->push_back(std::make_unique<const FakeCommit>("commit_id1", "data1"));
    head_commits->push_back(std::make_unique<const FakeCommit>("commit_id2", "data2"));
    return ledger::Status::OK;
  }
};

TEST_F(PageCommunicatorImplTest, DontSendMultipleHeadsOnWatchStartRequest) {
  FakeDeviceMesh mesh;
  FakePageStorageWithTwoHeads storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();

  ASSERT_EQ(mesh.messages_.size(), 0u);
}

TEST_F(PageCommunicatorImplTest, GetObject) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  // Verify the message sent to request the object.
  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(message->message_type(), MessageUnion_Request);
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->page_id()), "page");
  EXPECT_EQ(request->request_type(), RequestMessage_ObjectRequest);
  const ObjectRequest* object_request = static_cast<const ObjectRequest*>(request->request());
  EXPECT_EQ(object_request->object_ids()->size(), 1u);
  EXPECT_EQ(object_request->object_ids()->begin()->key_index(), 0u);
  EXPECT_EQ(convert::ExtendedStringView(object_request->object_ids()->begin()->digest()), "foo");
}

TEST_F(PageCommunicatorImplTest, DontGetObjectsIfMarkPageSyncedToPeerFailed) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  // If storage fails to mark the page as synced to a peer, the mesh should not
  // be updated.
  storage.mark_synced_to_peer_status = ledger::Status::IO_ERROR;
  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::INTERNAL_NOT_FOUND);
  EXPECT_THAT(mesh.messages_, IsEmpty());
}

TEST_F(PageCommunicatorImplTest, ObjectRequest) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  storage.SetPiece(MakeObjectIdentifier("object_digest"), "some data");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildObjectRequestBuffer(
      &request_buffer, "ledger", "page",
      {MakeObjectIdentifier("object_digest"), MakeObjectIdentifier("object_digest2")});
  MessageHolder<Message> request_message =
      *CreateMessageHolder<Message>(convert::ToStringView(request_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      MakeP2PClientId(2u),
      std::move(request_message).TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  RunLoopUntilIdle();

  // Verify the response.
  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  const Message* reply_message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(reply_message->message_type(), MessageUnion_Response);
  const Response* response = static_cast<const Response*>(reply_message->message());
  const NamespacePageId* response_namespace_page_id = response->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->page_id()), "page");
  EXPECT_EQ(response->response_type(), ResponseMessage_ObjectResponse);
  const ObjectResponse* object_response = static_cast<const ObjectResponse*>(response->response());
  ASSERT_EQ(object_response->objects()->size(), 2u);
  auto it = object_response->objects()->begin();
  EXPECT_EQ(convert::ExtendedStringView(it->id()->digest()), "object_digest");
  EXPECT_EQ(it->status(), ObjectStatus_OK);
  EXPECT_EQ(convert::ExtendedStringView(it->data()->bytes()), "some data");
  EXPECT_EQ(it->sync_status(), ObjectSyncStatus_UNSYNCED);
  it++;
  EXPECT_EQ(convert::ExtendedStringView(it->id()->digest()), "object_digest2");
  EXPECT_EQ(it->status(), ObjectStatus_UNKNOWN_OBJECT);
}

TEST_F(PageCommunicatorImplTest, ObjectRequestSynced) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  storage.SetPiece(MakeObjectIdentifier("object_digest"), "some data", true);
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildObjectRequestBuffer(&request_buffer, "ledger", "page",
                           {MakeObjectIdentifier("object_digest")});
  MessageHolder<Message> request_message =
      *CreateMessageHolder<Message>(convert::ToStringView(request_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      MakeP2PClientId(2u),
      std::move(request_message).TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  RunLoopUntilIdle();

  // Verify the response.
  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  const Message* reply_message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(reply_message->message_type(), MessageUnion_Response);
  const Response* response = static_cast<const Response*>(reply_message->message());
  const NamespacePageId* response_namespace_page_id = response->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->page_id()), "page");
  EXPECT_EQ(response->response_type(), ResponseMessage_ObjectResponse);
  const ObjectResponse* object_response = static_cast<const ObjectResponse*>(response->response());
  ASSERT_EQ(object_response->objects()->size(), 1u);
  auto it = object_response->objects()->begin();
  EXPECT_EQ(convert::ExtendedStringView(it->id()->digest()), "object_digest");
  EXPECT_EQ(it->status(), ObjectStatus_OK);
  EXPECT_EQ(convert::ExtendedStringView(it->data()->bytes()), "some data");
  EXPECT_EQ(it->sync_status(), ObjectSyncStatus_SYNCED_TO_CLOUD);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseSuccess) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(&response_buffer, "ledger", "page",
                            {std::make_tuple(MakeObjectIdentifier("foo"), "foo_data", false),
                             std::make_tuple(MakeObjectIdentifier("bar"), "bar_data", false)});
  MessageHolder<Message> response_message =
      *CreateMessageHolder<Message>(convert::ToStringView(response_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(2u),
      std::move(response_message).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(data->Get(), "foo_data");
  EXPECT_EQ(is_object_synced, storage::IsObjectSynced::NO);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseSynced) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(&response_buffer, "ledger", "page",
                            {std::make_tuple(MakeObjectIdentifier("foo"), "foo_data", true)});
  MessageHolder<Message> response_message =
      *CreateMessageHolder<Message>(convert::ToStringView(response_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(2u),
      std::move(response_message).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(data->Get(), "foo_data");
  EXPECT_EQ(is_object_synced, storage::IsObjectSynced::YES);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseFail) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(&response_buffer, "ledger", "page",
                            {std::make_tuple(MakeObjectIdentifier("foo"), "", false)});
  MessageHolder<Message> response_message =
      *CreateMessageHolder<Message>(convert::ToStringView(response_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(2u),
      std::move(response_message).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::INTERNAL_NOT_FOUND);
  EXPECT_FALSE(data);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseMultiDeviceSuccess) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  ConnectToDevice(&page_communicator, MakeP2PClientId(3u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(mesh.messages_.size(), 2u);

  flatbuffers::FlatBufferBuilder response_buffer_1;
  BuildObjectResponseBuffer(&response_buffer_1, "ledger", "page",
                            {std::make_tuple(MakeObjectIdentifier("foo"), "", false)});
  MessageHolder<Message> message_1 =
      *CreateMessageHolder<Message>(convert::ToStringView(response_buffer_1), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(2u), std::move(message_1).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));
  EXPECT_FALSE(called);

  flatbuffers::FlatBufferBuilder response_buffer_2;
  BuildObjectResponseBuffer(&response_buffer_2, "ledger", "page",
                            {std::make_tuple(MakeObjectIdentifier("foo"), "foo_data", false)});
  MessageHolder<Message> message_2 =
      *CreateMessageHolder<Message>(convert::ToStringView(response_buffer_2), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(3u), std::move(message_2).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(data->Get(), "foo_data");
  EXPECT_EQ(source, storage::ChangeSource::P2P);
  EXPECT_EQ(is_object_synced, storage::IsObjectSynced::NO);
}

TEST_F(PageCommunicatorImplTest, GetObjectProcessResponseMultiDeviceFail) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  ConnectToDevice(&page_communicator, MakeP2PClientId(3u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(mesh.messages_.size(), 2u);

  flatbuffers::FlatBufferBuilder response_buffer_1;
  BuildObjectResponseBuffer(&response_buffer_1, "ledger", "page",
                            {std::make_tuple(MakeObjectIdentifier("foo"), "", false)});
  MessageHolder<Message> message_1 =
      *CreateMessageHolder<Message>(convert::ToStringView(response_buffer_1), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(2u), std::move(message_1).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));
  EXPECT_FALSE(called);

  flatbuffers::FlatBufferBuilder response_buffer_2;
  BuildObjectResponseBuffer(&response_buffer_2, "ledger", "page",
                            {std::make_tuple(MakeObjectIdentifier("foo"), "", false)});
  MessageHolder<Message> message_2 =
      *CreateMessageHolder<Message>(convert::ToStringView(response_buffer_2), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(3u), std::move(message_2).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::INTERNAL_NOT_FOUND);
  EXPECT_FALSE(data);
}

TEST_F(PageCommunicatorImplTest, GetObjectMultipleCalls) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called1, called2;
  storage::Status status1, status2;
  storage::ChangeSource source1, source2;
  storage::IsObjectSynced is_object_synced1, is_object_synced2;
  std::unique_ptr<storage::DataSource::DataChunk> data1, data2;
  page_communicator.GetObject(MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
                              ledger::Capture(ledger::SetWhenCalled(&called1), &status1, &source1,
                                              &is_object_synced1, &data1));
  RunLoopUntilIdle();
  EXPECT_FALSE(called1);

  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  page_communicator.GetObject(MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
                              ledger::Capture(ledger::SetWhenCalled(&called2), &status2, &source2,
                                              &is_object_synced2, &data2));
  RunLoopUntilIdle();
  EXPECT_FALSE(called2);

  flatbuffers::FlatBufferBuilder response_buffer;
  BuildObjectResponseBuffer(&response_buffer, "ledger", "page",
                            {std::make_tuple(MakeObjectIdentifier("foo"), "foo_data", true)});
  MessageHolder<Message> response_message =
      *CreateMessageHolder<Message>(convert::ToStringView(response_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(2u),
      std::move(response_message).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  EXPECT_TRUE(called1);
  EXPECT_TRUE(called2);
  EXPECT_EQ(status1, storage::Status::OK);
  EXPECT_EQ(status2, storage::Status::OK);
  EXPECT_EQ(data1->Get(), "foo_data");
  EXPECT_EQ(data2->Get(), "foo_data");
  EXPECT_EQ(is_object_synced1, storage::IsObjectSynced::YES);
  EXPECT_EQ(is_object_synced2, storage::IsObjectSynced::YES);
}

TEST_F(PageCommunicatorImplTest, CommitUpdate) {
  FakeDeviceMesh mesh;
  FakePageStorage storage_1(dispatcher(), "page");
  PageCommunicatorImpl page_communicator_1(&environment_, &storage_1, &storage_1, "ledger", "page",
                                           &mesh);
  page_communicator_1.Start();

  ConnectToDevice(&page_communicator_1, MakeP2PClientId(2u), "ledger", "page");

  FakePageStorage storage_2(dispatcher(), "page");
  storage_2.generation_and_missing_parents_["id 2"] = {1, {"id 1"}};
  PageCommunicatorImpl page_communicator_2(&environment_, &storage_2, &storage_2, "ledger", "page",
                                           &mesh);
  page_communicator_2.Start();
  ConnectToDevice(&page_communicator_2, MakeP2PClientId(1u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.emplace_back(std::make_unique<FakeCommit>("id 1", "data 1"));
  commits.emplace_back(std::make_unique<FakeCommit>("id 2", "data 2"));
  ASSERT_NE(nullptr, storage_1.watcher_);
  storage_1.watcher_->OnNewCommits(commits, storage::ChangeSource::CLOUD);

  RunLoopUntilIdle();
  // No new message is sent on commits from CLOUD.
  ASSERT_EQ(mesh.messages_.size(), 0u);

  storage_1.watcher_->OnNewCommits(commits, storage::ChangeSource::P2P);

  RunLoopUntilIdle();
  // No new message is sent on commits from P2P either.
  ASSERT_EQ(mesh.messages_.size(), 0u);

  storage_1.watcher_->OnNewCommits(commits, storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();

  // Local commit: a message is sent.
  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  MessageHolder<Message> reply_message =
      *CreateMessageHolder<Message>(mesh.messages_[0].second, &ParseMessage);
  ASSERT_EQ(reply_message->message_type(), MessageUnion_Response);
  MessageHolder<Response> response =
      std::move(reply_message).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      });
  const NamespacePageId* response_namespace_page_id = response->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->page_id()), "page");
  EXPECT_EQ(response->response_type(), ResponseMessage_CommitResponse);

  // Send it to the other side.
  page_communicator_2.OnNewResponse(MakeP2PClientId(1u), std::move(response));
  RunLoopUntilIdle();

  // The other side's storage has the commit.
  ASSERT_EQ(storage_2.commits_from_sync_.size(), 1u);
  ASSERT_THAT(storage_2.commits_from_sync_[0].first,
              ElementsAre(MatchesCommitIdAndBytes("id 1", "data 1"),
                          MatchesCommitIdAndBytes("id 2", "data 2")));

  // Verify we don't crash on response from storage
  storage_2.commits_from_sync_[0].second(ledger::Status::OK);
  RunLoopUntilIdle();
}

TEST_F(PageCommunicatorImplTest, GetObjectDisconnect) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called1, called2, called3, called4;
  ledger::Status status1, status2, status3, status4;
  storage::ChangeSource source1, source2, source3, source4;
  storage::IsObjectSynced is_object_synced1, is_object_synced2, is_object_synced3,
      is_object_synced4;
  std::unique_ptr<storage::DataSource::DataChunk> data1, data2, data3, data4;
  page_communicator.GetObject(MakeObjectIdentifier("foo1"), storage::RetrievedObjectType::BLOB,
                              ledger::Capture(ledger::SetWhenCalled(&called1), &status1, &source1,
                                              &is_object_synced1, &data1));
  page_communicator.GetObject(MakeObjectIdentifier("foo2"), storage::RetrievedObjectType::BLOB,
                              ledger::Capture(ledger::SetWhenCalled(&called2), &status2, &source2,
                                              &is_object_synced2, &data2));
  page_communicator.GetObject(MakeObjectIdentifier("foo3"), storage::RetrievedObjectType::BLOB,
                              ledger::Capture(ledger::SetWhenCalled(&called3), &status3, &source3,
                                              &is_object_synced3, &data3));
  page_communicator.GetObject(MakeObjectIdentifier("foo4"), storage::RetrievedObjectType::BLOB,
                              ledger::Capture(ledger::SetWhenCalled(&called4), &status4, &source4,
                                              &is_object_synced4, &data4));
  RunLoopUntilIdle();
  EXPECT_FALSE(called1);
  EXPECT_FALSE(called2);
  EXPECT_FALSE(called3);
  EXPECT_FALSE(called4);
  EXPECT_EQ(mesh.messages_.size(), 4u);

  flatbuffers::FlatBufferBuilder stop_buffer;
  BuildWatchStopBuffer(&stop_buffer, "ledger", "page");
  MessageHolder<Message> watch_stop_message =
      *CreateMessageHolder<Message>(convert::ToStringView(stop_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      MakeP2PClientId(2u),
      std::move(watch_stop_message).TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));
  RunLoopUntilIdle();

  // All requests are terminated with a not found status.
  EXPECT_TRUE(called1);
  EXPECT_EQ(status1, ledger::Status::INTERNAL_NOT_FOUND);
  EXPECT_EQ(source1, storage::ChangeSource::P2P);
  EXPECT_FALSE(data1);

  EXPECT_TRUE(called2);
  EXPECT_EQ(status2, ledger::Status::INTERNAL_NOT_FOUND);
  EXPECT_EQ(source2, storage::ChangeSource::P2P);
  EXPECT_FALSE(data2);

  EXPECT_TRUE(called3);
  EXPECT_EQ(status3, ledger::Status::INTERNAL_NOT_FOUND);
  EXPECT_EQ(source3, storage::ChangeSource::P2P);
  EXPECT_FALSE(data3);

  EXPECT_TRUE(called4);
  EXPECT_EQ(status4, ledger::Status::INTERNAL_NOT_FOUND);
  EXPECT_EQ(source4, storage::ChangeSource::P2P);
  EXPECT_FALSE(data4);
}

TEST_F(PageCommunicatorImplTest, CommitRequest) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  const storage::Commit& commit_1 = storage.AddCommit("commit1", "data1");

  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  // Send request to PageCommunicator. We request two objects: |object_digest|
  // and |object_digest2|. Only |object_digest| will be present in storage.
  flatbuffers::FlatBufferBuilder request_buffer;
  BuildCommitRequestBuffer(
      &request_buffer, "ledger", "page",
      {storage::CommitId(commit_1.GetId()), storage::CommitId("missing_commit")});
  MessageHolder<Message> request_message =
      *CreateMessageHolder<Message>(convert::ToStringView(request_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      MakeP2PClientId(2u),
      std::move(request_message).TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));

  RunLoopUntilIdle();

  // Verify the response.
  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  const Message* reply_message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(reply_message->message_type(), MessageUnion_Response);
  const Response* response = static_cast<const Response*>(reply_message->message());
  const NamespacePageId* response_namespace_page_id = response->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->page_id()), "page");
  EXPECT_EQ(response->response_type(), ResponseMessage_CommitResponse);
  const CommitResponse* commit_response = static_cast<const CommitResponse*>(response->response());
  ASSERT_EQ(commit_response->commits()->size(), 2u);
  auto it = commit_response->commits()->begin();
  EXPECT_EQ(convert::ExtendedStringView(it->id()->id()), "commit1");
  EXPECT_EQ(it->status(), CommitStatus_OK);
  EXPECT_EQ(convert::ExtendedStringView(it->commit()->bytes()), "data1");
  it++;
  EXPECT_EQ(convert::ExtendedStringView(it->id()->id()), "missing_commit");
  EXPECT_EQ(it->status(), CommitStatus_UNKNOWN_COMMIT);
}

// Sends an update for new commits that triggers a backlog sync.
TEST_F(PageCommunicatorImplTest, CommitBatchUpdate) {
  FakeDeviceMesh mesh;
  FakePageStorage storage_1(dispatcher(), "page");
  storage_1.AddCommit("id 0", "data 0");
  PageCommunicatorImpl page_communicator_1(&environment_, &storage_1, &storage_1, "ledger", "page",
                                           &mesh);
  page_communicator_1.Start();

  FakePageStorage storage_2(dispatcher(), "page");
  PageCommunicatorImpl page_communicator_2(&environment_, &storage_2, &storage_2, "ledger", "page",
                                           &mesh);
  storage_2.generation_and_missing_parents_["id 1"] = {1, {"id 0"}};
  storage_2.generation_and_missing_parents_["id 2"] = {2, {"id 1"}};
  page_communicator_2.Start();

  ConnectToDevice(&page_communicator_1, MakeP2PClientId(2u), "ledger", "page");
  ConnectToDevice(&page_communicator_2, MakeP2PClientId(1u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.emplace_back(
      std::make_unique<FakeCommit>("id 1", "data 1", std::vector<storage::CommitId>({"id 0"})));
  commits.emplace_back(
      std::make_unique<FakeCommit>("id 2", "data 2", std::vector<storage::CommitId>({"id 1"})));

  storage_1.watcher_->OnNewCommits(commits, storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();

  // Local commit: a message is sent.
  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  {
    MessageHolder<Message> reply_message =
        *CreateMessageHolder<Message>(mesh.messages_[0].second, &ParseMessage);
    ASSERT_EQ(reply_message->message_type(), MessageUnion_Response);
    MessageHolder<Response> response =
        std::move(reply_message).TakeAndMap<Response>([](const Message* message) {
          return static_cast<const Response*>(message->message());
        });
    const NamespacePageId* response_namespace_page_id = response->namespace_page();
    EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->namespace_id()), "ledger");
    EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->page_id()), "page");
    EXPECT_EQ(response->response_type(), ResponseMessage_CommitResponse);

    // Send it to the other side.
    page_communicator_2.OnNewResponse(MakeP2PClientId(1u), std::move(response));
  }
  RunLoopUntilIdle();

  EXPECT_THAT(storage_2.commits_from_sync_, IsEmpty());
  // |page_communicator_2| should ask for the base, "id 0" commit.
  ASSERT_EQ(mesh.messages_.size(), 2u);
  EXPECT_EQ(mesh.messages_[1].first, MakeP2PClientId(1u));

  {
    MessageHolder<Message> request_message =
        *CreateMessageHolder<Message>(mesh.messages_[1].second, &ParseMessage);
    ASSERT_EQ(request_message->message_type(), MessageUnion_Request);
    MessageHolder<Request> request =
        std::move(request_message).TakeAndMap<Request>([](const Message* message) {
          return static_cast<const Request*>(message->message());
        });
    const NamespacePageId* request_namespace_page_id = request->namespace_page();
    EXPECT_EQ(convert::ExtendedStringView(request_namespace_page_id->namespace_id()), "ledger");
    EXPECT_EQ(convert::ExtendedStringView(request_namespace_page_id->page_id()), "page");
    EXPECT_EQ(request->request_type(), RequestMessage_CommitRequest);

    // Send it to the other side.
    page_communicator_1.OnNewRequest(MakeP2PClientId(2u), std::move(request));
  }
  RunLoopUntilIdle();

  // |page_communicator_1| sends commit "id 0" to device 2.
  ASSERT_EQ(mesh.messages_.size(), 3u);
  EXPECT_EQ(mesh.messages_[2].first, MakeP2PClientId(2u));

  {
    MessageHolder<Message> reply_message =
        *CreateMessageHolder<Message>(mesh.messages_[2].second, &ParseMessage);
    ASSERT_EQ(reply_message->message_type(), MessageUnion_Response);
    MessageHolder<Response> response =
        std::move(reply_message).TakeAndMap<Response>([](const Message* message) {
          return static_cast<const Response*>(message->message());
        });
    const NamespacePageId* response_namespace_page_id = response->namespace_page();
    EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->namespace_id()), "ledger");
    EXPECT_EQ(convert::ExtendedStringView(response_namespace_page_id->page_id()), "page");
    EXPECT_EQ(response->response_type(), ResponseMessage_CommitResponse);

    // Send it to the other side.
    page_communicator_2.OnNewResponse(MakeP2PClientId(1u), std::move(response));
  }
  RunLoopUntilIdle();

  // Verify that we are truely adding the whole commit batch.
  ASSERT_THAT(storage_2.commits_from_sync_, SizeIs(1));
  EXPECT_THAT(storage_2.commits_from_sync_[0].first,
              ElementsAre(MatchesCommitIdAndBytes("id 0", "data 0"),
                          MatchesCommitIdAndBytes("id 1", "data 1"),
                          MatchesCommitIdAndBytes("id 2", "data 2")));

  // Verify we don't crash on response from storage
  storage_2.commits_from_sync_[0].second(ledger::Status::OK);
}

class FakePageStorageDelayingMarkSyncedToPeer : public FakePageStorage {
 public:
  FakePageStorageDelayingMarkSyncedToPeer(async_dispatcher_t* dispatcher, std::string page_id)
      : FakePageStorage(dispatcher, page_id){};
  ~FakePageStorageDelayingMarkSyncedToPeer() override = default;

  void MarkSyncedToPeer(fit::function<void(ledger::Status)> callback) override {
    mark_synced_to_peer_callback_ = std::move(callback);
  }

  fit::function<void(ledger::Status)> mark_synced_to_peer_callback_;
};

// Check that we do not add commits from a peer to the storage until we have it as an interested
// peer.
TEST_F(PageCommunicatorImplTest, CommitBatchDelayedUntilPeerReady) {
  FakeDeviceMesh mesh;
  // Device 2 is already present.
  mesh.devices_.insert(MakeP2PClientId(2u));
  FakePageStorageDelayingMarkSyncedToPeer storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();
  // We send a watch request to device 2.
  ASSERT_EQ(mesh.messages_.size(), 1u);
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
      mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  const Message* message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(message->message_type(), MessageUnion_Request);
  const Request* request = static_cast<const Request*>(message->message());
  const NamespacePageId* namespace_page_id = request->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->page_id()), "page");
  EXPECT_EQ(request->request_type(), RequestMessage_WatchStartRequest);
  mesh.messages_.clear();

  // Device 2 sends a watch request in return.
  flatbuffers::FlatBufferBuilder watch_request_buffer;
  BuildWatchStartBuffer(&watch_request_buffer, "ledger", "page");
  MessageHolder<Message> watch_message =
      *CreateMessageHolder<Message>(convert::ToStringView(watch_request_buffer), &ParseMessage);
  page_communicator.OnNewRequest(
      MakeP2PClientId(2u), std::move(watch_message).TakeAndMap<Request>([](const Message* message) {
        return static_cast<const Request*>(message->message());
      }));
  RunLoopUntilIdle();
  mesh.messages_.clear();

  // Device 2 sends a commit.
  flatbuffers::FlatBufferBuilder commit_buffer;
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.emplace_back(std::make_unique<FakeCommit>("id", "data"));
  BuildCommitBuffer(&commit_buffer, "ledger", "page", std::move(commits));
  MessageHolder<Message> commit_message =
      *CreateMessageHolder<Message>(convert::ToStringView(commit_buffer), &ParseMessage);
  page_communicator.OnNewResponse(
      MakeP2PClientId(2u),
      std::move(commit_message).TakeAndMap<Response>([](const Message* message) {
        return static_cast<const Response*>(message->message());
      }));

  // The call to MarkSyncedToPeer is delayed. The commit is not added. GetObject returns not found
  // and does not post a message.
  ASSERT_TRUE(storage.mark_synced_to_peer_callback_);
  EXPECT_THAT(storage.commits_from_sync_, IsEmpty());

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::INTERNAL_NOT_FOUND);

  EXPECT_THAT(mesh.messages_, IsEmpty());

  // MarkSyncedToPeer proceeds.
  storage.mark_synced_to_peer_callback_(ledger::Status::OK);
  RunLoopUntilIdle();

  // We are sending our head.
  EXPECT_THAT(mesh.messages_, SizeIs(1));
  mesh.messages_.clear();

  // The commit is added.
  ASSERT_THAT(storage.commits_from_sync_, SizeIs(1));
  EXPECT_THAT(storage.commits_from_sync_[0].first,
              ElementsAre(MatchesCommitIdAndBytes("id", "data")));

  // Calling GetObject now sends a message to device 2.
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_THAT(mesh.messages_, SizeIs(1));
  EXPECT_EQ(mesh.messages_[0].first, MakeP2PClientId(2u));

  verifier =
      flatbuffers::Verifier(reinterpret_cast<const unsigned char*>(mesh.messages_[0].second.data()),
                            mesh.messages_[0].second.size());
  ASSERT_TRUE(VerifyMessageBuffer(verifier));

  // Verify the message sent to request the object.
  message = GetMessage(mesh.messages_[0].second.data());
  ASSERT_EQ(message->message_type(), MessageUnion_Request);
  request = static_cast<const Request*>(message->message());
  namespace_page_id = request->namespace_page();
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->namespace_id()), "ledger");
  EXPECT_EQ(convert::ExtendedStringView(namespace_page_id->page_id()), "page");
  EXPECT_EQ(request->request_type(), RequestMessage_ObjectRequest);
  const ObjectRequest* object_request = static_cast<const ObjectRequest*>(request->request());
  EXPECT_EQ(object_request->object_ids()->size(), 1u);
  EXPECT_EQ(object_request->object_ids()->begin()->key_index(), 0u);
  EXPECT_EQ(convert::ExtendedStringView(object_request->object_ids()->begin()->digest()), "foo");
}

// Removes a device while we are performing the GetObject call.
TEST_F(PageCommunicatorImplTest, GetObjectRemoveDevice) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  ConnectToDevice(&page_communicator, MakeP2PClientId(3u), "ledger", "page");
  ConnectToDevice(&page_communicator, MakeP2PClientId(4u), "ledger", "page");
  RunLoopUntilIdle();

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;

  mesh.OnNextSend(MakeP2PClientId(3u), [&page_communicator]() {
    page_communicator.OnDeviceChange(MakeP2PClientId(3u), p2p_provider::DeviceChangeType::DELETED);
  });

  page_communicator.GetObject(
      MakeObjectIdentifier("foo1"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));

  // The previous call to GetObject should return and not result in an
  // exception. Note that it is expected for GetObject callback to not be
  // called.
}

// Removes a device while we are performing the OnNewCommits call.
TEST_F(PageCommunicatorImplTest, OnNewCommitsRemoveDevice) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  ConnectToDevice(&page_communicator, MakeP2PClientId(3u), "ledger", "page");
  ConnectToDevice(&page_communicator, MakeP2PClientId(4u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  mesh.OnNextSend(MakeP2PClientId(3u), [&page_communicator]() {
    page_communicator.OnDeviceChange(MakeP2PClientId(3u), p2p_provider::DeviceChangeType::DELETED);
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
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  ConnectToDevice(&page_communicator, MakeP2PClientId(3u), "ledger", "page");
  ConnectToDevice(&page_communicator, MakeP2PClientId(4u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  mesh.OnNextSend(MakeP2PClientId(3u), [&page_communicator]() {
    page_communicator.OnDeviceChange(MakeP2PClientId(3u), p2p_provider::DeviceChangeType::DELETED);
  });

  // The destructor of PageCommunicatorImpl sends messages to connected devices.
  // This test succeeds if this destructor completes without throwing an
  // exception.
}

TEST_F(PageCommunicatorImplTest, GetObjectNoPeer) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  bool called;
  ledger::Status status = ledger::Status::NOT_IMPLEMENTED;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::INTERNAL_NOT_FOUND);

  // A second call for the same object also returns.
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::INTERNAL_NOT_FOUND);
}

// When a device disconnects, its pending object requests should be abandonned.
TEST_F(PageCommunicatorImplTest, GetObject_Disconnect) {
  FakeDeviceMesh mesh;
  FakePageStorage storage(dispatcher(), "page");
  PageCommunicatorImpl page_communicator(&environment_, &storage, &storage, "ledger", "page",
                                         &mesh);
  page_communicator.Start();

  ConnectToDevice(&page_communicator, MakeP2PClientId(2u), "ledger", "page");
  RunLoopUntilIdle();
  mesh.messages_.clear();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data;
  page_communicator.GetObject(
      MakeObjectIdentifier("foo"), storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced, &data));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  page_communicator.OnDeviceChange(MakeP2PClientId(2u), p2p_provider::DeviceChangeType::DELETED);

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::INTERNAL_NOT_FOUND);
}

}  // namespace
}  // namespace p2p_sync
