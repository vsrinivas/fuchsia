// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_

#include <flatbuffers/flatbuffers.h>
#include <lib/fit/function.h>

#include "lib/callback/auto_cleanable.h"
#include "lib/callback/cancellable.h"
#include "lib/callback/waiter.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/p2p_provider/public/types.h"
#include "peridot/bin/ledger/p2p_sync/impl/device_mesh.h"
#include "peridot/bin/ledger/p2p_sync/impl/message_generated.h"
#include "peridot/bin/ledger/p2p_sync/impl/message_holder.h"
#include "peridot/bin/ledger/p2p_sync/public/page_communicator.h"
#include "peridot/bin/ledger/storage/public/commit_watcher.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/page_sync_client.h"
#include "peridot/bin/ledger/storage/public/page_sync_delegate.h"
#include "peridot/lib/convert/convert.h"

namespace p2p_sync {
class PageCommunicatorImplInspectorForTest;

class PageCommunicatorImpl : public PageCommunicator,
                             public storage::PageSyncDelegate,
                             public storage::CommitWatcher {
 public:
  PageCommunicatorImpl(storage::PageStorage* storage,
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
                         std::unique_ptr<storage::DataSource::DataChunk>)>
          callback) override;

  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

 private:
  friend class PageCommunicatorImplInspectorForTest;
  class PendingObjectRequestHolder;

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
      std::vector<std::pair<storage::ObjectIdentifier,
                            std::unique_ptr<const storage::Object>>>
          results);

  // Processes an incoming ObjectRequest object.
  void ProcessObjectRequest(fxl::StringView source,
                            const ObjectRequest* request);

  // Map of pending requests for objects.
  callback::AutoCleanableMap<storage::ObjectIdentifier,
                             PendingObjectRequestHolder>
      pending_object_requests_;
  // List of devices we know are interested in this page.
  std::set<std::string, convert::StringViewComparator> interested_devices_;
  // List of devices we know are not interested in this page.
  std::set<std::string, convert::StringViewComparator> not_interested_devices_;
  fit::closure on_delete_;
  bool started_ = false;
  bool in_destructor_ = false;

  // Commit upload: we queue commits to upload here while we check if a
  // conflict exists. If it exist, we wait until it is resolved before
  // uploading.
  std::vector<std::unique_ptr<const storage::Commit>> commits_to_upload_;

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

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_PAGE_COMMUNICATOR_IMPL_H_
