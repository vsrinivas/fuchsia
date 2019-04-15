// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_

#include <flatbuffers/flatbuffers.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/callback/cancellable.h>
#include <lib/callback/waiter.h>
#include <lib/fit/function.h>

#include "peridot/lib/convert/convert.h"
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
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace p2p_sync {
class PageCommunicatorImplInspectorForTest;

class PageCommunicatorImpl : public PageCommunicator,
                             public storage::PageSyncDelegate,
                             public storage::CommitWatcher,
                             public CommitBatch::Delegate {
 public:
  PageCommunicatorImpl(coroutine::CoroutineService* coroutine_service,
                       storage::PageStorage* storage,
                       storage::PageSyncClient* sync_client,
                       std::string namespace_id, std::string page_id,
                       DeviceMesh* mesh);
  ~PageCommunicatorImpl() override;

  void set_on_delete(fit::closure on_delete);

  // OnDeviceChange is called each time a device connects or unconnects.
  void OnDeviceChange(fxl::StringView remote_device,
                      p2p_provider::DeviceChangeType change_type);

  // Called when a new request arrived for this page from device |source|.
  void OnNewRequest(fxl::StringView source, MessageHolder<Request> message);

  // Called when a new response arrived for this page from device |source|.
  void OnNewResponse(fxl::StringView source, MessageHolder<Response> message);

  // PageCommunicator:
  void Start() override;

  // storage::PageSyncDelegate:
  void GetObject(
      storage::ObjectIdentifier object_identifier,
      fit::function<void(storage::Status, storage::ChangeSource,
                         storage::IsObjectSynced,
                         std::unique_ptr<storage::DataSource::DataChunk>)>
          callback) override;

  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

 private:
  friend class PageCommunicatorImplInspectorForTest;
  class PendingObjectRequestHolder;
  struct ObjectResponseHolder;

  void RequestCommits(fxl::StringView device,
                      std::vector<storage::CommitId> ids) override;

  // These methods build the flatbuffer message corresponding to their name.
  void BuildWatchStartBuffer(flatbuffers::FlatBufferBuilder* buffer);
  void BuildWatchStopBuffer(flatbuffers::FlatBufferBuilder* buffer);
  void BuildObjectRequestBuffer(flatbuffers::FlatBufferBuilder* buffer,
                                storage::ObjectIdentifier object_identifier);
  void BuildCommitBuffer(
      flatbuffers::FlatBufferBuilder* buffer,
      const std::vector<std::unique_ptr<const storage::Commit>>& commits);
  void BuildObjectResponseBuffer(
      flatbuffers::FlatBufferBuilder* buffer,
      std::list<ObjectResponseHolder> object_responses);

  // Processes an incoming CommitRequest object from device |source|.
  void ProcessCommitRequest(std::string source,
                            MessageHolder<CommitRequest> request);

  // Builds a CommitResponse buffer in response to an incoming CommitRequest.
  // This is different from |BuildCommitBuffer| which builds CommitResponse for
  // a remote watcher. In particular, |BuildCommitBuffer|'s commits always
  // exist. In this method, the pair's second element for a commit will be null
  // if the commit does not exist on this device.
  void BuildCommitResponseBuffer(
      flatbuffers::FlatBufferBuilder* buffer,
      const std::vector<
          std::pair<storage::CommitId, std::unique_ptr<const storage::Commit>>>&
          commits);

  // Processes an incoming ObjectRequest object.
  void ProcessObjectRequest(fxl::StringView source,
                            MessageHolder<ObjectRequest> request);

  // Marks the PageStorage as synced to a peer. If successful, on the following
  // call to MarkSyncedToPeer, the given |callback| will be called immediately.
  void MarkSyncedToPeer(fit::function<void(storage::Status)> callback);

  // Map of pending requests for objects.
  callback::AutoCleanableMap<storage::ObjectIdentifier,
                             PendingObjectRequestHolder>
      pending_object_requests_;
  // Map of pending commit batch insertions.
  callback::AutoCleanableMap<std::string, CommitBatch,
                             convert::StringViewComparator>
      pending_commit_batches_;
  // List of devices we know are interested in this page.
  std::set<std::string, convert::StringViewComparator> interested_devices_;
  // List of devices we know are not interested in this page.
  std::set<std::string, convert::StringViewComparator> not_interested_devices_;
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
  fxl::WeakPtrFactory<PageCommunicatorImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageCommunicatorImpl);
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_
