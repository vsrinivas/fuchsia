// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/page_communicator_impl.h"

#include <lib/callback/scoped_callback.h>
#include <lib/callback/waiter.h>
#include <lib/fit/function.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/p2p_sync/impl/message_generated.h"
#include "src/ledger/bin/storage/public/read_data_source.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace p2p_sync {
namespace {
storage::ObjectIdentifier ToObjectIdentifier(const ObjectId* fb_object_id) {
  uint32_t key_index = fb_object_id->key_index();
  uint32_t deletion_scope_id = fb_object_id->deletion_scope_id();
  return storage::ObjectIdentifier{
      key_index, deletion_scope_id,
      storage::ObjectDigest(fb_object_id->digest())};
}
}  // namespace

// PendingObjectRequestHolder holds state for object requests that have been
// sent to peers and for which we wait for an answer.
class PageCommunicatorImpl::PendingObjectRequestHolder {
 public:
  explicit PendingObjectRequestHolder(
      fit::function<void(storage::Status, storage::ChangeSource,
                         storage::IsObjectSynced,
                         std::unique_ptr<storage::DataSource::DataChunk>)>
          callback)
      : callback_(std::move(callback)) {}

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

  // Registers a new pending request to device |destination|.
  void AddNewPendingRequest(std::string destination) {
    requests_.emplace(std::move(destination));
  }

  // Processes the response from device |source|.
  void Complete(fxl::StringView source, const Object* object) {
    auto it = requests_.find(source);
    if (it == requests_.end()) {
      return;
    }
    if (object == nullptr || object->status() == ObjectStatus_UNKNOWN_OBJECT) {
      requests_.erase(it);
      if (!requests_.empty()) {
        return;
      }
      // All requests have returned and none is valid: return an error.
      callback_(storage::Status::INTERNAL_NOT_FOUND, storage::ChangeSource::P2P,
                storage::IsObjectSynced::NO, nullptr);
      if (on_empty_) {
        on_empty_();
      }
      return;
    }

    std::unique_ptr<storage::DataSource::DataChunk> chunk =
        storage::DataSource::DataChunk::Create(
            convert::ToString(object->data()->bytes()));
    storage::IsObjectSynced is_object_synced;
    switch (object->sync_status()) {
      case ObjectSyncStatus_UNSYNCED:
        is_object_synced = storage::IsObjectSynced::NO;
        break;
      case ObjectSyncStatus_SYNCED_TO_CLOUD:
        is_object_synced = storage::IsObjectSynced::YES;
        break;
    }
    callback_(storage::Status::OK, storage::ChangeSource::P2P, is_object_synced,
              std::move(chunk));
    if (on_empty_) {
      on_empty_();
    }
  }

 private:
  fit::function<void(
      storage::Status, storage::ChangeSource, storage::IsObjectSynced,
      std::unique_ptr<storage::DataSource::DataChunk>)> const callback_;
  // Set of devices for which we are waiting an answer.
  // We might be able to get rid of this list and just use a counter (or even
  // nothing at all) once we have a timeout on requests.
  std::set<std::string, convert::StringViewComparator> requests_;
  fit::closure on_empty_;
};

// ObjectResponseHolder holds temporary data we collect in order to build
// ObjectResponses.
// This is necessary as object data (from |storage::Object|) and synchronization
// data come from different asynchronous calls.
struct PageCommunicatorImpl::ObjectResponseHolder {
  storage::ObjectIdentifier identifier;
  std::unique_ptr<const storage::Piece> piece;
  bool is_synced = false;

  explicit ObjectResponseHolder(storage::ObjectIdentifier identifier)
      : identifier(std::move(identifier)) {}
};

PageCommunicatorImpl::PageCommunicatorImpl(
    coroutine::CoroutineService* coroutine_service,
    storage::PageStorage* storage, storage::PageSyncClient* sync_client,
    std::string namespace_id, std::string page_id, DeviceMesh* mesh)
    : coroutine_manager_(coroutine_service),
      namespace_id_(std::move(namespace_id)),
      page_id_(std::move(page_id)),
      mesh_(mesh),
      storage_(storage),
      sync_client_(sync_client),
      weak_factory_(this) {}

PageCommunicatorImpl::~PageCommunicatorImpl() {
  FXL_DCHECK(!in_destructor_);
  in_destructor_ = true;

  flatbuffers::FlatBufferBuilder buffer;
  if (!started_) {
    if (on_delete_) {
      on_delete_();
    }
    return;
  }

  BuildWatchStopBuffer(&buffer);
  for (const auto& device : interested_devices_) {
    mesh_->Send(device, buffer);
  }

  if (on_delete_) {
    on_delete_();
  }
}

