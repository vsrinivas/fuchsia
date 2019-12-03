// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/page_communicator_impl.h"

#include <lib/fit/function.h>

#include "flatbuffers/flatbuffers.h"
#include "src/ledger/bin/p2p_sync/impl/message_generated.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/read_data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace p2p_sync {
namespace {
storage::ObjectIdentifier ToObjectIdentifier(const ObjectId* fb_object_id,
                                             storage::PageStorage* storage) {
  uint32_t key_index = fb_object_id->key_index();
  return storage->GetObjectIdentifierFactory()->MakeObjectIdentifier(
      key_index, storage::ObjectDigest(fb_object_id->digest()));
}
}  // namespace

// PendingObjectRequestHolder holds state for object requests that have been
// sent to peers and for which we wait for an answer.
class PageCommunicatorImpl::PendingObjectRequestHolder {
 public:
  PendingObjectRequestHolder() = default;

  void SetOnDiscardable(fit::closure on_discardable) {
    on_discardable_ = std::move(on_discardable);
  }

  bool IsDiscardable() const { return response_ || !requests_.empty(); }

  // Registers this additional callback for the request, or reply immediately if
  // |Complete| has already been called and there is a cached response.
  void AddCallback(
      fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                         std::unique_ptr<storage::DataSource::DataChunk>)>
          callback) {
    if (response_) {
      SendResponse(std::move(callback));
      return;
    }
    callbacks_.push_back(std::move(callback));
  }

  // Registers a new pending request to device |destination|.
  void AddNewPendingRequest(p2p_provider::P2PClientId destination) {
    requests_.emplace(std::move(destination));
  }

  // Processes the response from device |source|. Caches the response in
  // |response_| and sends it to all registered callbacks.
  void Complete(const p2p_provider::P2PClientId& source, const Object* object) {
    if (response_) {
      // Another device already answered, the result is not useful anymore.
      return;
    }
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
      response_ = {ledger::Status::INTERNAL_NOT_FOUND, storage::IsObjectSynced::NO, ""};
      auto callbacks = std::move(callbacks_);
      callbacks_.clear();
      for (auto& callback : callbacks) {
        SendResponse(std::move(callback));
      }
      if (on_discardable_) {
        on_discardable_();
      }
      return;
    }

    storage::IsObjectSynced is_object_synced;
    switch (object->sync_status()) {
      case ObjectSyncStatus_UNSYNCED:
        is_object_synced = storage::IsObjectSynced::NO;
        break;
      case ObjectSyncStatus_SYNCED_TO_CLOUD:
        is_object_synced = storage::IsObjectSynced::YES;
        break;
    }
    response_ = {ledger::Status::OK, is_object_synced, convert::ToString(object->data()->bytes())};
    auto callbacks = std::move(callbacks_);
    callbacks_.clear();
    for (auto& callback : callbacks) {
      SendResponse(std::move(callback));
    }
    if (on_discardable_) {
      on_discardable_();
    }
  }

 private:
  struct Response {
    ledger::Status status;
    storage::IsObjectSynced is_object_synced;
    std::string data;
  };

  void SendResponse(
      fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                         std::unique_ptr<storage::DataSource::DataChunk>)>
          callback) {
    FXL_DCHECK(response_);
    callback(response_->status, storage::ChangeSource::P2P, response_->is_object_synced,
             response_->status == ledger::Status::OK
                 ? storage::DataSource::DataChunk::Create(response_->data)
                 : nullptr);
  }

  std::vector<fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                                 std::unique_ptr<storage::DataSource::DataChunk>)>>
      callbacks_;
  // Set of devices for which we are waiting an answer.
  // We might be able to get rid of this list and just use a counter (or even
  // nothing at all) once we have a timeout on requests.
  std::set<p2p_provider::P2PClientId> requests_;
  fit::closure on_discardable_;
  std::optional<Response> response_;
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

