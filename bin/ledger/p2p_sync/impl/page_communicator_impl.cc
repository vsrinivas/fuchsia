// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_sync/impl/page_communicator_impl.h"

#include "peridot/bin/ledger/p2p_sync/impl/message_generated.h"
#include "peridot/lib/convert/convert.h"

namespace p2p_sync {
PageCommunicatorImpl::PageCommunicatorImpl(
    storage::PageStorage* /*storage*/,
    storage::PageSyncClient* /*sync_client*/,
    std::string namespace_id,
    std::string page_id,
    DeviceMesh* mesh)
    : namespace_id_(std::move(namespace_id)),
      page_id_(std::move(page_id)),
      mesh_(mesh) {}

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

  CreateWatchStop(&buffer);
  char* buf = reinterpret_cast<char*>(buffer.GetBufferPointer());
  size_t size = buffer.GetSize();

  for (const auto& device : interested_devices_) {
    mesh_->Send(device, fxl::StringView(buf, size));
  }

  if (on_delete_) {
    on_delete_();
  }
}

void PageCommunicatorImpl::Start() {
  FXL_DCHECK(!started_);
  started_ = true;

  flatbuffers::FlatBufferBuilder buffer;
  CreateWatchStart(&buffer);

  for (const auto& device : mesh_->GetDeviceList()) {
    FXL_LOG(INFO) << "Sending to device " << device;
    mesh_->Send(device, convert::ExtendedStringView(buffer));
  }
}

void PageCommunicatorImpl::set_on_delete(fxl::Closure on_delete) {
  FXL_DCHECK(!on_delete_) << "set_on_delete() can only be called once.";
  on_delete_ = std::move(on_delete);
}

void PageCommunicatorImpl::OnDeviceChange(
    fxl::StringView remote_device,
    p2p_provider::DeviceChangeType change_type) {
  if (!started_ || in_destructor_) {
    return;
  }

  if (change_type == p2p_provider::DeviceChangeType::DELETED) {
    const auto& it = interested_devices_.find(remote_device);
    if (it != interested_devices_.end()) {
      interested_devices_.erase(it);
    }
    return;
  }

  flatbuffers::FlatBufferBuilder buffer;
  CreateWatchStart(&buffer);
  mesh_->Send(remote_device, convert::ExtendedStringView(buffer));
}

void PageCommunicatorImpl::OnNewRequest(fxl::StringView source,
                                        const Request* message) {
  FXL_DCHECK(!in_destructor_);
  switch (message->request_type()) {
    case RequestMessage_WatchStartRequest: {
      if (interested_devices_.find(source) == interested_devices_.end()) {
        interested_devices_.insert(source.ToString());
      }
      break;
    }
    case RequestMessage_WatchStopRequest: {
      const auto& it = interested_devices_.find(source);
      if (it != interested_devices_.end()) {
        interested_devices_.erase(it);
      }
      break;
    }
    case RequestMessage_CommitRequest:
    case RequestMessage_ObjectRequest:
      FXL_NOTIMPLEMENTED();
      break;
    case RequestMessage_NONE:
      FXL_LOG(ERROR) << "The message received is malformed.";
      return;
  }
}

void PageCommunicatorImpl::OnNewResponse(fxl::StringView /*source*/,
                                         const Response* /*message*/) {
  FXL_DCHECK(!in_destructor_);
  FXL_NOTIMPLEMENTED();
}

void PageCommunicatorImpl::CreateWatchStart(
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

void PageCommunicatorImpl::CreateWatchStop(
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

}  // namespace p2p_sync
