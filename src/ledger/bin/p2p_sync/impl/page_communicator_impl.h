// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_

#include <lib/fit/function.h>

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/p2p_provider/public/types.h"
#include "src/ledger/bin/p2p_sync/impl/commit_batch.h"
#include "src/ledger/bin/p2p_sync/impl/device_mesh.h"
#include "src/ledger/bin/p2p_sync/impl/message_generated.h"
#include "src/ledger/bin/p2p_sync/impl/message_holder.h"
#include "src/ledger/bin/p2p_sync/public/page_communicator.h"
#include "src/ledger/bin/storage/public/commit_watcher.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/page_sync_client.h"
#include "src/ledger/bin/storage/public/page_sync_delegate.h"
#include "src/ledger/lib/callback/auto_cleanable.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace p2p_sync {
class PageCommunicatorImplInspectorForTest;

class PageCommunicatorImpl : public PageCommunicator,
                             public storage::PageSyncDelegate,
                             public storage::CommitWatcher,
                             public CommitBatch::Delegate {
 public:
  PageCommunicatorImpl(ledger::Environment* environment, storage::PageStorage* storage,
                       storage::PageSyncClient* sync_client, std::string namespace_id,
                       std::string page_id, DeviceMesh* mesh);
  PageCommunicatorImpl(const PageCommunicatorImpl&) = delete;
  PageCommunicatorImpl& operator=(const PageCommunicatorImpl&) = delete;
  ~PageCommunicatorImpl() override;

  void set_on_delete(fit::closure on_delete);

  // OnDeviceChange is called each time a device connects or unconnects.
  void OnDeviceChange(const p2p_provider::P2PClientId& remote_device,
                      p2p_provider::DeviceChangeType change_type);

  // Called when a new request arrived for this page from device |source|.
  void OnNewRequest(const p2p_provider::P2PClientId& source, MessageHolder<Request> message);

  // Called when a new response arrived for this page from device |source|.
  void OnNewResponse(const p2p_provider::P2PClientId& source, MessageHolder<Response> message);

  // PageCommunicator:
  void Start() override;

  // storage::PageSyncDelegate:
  void GetObject(storage::ObjectIdentifier object_identifier,
                 storage::RetrievedObjectType retrieved_object_type,
                 fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                                    std::unique_ptr<storage::DataSource::DataChunk>)>
                     callback) override;
  void GetDiff(
      storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases,
      fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
          callback) override;

  void UpdateClock(storage::Clock clock, fit::function<void(ledger::Status)> callback) override;

  // storage::CommitWatcher:
  void OnNewCommits(const std::vector<std::unique_ptr<const storage::Commit>>& commits,
                    storage::ChangeSource source) override;

 private:
  friend class PageCommunicatorImplInspectorForTest;
  class PendingObjectRequestHolder;
  struct ObjectResponseHolder;

  void RequestCommits(const p2p_provider::P2PClientId& device,
                      std::vector<storage::CommitId> ids) override;

  // These methods build the flatbuffer message corresponding to their name.
  void BuildWatchStartBuffer(flatbuffers::FlatBufferBuilder* buffer);
  void BuildWatchStopBuffer(flatbuffers::FlatBufferBuilder* buffer);
  void BuildObjectRequestBuffer(flatbuffers::FlatBufferBuilder* buffer,
                                storage::ObjectIdentifier object_identifier);
  void BuildCommitBuffer(flatbuffers::FlatBufferBuilder* buffer,
                         const std::vector<std::unique_ptr<const storage::Commit>>& commits);
  void BuildObjectResponseBuffer(flatbuffers::FlatBufferBuilder* buffer,
                                 std::list<ObjectResponseHolder> object_responses);

  // Processes an incoming CommitRequest object from device |source|.
  void ProcessCommitRequest(p2p_provider::P2PClientId source, MessageHolder<CommitRequest> request);

  // Builds a CommitResponse buffer in response to an incoming CommitRequest.
  // This is different from |BuildCommitBuffer| which builds CommitResponse for
  // a remote watcher. In particular, |BuildCommitBuffer|'s commits always
  // exist. In this method, the pair's second element for a commit will be null
  // if the commit does not exist on this device.
  void BuildCommitResponseBuffer(
      flatbuffers::FlatBufferBuilder* buffer,
      const std::vector<std::pair<storage::CommitId, std::unique_ptr<const storage::Commit>>>&
          commits);

  // Processes an incoming ObjectRequest object.
  void ProcessObjectRequest(p2p_provider::P2PClientId source, MessageHolder<ObjectRequest> request);

  // Marks the PageStorage as synced to a peer. If successful, on the following
  // call to MarkSyncedToPeer, the given |callback| will be called immediately.
  void MarkSyncedToPeer(fit::function<void(ledger::Status)> callback);

  // Sends a single message to all interested devices.
  void SendToInterestedDevices(convert::ExtendedStringView data);

  // If the page is merged, send the head to |device|.
  void SendHead(const p2p_provider::P2PClientId& device);

  // Map of pending requests for objects.
  ledger::AutoCleanableMap<storage::ObjectIdentifier, PendingObjectRequestHolder>
      pending_object_requests_;
  // Map of pending commit batch insertions.
  ledger::AutoCleanableMap<p2p_provider::P2PClientId, CommitBatch> pending_commit_batches_;
  // List of devices we know are interested in this page.
  std::set<p2p_provider::P2PClientId> interested_devices_;
  // List of devices we know are not interested in this page.
  std::set<p2p_provider::P2PClientId> not_interested_devices_;
  fit::closure on_delete_;
  bool marked_as_synced_to_peer_ = false;
  bool started_ = false;
  bool in_destructor_ = false;

  // Commit upload: we queue commits to upload here while we check if a
  // conflict exists. If it exist, we wait until it is resolved before
  // uploading.
  std::vector<std::unique_ptr<const storage::Commit>> commits_to_upload_;

  coroutine::CoroutineManager coroutine_manager_;
  const std::string namespace_id_;
  const std::string page_id_;
  DeviceMesh* const mesh_;
  storage::PageStorage* const storage_;
  storage::PageSyncClient* const sync_client_;

  // This must be the last member of the class.
  ledger::WeakPtrFactory<PageCommunicatorImpl> weak_factory_;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_