void PageCommunicatorImpl::Start() {
  FXL_DCHECK(!started_);
  started_ = true;
  sync_client_->SetSyncDelegate(this);
  storage_->AddCommitWatcher(this);

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer);

  for (const auto& device : mesh_->GetDeviceList()) {
    mesh_->Send(device, buffer);
  }
}

void PageCommunicatorImpl::set_on_delete(fit::closure on_delete) {
  FXL_DCHECK(!on_delete_) << "set_on_delete() can only be called once.";
  on_delete_ = std::move(on_delete);
}

void PageCommunicatorImpl::OnDeviceChange(
    fxl::StringView remote_device, p2p_provider::DeviceChangeType change_type) {
  if (!started_ || in_destructor_) {
    return;
  }

  if (change_type == p2p_provider::DeviceChangeType::DELETED) {
    const auto& it = interested_devices_.find(remote_device);
    if (it != interested_devices_.end()) {
      interested_devices_.erase(it);
    }
    const auto& it2 = not_interested_devices_.find(remote_device);
    if (it2 != not_interested_devices_.end()) {
      not_interested_devices_.erase(it2);
    }
    const auto& it3 = pending_commit_batches_.find(remote_device);
    if (it3 != pending_commit_batches_.end()) {
      pending_commit_batches_.erase(it3);
    }
    return;
  }

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer);
  mesh_->Send(remote_device, buffer);
}

void PageCommunicatorImpl::OnNewRequest(fxl::StringView source,
                                        MessageHolder<Request> message) {
  FXL_DCHECK(!in_destructor_);
  switch (message->request_type()) {
    case RequestMessage_WatchStartRequest: {
      MarkSyncedToPeer(
          [this, source = source.ToString()](storage::Status status) {
            if (status != storage::Status::OK) {
              // If we fail to mark the page storage as synced to a peer, we
              // might end up in a situation of deleting from disk a partially
              // synced page. Log an error and return.
              FXL_LOG(ERROR) << "Failed to mark PageStorage as synced to peer";
              return;
            }
            if (interested_devices_.find(source) == interested_devices_.end()) {
              interested_devices_.insert(source);
            }
            auto it = not_interested_devices_.find(source);
            if (it != not_interested_devices_.end()) {
              // The device used to be uninterested, but now wants updates.
              // Let's contact it again.
              not_interested_devices_.erase(it);
              flatbuffers::FlatBufferBuilder buffer;
              BuildWatchStartBuffer(&buffer);
              mesh_->Send(source, buffer);
            }
          });
      break;
    }
    case RequestMessage_WatchStopRequest: {
      const auto& it = interested_devices_.find(source);
      if (it != interested_devices_.end()) {
        interested_devices_.erase(it);
      }
      // Device |source| disconnected, thus will not answer any request. We thus
      // mark all pending requests to |source| to be finished.
      std::vector<PendingObjectRequestHolder*> requests;
      requests.reserve(pending_object_requests_.size());
      for (auto& object_request : pending_object_requests_) {
        // We cannot call Complete here because it deletes the object, making
        // the iterator used in this for loop invalid.
        requests.push_back(&object_request.second);
      }
      for (PendingObjectRequestHolder* const request : requests) {
        request->Complete(source, nullptr);
      }
      break;
    }
    case RequestMessage_CommitRequest:
      ProcessCommitRequest(
          source.ToString(),
          std::move(message).TakeAndMap<CommitRequest>(
              [](const Request* request) {
                return static_cast<const CommitRequest*>(request->request());
              }));
      break;
    case RequestMessage_ObjectRequest:
      ProcessObjectRequest(
          source,
          std::move(message).TakeAndMap<ObjectRequest>(
              [](const Request* request) {
                return static_cast<const ObjectRequest*>(request->request());
              }));
      break;
    case RequestMessage_NONE:
      FXL_LOG(ERROR) << "The message received is malformed";
      break;
  }
}