PageCommunicatorImpl::PageCommunicatorImpl(ledger::Environment* environment,
                                           storage::PageStorage* storage,
                                           storage::PageSyncClient* sync_client,
                                           std::string namespace_id, std::string page_id,
                                           DeviceMesh* mesh)
    : pending_object_requests_(environment->dispatcher()),
      pending_commit_batches_(environment->dispatcher()),
      coroutine_manager_(environment->coroutine_service()),
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
  SendToInterestedDevices(buffer);

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

  // DeviceMesh::GetDeviceList makes a copy, so we can iterate without issue
  // here.
  for (const auto& device : mesh_->GetDeviceList()) {
    mesh_->Send(device, buffer);
  }
}

void PageCommunicatorImpl::set_on_delete(fit::closure on_delete) {
  FXL_DCHECK(!on_delete_) << "set_on_delete() can only be called once.";
  on_delete_ = std::move(on_delete);
}

void PageCommunicatorImpl::OnDeviceChange(const p2p_provider::P2PClientId& remote_device,
                                          p2p_provider::DeviceChangeType change_type) {
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

    // Remove pending requests from the device: it disconnected, it is not going
    // to answer.
    for (auto it = pending_object_requests_.begin(); it != pending_object_requests_.end();) {
      // |Complete| may delete the request, invalidating the iterator. We
      // increment and make a copy to be able to continue to iterate.
      auto request = it++;
      request->second.Complete(remote_device, nullptr);
    }
    return;
  }

  flatbuffers::FlatBufferBuilder buffer;
  BuildWatchStartBuffer(&buffer);
  mesh_->Send(remote_device, buffer);
}

