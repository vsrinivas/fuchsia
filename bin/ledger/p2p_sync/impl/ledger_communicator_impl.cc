// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_sync/impl/ledger_communicator_impl.h"

#include "lib/fxl/functional/closure.h"
#include "peridot/bin/ledger/p2p_sync/impl/flatbuffer_message_factory.h"
#include "peridot/bin/ledger/p2p_sync/impl/message_generated.h"
#include "peridot/bin/ledger/p2p_sync/impl/page_communicator_impl.h"

namespace p2p_sync {
LedgerCommunicatorImpl::LedgerCommunicatorImpl(std::string namespace_id,
                                               DeviceMesh* mesh)
    : namespace_id_(std::move(namespace_id)), mesh_(mesh) {}

LedgerCommunicatorImpl::~LedgerCommunicatorImpl() {
  FXL_DCHECK(pages_.empty());
  if (on_delete_) {
    on_delete_();
  }
}

void LedgerCommunicatorImpl::set_on_delete(fxl::Closure on_delete) {
  FXL_DCHECK(!on_delete_) << "on_delete() can only be called once.";
  on_delete_ = std::move(on_delete);
}

void LedgerCommunicatorImpl::OnDeviceChange(
    fxl::StringView remote_device, p2p_provider::DeviceChangeType change_type) {
  for (const auto& page : pages_) {
    page.second->OnDeviceChange(remote_device, change_type);
  }
}

void LedgerCommunicatorImpl::OnNewRequest(fxl::StringView source,
                                          fxl::StringView page_id,
                                          MessageHolder<Request> message) {
  const auto& it = pages_.find(page_id);
  if (it == pages_.end()) {
    // Send unknown page response.
    flatbuffers::FlatBufferBuilder buffer;
    CreateUnknownResponseMessage(&buffer, namespace_id_, page_id,
                                 ResponseStatus_UNKNOWN_PAGE);
    mesh_->Send(source, convert::ExtendedStringView(buffer));
    return;
  }

  it->second->OnNewRequest(source, std::move(message));
}

void LedgerCommunicatorImpl::OnNewResponse(fxl::StringView source,
                                           fxl::StringView page_id,
                                           MessageHolder<Response> message) {
  const auto& it = pages_.find(page_id);
  if (it == pages_.end()) {
    // Page has been deleted between request and response, just discard.
    return;
  }

  it->second->OnNewResponse(source, std::move(message));
}

std::unique_ptr<PageCommunicator> LedgerCommunicatorImpl::GetPageCommunicator(
    storage::PageStorage* storage, storage::PageSyncClient* sync_client) {
  storage::PageId page_id = storage->GetId();

  FXL_DCHECK(pages_.find(page_id) == pages_.end());

  std::unique_ptr<PageCommunicatorImpl> page =
      std::make_unique<PageCommunicatorImpl>(storage, sync_client,
                                             namespace_id_, page_id, mesh_);
  PageCommunicatorImpl* page_ptr = page.get();
  pages_.emplace(page_id, page_ptr);
  page->set_on_delete([this, page_id = std::move(page_id), page_ptr] {
    auto it = pages_.find(page_id);
    FXL_DCHECK(it != pages_.end());
    pages_.erase(it);
  });
  return page;
}

}  // namespace p2p_sync