void PageCommunicatorImpl::OnNewResponse(fxl::StringView source,
                                         MessageHolder<Response> message) {
  FXL_DCHECK(!in_destructor_);
  if (message->status() != ResponseStatus_OK) {
    // The namespace or page was unknown on the other side. We can probably do
    // something smart with this information (for instance, stop sending
    // requests over), but we just ignore it for now.
    not_interested_devices_.emplace(source.ToString());
    return;
  }
  switch (message->response_type()) {
    case ResponseMessage_ObjectResponse: {
      const ObjectResponse* object_response =
          static_cast<const ObjectResponse*>(message->response());
      for (const Object* object : *(object_response->objects())) {
        auto object_id = ToObjectIdentifier(object->id());
        auto pending_request = pending_object_requests_.find(object_id);
        if (pending_request == pending_object_requests_.end()) {
          continue;
        }
        pending_request->second.Complete(source, object);
      }
      break;
    }
    case ResponseMessage_CommitResponse: {
      const CommitResponse* commit_response =
          static_cast<const CommitResponse*>(message->response());
      std::vector<storage::PageStorage::CommitIdAndBytes> commits;
      for (const Commit* commit : *(commit_response->commits())) {
        if (commit->status() != CommitStatus_OK) {
          continue;
        }
        commits.emplace_back(convert::ToString(commit->id()->id()),
                             convert::ToString(commit->commit()->bytes()));
      }
      auto it = pending_commit_batches_.find(source);
      if (it != pending_commit_batches_.end()) {
        it->second.AddToBatch(std::move(commits));
      } else {
        auto it_pair = pending_commit_batches_.emplace(
            std::piecewise_construct, std::forward_as_tuple(source.ToString()),
            std::forward_as_tuple(source.ToString(), this, storage_));
        it_pair.first->second.AddToBatch(std::move(commits));
      }
      break;
    }

    case ResponseMessage_NONE:
      FXL_LOG(ERROR) << "The message received is malformed";
      return;
  }
}

void PageCommunicatorImpl::GetObject(
    storage::ObjectIdentifier object_identifier,
    fit::function<void(storage::Status, storage::ChangeSource,
                       storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  flatbuffers::FlatBufferBuilder buffer;

  BuildObjectRequestBuffer(&buffer, object_identifier);

  auto request_holder = pending_object_requests_.emplace(
      std::move(object_identifier), std::move(callback));

  for (const auto& device : interested_devices_) {
    request_holder.first->second.AddNewPendingRequest(device);
  }
  for (const auto& device : interested_devices_) {
    mesh_->Send(device, buffer);
  }
}

void PageCommunicatorImpl::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& commits,
    storage::ChangeSource source) {
  if (source != storage::ChangeSource::LOCAL) {
    // Don't propagate synced commits.
    return;
  }
  for (const auto& commit : commits) {
    commits_to_upload_.emplace_back(commit->Clone());
  }
  // We need to check if we need to merge first.
  std::vector<std::unique_ptr<const storage::Commit>> head_commits;
  storage::Status status = storage_->GetHeadCommits(&head_commits);
  if (status != storage::Status::OK) {
    return;
  }
  if (head_commits.size() != 1) {
    // A merge needs to happen, let's wait until we
    // have one.
    return;
  }
  if (commits_to_upload_.empty()) {
    // Commits have already been sent. Let's stop
    // early.
    return;
  }
  flatbuffers::FlatBufferBuilder buffer;
  BuildCommitBuffer(&buffer, commits_to_upload_);

  for (const auto& device : interested_devices_) {
    mesh_->Send(device, buffer);
  }
  commits_to_upload_.clear();
}