void PageCommunicatorImpl::OnNewRequest(const p2p_provider::P2PClientId& source,
                                        MessageHolder<Request> message) {
  FXL_DCHECK(!in_destructor_);
  switch (message->request_type()) {
    case RequestMessage_WatchStartRequest: {
      MarkSyncedToPeer([this, source](ledger::Status status) {
        if (status != ledger::Status::OK) {
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

        // If we have a single head commit, send it over so the peer can start downloading its
        // backlog.
        SendHead(source);

        // Now that the client is marked as interested, process the commits it pushed.
        auto batch_it = pending_commit_batches_.find(source);
        if (batch_it != pending_commit_batches_.end()) {
          batch_it->second.MarkPeerReady();
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
      ProcessCommitRequest(source,
                           std::move(message).TakeAndMap<CommitRequest>([](const Request* request) {
                             return static_cast<const CommitRequest*>(request->request());
                           }));
      break;
    case RequestMessage_ObjectRequest:
      ProcessObjectRequest(source,
                           std::move(message).TakeAndMap<ObjectRequest>([](const Request* request) {
                             return static_cast<const ObjectRequest*>(request->request());
                           }));
      break;
    case RequestMessage_NONE:
      FXL_LOG(ERROR) << "The message received is malformed";
      break;
  }
}

void PageCommunicatorImpl::OnNewResponse(const p2p_provider::P2PClientId& source,
                                         MessageHolder<Response> message) {
  FXL_DCHECK(!in_destructor_);
  if (message->status() != ResponseStatus_OK) {
    // The namespace or page was unknown on the other side. We can probably do
    // something smart with this information (for instance, stop sending
    // requests over), but we just ignore it for now.
    not_interested_devices_.emplace(source);
    return;
  }
  switch (message->response_type()) {
    case ResponseMessage_ObjectResponse: {
      const ObjectResponse* object_response =
          static_cast<const ObjectResponse*>(message->response());
      for (const Object* object : *(object_response->objects())) {
        auto object_id = ToObjectIdentifier(object->id(), storage_);
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
        auto it_pair = pending_commit_batches_.try_emplace(source, source, this, storage_);
        it_pair.first->second.AddToBatch(std::move(commits));
        if (interested_devices_.find(source) != interested_devices_.end()) {
          it_pair.first->second.MarkPeerReady();
        }
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
    storage::RetrievedObjectType /*retrieved_object_type*/,
    fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                       std::unique_ptr<storage::DataSource::DataChunk>)>
        callback) {
  if (interested_devices_.empty()) {
    callback(ledger::Status::INTERNAL_NOT_FOUND, storage::ChangeSource::P2P,
             storage::IsObjectSynced::NO, nullptr);
    return;
  }

  auto [request_holder, is_new_request] = pending_object_requests_.try_emplace(object_identifier);
  request_holder->second.AddCallback(std::move(callback));

  if (is_new_request) {
    flatbuffers::FlatBufferBuilder buffer;
    BuildObjectRequestBuffer(&buffer, std::move(object_identifier));

    for (const auto& device : interested_devices_) {
      request_holder->second.AddNewPendingRequest(device);
    }

    SendToInterestedDevices(buffer);
  }
}

void PageCommunicatorImpl::GetDiff(
    storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases,
    fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
        callback) {
  FXL_NOTIMPLEMENTED();
  callback(ledger::Status::NOT_IMPLEMENTED, {}, {});
}

void PageCommunicatorImpl::UpdateClock(storage::Clock /*clock*/,
                                       fit::function<void(ledger::Status)> /*callback*/) {
  FXL_NOTIMPLEMENTED();
};

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
  ledger::Status status = storage_->GetHeadCommits(&head_commits);
  if (status != ledger::Status::OK) {
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

  SendToInterestedDevices(buffer);
  commits_to_upload_.clear();
}

void PageCommunicatorImpl::RequestCommits(const p2p_provider::P2PClientId& device,
                                          std::vector<storage::CommitId> ids) {
  flatbuffers::FlatBufferBuilder buffer;
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(buffer, convert::ToFlatBufferVector(&buffer, namespace_id_),
                            convert::ToFlatBufferVector(&buffer, page_id_));
  std::vector<flatbuffers::Offset<CommitId>> commit_ids;
  for (const auto& id : ids) {
    flatbuffers::Offset<CommitId> commit_id =
        CreateCommitId(buffer, convert::ToFlatBufferVector(&buffer, id));
    commit_ids.push_back(commit_id);
  }
  flatbuffers::Offset<CommitRequest> commit_request =
      CreateCommitRequest(buffer, buffer.CreateVector(commit_ids));
  flatbuffers::Offset<Request> request = CreateRequest(
      buffer, namespace_page_id, RequestMessage_CommitRequest, commit_request.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(buffer, MessageUnion_Request, request.Union());
  buffer.Finish(message);
  mesh_->Send(device, buffer);
}

void PageCommunicatorImpl::BuildWatchStartBuffer(flatbuffers::FlatBufferBuilder* buffer) {
  flatbuffers::Offset<WatchStartRequest> watch_start = CreateWatchStartRequest(*buffer);
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_WatchStartRequest, watch_start.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void PageCommunicatorImpl::BuildWatchStopBuffer(flatbuffers::FlatBufferBuilder* buffer) {
  flatbuffers::Offset<WatchStopRequest> watch_stop = CreateWatchStopRequest(*buffer);
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_WatchStopRequest, watch_stop.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void PageCommunicatorImpl::BuildObjectRequestBuffer(flatbuffers::FlatBufferBuilder* buffer,
                                                    storage::ObjectIdentifier object_identifier) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  flatbuffers::Offset<ObjectId> object_id = CreateObjectId(
      *buffer, object_identifier.key_index(),
      convert::ToFlatBufferVector(buffer, object_identifier.object_digest().Serialize()));
  flatbuffers::Offset<ObjectRequest> object_request = CreateObjectRequest(
      *buffer, buffer->CreateVector(std::vector<flatbuffers::Offset<ObjectId>>({object_id})));
  flatbuffers::Offset<Request> request = CreateRequest(
      *buffer, namespace_page_id, RequestMessage_ObjectRequest, object_request.Union());
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Request, request.Union());
  buffer->Finish(message);
}

void PageCommunicatorImpl::BuildCommitBuffer(
    flatbuffers::FlatBufferBuilder* buffer,
    const std::vector<std::unique_ptr<const storage::Commit>>& commits) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
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

void PageCommunicatorImpl::BuildCommitResponseBuffer(
    flatbuffers::FlatBufferBuilder* buffer,
    const std::vector<std::pair<storage::CommitId, std::unique_ptr<const storage::Commit>>>&
        commits) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  std::vector<flatbuffers::Offset<Commit>> fb_commits;
  for (const auto& commit : commits) {
    flatbuffers::Offset<CommitId> fb_commit_id =
        CreateCommitId(*buffer, convert::ToFlatBufferVector(buffer, commit.first));
    if (commit.second) {
      flatbuffers::Offset<Data> fb_commit_data = CreateData(
          *buffer, convert::ToFlatBufferVector(buffer, commit.second->GetStorageBytes()));
      fb_commits.emplace_back(CreateCommit(*buffer, fb_commit_id, CommitStatus_OK, fb_commit_data));
    } else {
      fb_commits.emplace_back(CreateCommit(*buffer, fb_commit_id, CommitStatus_UNKNOWN_COMMIT));
    }
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

void PageCommunicatorImpl::ProcessCommitRequest(p2p_provider::P2PClientId source,
                                                MessageHolder<CommitRequest> request) {
  coroutine_manager_.StartCoroutine(
      [this, source = std::move(source),
       request = std::move(request)](coroutine::CoroutineHandler* handler) {
        auto commit_waiter = fxl::MakeRefCounted<callback::Waiter<
            ledger::Status, std::pair<storage::CommitId, std::unique_ptr<const storage::Commit>>>>(
            ledger::Status::OK);
        for (const CommitId* id : *request->commit_ids()) {
          storage_->GetCommit(
              id->id(),
              [commit_id = convert::ToString(id->id()), callback = commit_waiter->NewCallback()](
                  ledger::Status status, std::unique_ptr<const storage::Commit> commit) mutable {
                if (status == ledger::Status::INTERNAL_NOT_FOUND) {
                  // Not finding an commit is okay in this context: we'll just
                  // reply we don't have it. There is not need to abort
                  // processing the request.
                  callback(ledger::Status::OK, std::make_pair(std::move(commit_id), nullptr));
                  return;
                }
                callback(status, std::make_pair(std::move(commit_id), std::move(commit)));
              });
        }
        ledger::Status status;
        std::vector<std::pair<storage::CommitId, std::unique_ptr<const storage::Commit>>> commits;
        if (coroutine::Wait(handler, std::move(commit_waiter), &status, &commits) ==
            coroutine::ContinuationStatus::INTERRUPTED) {
          return;
        }

        if (status != ledger::Status::OK) {
          return;
        }

        flatbuffers::FlatBufferBuilder buffer;
        BuildCommitResponseBuffer(&buffer, commits);
        mesh_->Send(source, buffer);
      });
}

void PageCommunicatorImpl::ProcessObjectRequest(p2p_provider::P2PClientId source,
                                                MessageHolder<ObjectRequest> request) {
  coroutine_manager_.StartCoroutine(
      [this, source, request = std::move(request)](coroutine::CoroutineHandler* handler) {
        // We use a std::list so that we can keep a reference to an element
        // while adding new items.
        std::list<ObjectResponseHolder> object_responses;
        auto response_waiter =
            fxl::MakeRefCounted<callback::StatusWaiter<ledger::Status>>(ledger::Status::OK);
        for (const ObjectId* object_id : *request->object_ids()) {
          storage::ObjectIdentifier identifier = ToObjectIdentifier(object_id, storage_);
          // The identifier held by |object_responses| ensures that the piece will not be
          // garbage-collected while we try to retrieve it along with its synchronisation status
          // (therefore, if one of the calls returns NOT_FOUND, it means the piece was missing
          // before we tried to get it, not in-between due to a race condition).
          object_responses.emplace_back(identifier);
          auto& response = object_responses.back();
          storage_->GetPiece(identifier, [callback = response_waiter->NewCallback(), &response](
                                             ledger::Status status,
                                             std::unique_ptr<const storage::Piece> piece) mutable {
            if (status == ledger::Status::INTERNAL_NOT_FOUND) {
              // Not finding an object is okay in this context: we'll just
              // reply we don't have it. There is not need to abort
              // processing the request.
              callback(ledger::Status::OK);
              return;
            }
            response.piece = std::move(piece);
            callback(status);
          });
          storage_->IsPieceSynced(std::move(identifier),
                                  [callback = response_waiter->NewCallback(), &response](
                                      ledger::Status status, bool is_synced) {
                                    if (status == ledger::Status::INTERNAL_NOT_FOUND) {
                                      // Not finding an object is okay in this context: we'll just
                                      // reply we don't have it. There is not need to abort
                                      // processing the request.
                                      callback(ledger::Status::OK);
                                      return;
                                    }
                                    response.is_synced = is_synced;
                                    callback(status);
                                  });
        }

        ledger::Status status;
        if (coroutine::Wait(handler, response_waiter, &status) ==
            coroutine::ContinuationStatus::INTERRUPTED) {
          return;
        }

        if (status != ledger::Status::OK) {
          FXL_LOG(WARNING) << "Error while retrieving objects: " << status;
          return;
        }

        flatbuffers::FlatBufferBuilder buffer;
        BuildObjectResponseBuffer(&buffer, std::move(object_responses));

        mesh_->Send(source, buffer);
      });
}

void PageCommunicatorImpl::BuildObjectResponseBuffer(
    flatbuffers::FlatBufferBuilder* buffer, std::list<ObjectResponseHolder> object_responses) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id_),
                            convert::ToFlatBufferVector(buffer, page_id_));
  std::vector<flatbuffers::Offset<Object>> fb_objects;
  for (const ObjectResponseHolder& object_response : object_responses) {
    flatbuffers::Offset<ObjectId> fb_object_id =
        CreateObjectId(*buffer, object_response.identifier.key_index(),
                       convert::ToFlatBufferVector(
                           buffer, object_response.identifier.object_digest().Serialize()));
    if (object_response.piece) {
      fxl::StringView data = object_response.piece->GetData();
      flatbuffers::Offset<Data> fb_data =
          CreateData(*buffer, convert::ToFlatBufferVector(buffer, data));
      ObjectSyncStatus sync_status =
          object_response.is_synced ? ObjectSyncStatus_SYNCED_TO_CLOUD : ObjectSyncStatus_UNSYNCED;
      fb_objects.emplace_back(
          CreateObject(*buffer, fb_object_id, ObjectStatus_OK, fb_data, sync_status));
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

void PageCommunicatorImpl::MarkSyncedToPeer(fit::function<void(ledger::Status)> callback) {
  if (marked_as_synced_to_peer_) {
    callback(ledger::Status::OK);
    return;
  }
  storage_->MarkSyncedToPeer([this, callback = std::move(callback)](ledger::Status status) {
    if (status == ledger::Status::OK) {
      marked_as_synced_to_peer_ = true;
    }
    callback(status);
  });
}

void PageCommunicatorImpl::SendToInterestedDevices(convert::ExtendedStringView data) {
  for (auto it = interested_devices_.begin(); it != interested_devices_.end();) {
    // mesh_->Send() may invalidate the current iterator. Thus here, we make a
    // copy and increment it. If mesh_->Send() invalidates the iterator
    // |device|, |it| will not be affected.
    auto device = it++;
    mesh_->Send(*device, data);
  }
}

void PageCommunicatorImpl::SendHead(const p2p_provider::P2PClientId& device) {
  std::vector<std::unique_ptr<const storage::Commit>> head_commits;
  ledger::Status status = storage_->GetHeadCommits(&head_commits);
  if (status != ledger::Status::OK) {
    return;
  }
  FXL_DCHECK(head_commits.size() > 0);
  if (head_commits.size() != 1) {
    // We'll wait for the merge to happen, and send the head when storage notifies us.
    return;
  }
  if (head_commits[0]->GetId() == storage::kFirstPageCommitId) {
    // We have nothing to send.
    return;
  }
  flatbuffers::FlatBufferBuilder buffer;
  BuildCommitBuffer(&buffer, head_commits);

  mesh_->Send(device, buffer);
}

}  // namespace p2p_sync