void PageCommunicatorImpl::RequestCommits(fxl::StringView device,
                                          std::vector<storage::CommitId> ids) {
  flatbuffers::FlatBufferBuilder buffer;
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(buffer,
                            convert::ToFlatBufferVector(&buffer, namespace_id_),
                            convert::ToFlatBufferVector(&buffer, page_id_));
  std::vector<flatbuffers::Offset<CommitId>> commit_ids;
  for (const auto& id : ids) {
    flatbuffers::Offset<CommitId> commit_id =
        CreateCommitId(buffer, convert::ToFlatBufferVector(&buffer, id));
    commit_ids.push_back(commit_id);
  }
  flatbuffers::Offset<CommitRequest> commit_request =
      CreateCommitRequest(buffer, buffer.CreateVector(commit_ids));
  flatbuffers::Offset<Request> request =
      CreateRequest(buffer, namespace_page_id, RequestMessage_CommitRequest,
                    commit_request.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(buffer, MessageUnion_Request, request.Union());
  buffer.Finish(message);
  mesh_->Send(device, buffer);
}

void PageCommunicatorImpl::BuildWatchStartBuffer(
    flatbuffers::FlatBufferBuilder* buffer) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_WatchStartRequest);
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void PageCommunicatorImpl::BuildWatchStopBuffer(
    flatbuffers::FlatBufferBuilder* buffer) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_WatchStopRequest);
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void PageCommunicatorImpl::BuildObjectRequestBuffer(
    flatbuffers::FlatBufferBuilder* buffer,
    storage::ObjectIdentifier object_identifier) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  flatbuffers::Offset<ObjectId> object_id = CreateObjectId(
      *buffer, object_identifier.key_index(),
      object_identifier.deletion_scope_id(),
      convert::ToFlatBufferVector(
          buffer, object_identifier.object_digest().Serialize()));
  flatbuffers::Offset<ObjectRequest> object_request = CreateObjectRequest(
      *buffer, buffer->CreateVector(
                   std::vector<flatbuffers::Offset<ObjectId>>({object_id})));
  flatbuffers::Offset<Request> request =
      CreateRequest(*buffer, namespace_page_id, RequestMessage_ObjectRequest,
                    object_request.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void PageCommunicatorImpl::BuildCommitBuffer(
    flatbuffers::FlatBufferBuilder* buffer,
    const std::vector<std::unique_ptr<const storage::Commit>>& commits) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  std::vector<flatbuffers::Offset<Commit>> fb_commits;
  for (const auto& commit : commits) {
    flatbuffers::Offset<CommitId> fb_commit_id = CreateCommitId(
        *buffer, convert::ToFlatBufferVector(buffer, commit->GetId()));
    flatbuffers::Offset<Data> fb_commit_data = CreateData(
        *buffer,
        convert::ToFlatBufferVector(buffer, commit->GetStorageBytes()));
    fb_commits.emplace_back(
        CreateCommit(*buffer, fb_commit_id, CommitStatus_OK, fb_commit_data));
  }

  flatbuffers::Offset<CommitResponse> commit_response =
      CreateCommitResponse(*buffer, buffer->CreateVector(fb_commits));
  flatbuffers::Offset<Response> response =
      CreateResponse(*buffer, ResponseStatus_OK, namespace_page_id,
                     ResponseMessage_CommitResponse, commit_response.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Response, response.Union());
  buffer->Finish(message);
}

void PageCommunicatorImpl::BuildCommitResponseBuffer(
    flatbuffers::FlatBufferBuilder* buffer,
    const std::vector<
        std::pair<storage::CommitId, std::unique_ptr<const storage::Commit>>>&
        commits) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  std::vector<flatbuffers::Offset<Commit>> fb_commits;
  for (const auto& commit : commits) {
    flatbuffers::Offset<CommitId> fb_commit_id = CreateCommitId(
        *buffer, convert::ToFlatBufferVector(buffer, commit.first));
    if (commit.second) {
      flatbuffers::Offset<Data> fb_commit_data =
          CreateData(*buffer, convert::ToFlatBufferVector(
                                  buffer, commit.second->GetStorageBytes()));
      fb_commits.emplace_back(
          CreateCommit(*buffer, fb_commit_id, CommitStatus_OK, fb_commit_data));
    } else {
      fb_commits.emplace_back(
          CreateCommit(*buffer, fb_commit_id, CommitStatus_UNKNOWN_COMMIT));
    }
  }

  flatbuffers::Offset<CommitResponse> commit_response =
      CreateCommitResponse(*buffer, buffer->CreateVector(fb_commits));
  flatbuffers::Offset<Response> response =
      CreateResponse(*buffer, ResponseStatus_OK, namespace_page_id,
                     ResponseMessage_CommitResponse, commit_response.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Response, response.Union());
  buffer->Finish(message);
}

void PageCommunicatorImpl::ProcessCommitRequest(
    std::string source, MessageHolder<CommitRequest> request) {
  coroutine_manager_.StartCoroutine([this, source = std::move(source),
                                     request = std::move(request)](
                                        coroutine::CoroutineHandler* handler) {
    auto commit_waiter = fxl::MakeRefCounted<callback::Waiter<
        storage::Status,
        std::pair<storage::CommitId, std::unique_ptr<const storage::Commit>>>>(
        storage::Status::OK);
    for (const CommitId* id : *request->commit_ids()) {
      storage_->GetCommit(
          id->id(), [commit_id = convert::ToString(id->id()),
                     callback = commit_waiter->NewCallback()](
                        storage::Status status,
                        std::unique_ptr<const storage::Commit> commit) mutable {
            if (status == storage::Status::INTERNAL_NOT_FOUND) {
              // Not finding an commit is okay in this context: we'll just
              // reply we don't have it. There is not need to abort
              // processing the request.
              callback(storage::Status::OK,
                       std::make_pair(std::move(commit_id), nullptr));
              return;
            }
            callback(status,
                     std::make_pair(std::move(commit_id), std::move(commit)));
          });
    }
    storage::Status status;
    std::vector<
        std::pair<storage::CommitId, std::unique_ptr<const storage::Commit>>>
        commits;
    if (coroutine::Wait(handler, std::move(commit_waiter), &status, &commits) ==
        coroutine::ContinuationStatus::INTERRUPTED) {
      return;
    }

    if (status != storage::Status::OK) {
      return;
    }

    flatbuffers::FlatBufferBuilder buffer;
    BuildCommitResponseBuffer(&buffer, commits);
    mesh_->Send(source, buffer);
  });
}

void PageCommunicatorImpl::ProcessObjectRequest(
    fxl::StringView source, MessageHolder<ObjectRequest> request) {
  coroutine_manager_.StartCoroutine([this, source = source.ToString(),
                                     request = std::move(request)](
                                        coroutine::CoroutineHandler* handler) {
    // We use a std::list so that we can keep a reference to an element
    // while adding new items.
    std::list<ObjectResponseHolder> object_responses;
    auto response_waiter =
        fxl::MakeRefCounted<callback::StatusWaiter<storage::Status>>(
            storage::Status::OK);
    for (const ObjectId* object_id : *request->object_ids()) {
      storage::ObjectIdentifier identifier{
          object_id->key_index(), object_id->deletion_scope_id(),
          storage::ObjectDigest(object_id->digest())};
      object_responses.emplace_back(identifier);
      auto& response = object_responses.back();
      storage_->GetPiece(
          identifier, [callback = response_waiter->NewCallback(), &response](
                          storage::Status status,
                          std::unique_ptr<const storage::Piece> piece) mutable {
            if (status == storage::Status::INTERNAL_NOT_FOUND) {
              // Not finding an object is okay in this context: we'll just
              // reply we don't have it. There is not need to abort
              // processing the request.
              callback(storage::Status::OK);
              return;
            }
            response.piece = std::move(piece);
            callback(status);
          });
      storage_->IsPieceSynced(
          std::move(identifier),
          [callback = response_waiter->NewCallback(), &response](
              storage::Status status, bool is_synced) {
            if (status == storage::Status::INTERNAL_NOT_FOUND) {
              // Not finding an object is okay in this context: we'll just
              // reply we don't have it. There is not need to abort
              // processing the request.
              callback(storage::Status::OK);
              return;
            }
            response.is_synced = is_synced;
            callback(status);
          });
    }

    storage::Status status;
    if (coroutine::Wait(handler, response_waiter, &status) ==
        coroutine::ContinuationStatus::INTERRUPTED) {
      return;
    }

    if (status != storage::Status::OK) {
      FXL_LOG(WARNING) << "Error while retrieving objects: " << status;
      return;
    }

    flatbuffers::FlatBufferBuilder buffer;
    BuildObjectResponseBuffer(&buffer, std::move(object_responses));

    mesh_->Send(source, buffer);
  });
}

void PageCommunicatorImpl::BuildObjectResponseBuffer(
    flatbuffers::FlatBufferBuilder* buffer,
    std::list<ObjectResponseHolder> object_responses) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer,
                            convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  std::vector<flatbuffers::Offset<Object>> fb_objects;
  for (const ObjectResponseHolder& object_response : object_responses) {
    flatbuffers::Offset<ObjectId> fb_object_id = CreateObjectId(
        *buffer, object_response.identifier.key_index(),
        object_response.identifier.deletion_scope_id(),
        convert::ToFlatBufferVector(
            buffer, object_response.identifier.object_digest().Serialize()));
    if (object_response.piece) {
      fxl::StringView data = object_response.piece->GetData();
      flatbuffers::Offset<Data> fb_data =
          CreateData(*buffer, convert::ToFlatBufferVector(buffer, data));
      ObjectSyncStatus sync_status = object_response.is_synced
                                         ? ObjectSyncStatus_SYNCED_TO_CLOUD
                                         : ObjectSyncStatus_UNSYNCED;
      fb_objects.emplace_back(CreateObject(
          *buffer, fb_object_id, ObjectStatus_OK, fb_data, sync_status));
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

void PageCommunicatorImpl::MarkSyncedToPeer(
    fit::function<void(storage::Status)> callback) {
  if (marked_as_synced_to_peer_) {
    callback(storage::Status::OK);
    return;
  }
  storage_->MarkSyncedToPeer(
      [this, callback = std::move(callback)](storage::Status status) {
        if (status == storage::Status::OK) {
          marked_as_synced_to_peer_ = true;
        }
        callback(status);
      });
}

}  // namespace p2p_sync
