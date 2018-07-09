// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <lib/async/default.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"
#include "lib/fxl/functional/auto_call.h"

namespace btlib {
namespace sdp {

namespace {

// The VersionNumberList value. (5.0, Vol 3, Part B, 5.2.3)
constexpr uint16_t kVersion = 0x0100;  // Version 1.0

// The initial ServiceDatabaseState
constexpr uint32_t kInitialDbState = 0;

// Populates the ServiceDiscoveryService record.
void PopulateServiceDiscoveryService(ServiceRecord* sdp) {
  ZX_DEBUG_ASSERT(sdp);
  ZX_DEBUG_ASSERT(sdp->handle() == kSDPHandle);
  // ServiceClassIDList attribute should have the
  // ServiceDiscoveryServerServiceClassID
  // See v5.0, Vol 3, Part B, Sec 5.2.2
  sdp->SetServiceClassUUIDs({profile::kServiceDiscoveryClass});

  // The VersionNumberList attribute. See v5.0, Vol 3, Part B, Sec 5.2.3
  // Version 1.0
  sdp->SetAttribute(kSDP_VersionNumberList, std::vector<DataElement>{kVersion});

  // ServiceDatabaseState attribute. Changes when a service gets added or
  // removed.
  sdp->SetAttribute(kSDP_ServiceDatabaseState, kInitialDbState);
}

}  // namespace

Server::Server()
    : next_handle_(kFirstUnreservedHandle),
      db_state_(0),
      weak_ptr_factory_(this) {
  auto* sdp_record = MakeNewRecord(kSDPHandle);
  ZX_DEBUG_ASSERT(sdp_record);
  PopulateServiceDiscoveryService(sdp_record);
}

bool Server::AddConnection(const std::string& peer_id,
                           fbl::RefPtr<l2cap::Channel> channel) {
  bt_log(TRACE, "sdp", "add connection: %s", peer_id.c_str());

  auto iter = channels_.find(peer_id);
  if (iter != channels_.end()) {
    bt_log(WARN, "sdp", "peer already connected: %s", peer_id.c_str());
    return false;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  bool activated = channel->Activate(
      [self, peer_id](const l2cap::SDU& sdu) {
        if (self) {
          self->OnRxBFrame(peer_id, sdu);
        }
      },
      [self, peer_id]() {
        if (self) {
          self->OnChannelClosed(peer_id);
        }
      },
      async_get_default_dispatcher());
  if (!activated) {
    bt_log(WARN, "sdp", "failed to activate channel (peer: %s)",
           peer_id.c_str());
    return false;
  }
  self->channels_.emplace(peer_id, std::move(channel));
  return true;
}

bool Server::RegisterService(ConstructCallback callback) {
  ZX_DEBUG_ASSERT(callback);
  ServiceHandle next = GetNextHandle();
  if (!next) {
    return false;
  }
  auto* record = MakeNewRecord(next);
  if (!record) {
    return false;
  }
  // Let the caller populate the record
  callback(record);

  auto failed_validation = fxl::MakeAutoCall([&] { records_.erase(next); });
  // They are not allowed to change (or remove) the ServiceRecordHandle.
  if (!record->HasAttribute(kServiceRecordHandle)) {
    bt_log(SPEW, "sdp", "ServiceRecordHandle was removed");
    return false;
  }
  auto handle = record->GetAttribute(kServiceRecordHandle).Get<uint32_t>();
  if (!handle || !(*handle == next)) {
    bt_log(SPEW, "sdp", "ServiceRecordHandle was changed");
    return false;
  }
  // Services must at least have a ServiceClassIDList (5.0, Vol 3, Part B, 5.1)
  if (!record->HasAttribute(kServiceClassIdList)) {
    bt_log(SPEW, "sdp", "new record doesn't have a ServiceClass");
    return false;
  }
  // Class ID list is a data element sequence in which each data element is
  // a UUID representing the service classes that a given service record
  // conforms to. (5.0, Vol 3, Part B, 5.1.2)
  const DataElement& class_id_list = record->GetAttribute(kServiceClassIdList);
  if (class_id_list.type() != DataElement::Type::kSequence) {
    bt_log(SPEW, "sdp", "class ID list isn't a sequence");
    return false;
  }
  size_t idx;
  const DataElement* elem;
  for (idx = 0, elem = class_id_list.At(idx); elem != nullptr;
       elem = class_id_list.At(++idx)) {
    if (elem->type() != DataElement::Type::kUuid) {
      bt_log(SPEW, "sdp", "class ID list elements are not all UUIDs");
      return false;
    }
  }
  if (idx == 0) {
    bt_log(SPEW, "sdp", "no elements in the Class ID list (need at least 1)");
    return false;
  }
  failed_validation.cancel();
  bt_log(SPEW, "sdp", "registered service %#.8x, classes: %s", next,
         record->GetAttribute(kServiceClassIdList).Describe().c_str());
  return true;
}

bool Server::UnregisterService(ServiceHandle handle) {
  if (handle == kSDPHandle || records_.find(handle) == records_.end()) {
    return false;
  }
  bt_log(TRACE, "sdp", "unregistering service (handle: %#.8x)", handle);
  records_.erase(handle);
  return true;
}

ServiceRecord* Server::MakeNewRecord(ServiceHandle handle) {
  if (records_.find(handle) != records_.end()) {
    return nullptr;
  }

  records_.emplace(std::piecewise_construct, std::forward_as_tuple(handle),
                   std::forward_as_tuple(handle));
  return &records_.at(handle);
}

ServiceHandle Server::GetNextHandle() {
  ServiceHandle initial_next_handle = next_handle_;
  // We expect most of these to be free.
  // Safeguard against possibly having to wrap-around and reuse handles.
  while (records_.count(next_handle_)) {
    if (next_handle_ == kLastHandle) {
      bt_log(WARN, "sdp", "service handle wrapped to start");
      next_handle_ = kFirstUnreservedHandle;
    } else {
      next_handle_++;
    }
    if (next_handle_ == initial_next_handle) {
      return 0;
    }
  }
  return next_handle_++;
}

ServiceSearchResponse Server::SearchServices(
    const std::unordered_set<common::UUID>& pattern) {
  ServiceSearchResponse resp;
  std::vector<ServiceHandle> matched;
  for (const auto& it : records_) {
    if (it.second.FindUUID(pattern)) {
      matched.push_back(it.first);
    }
  }
  bt_log(SPEW, "sdp", "ServiceSearch matched %d records", matched.size());
  resp.set_service_record_handle_list(matched);
  return resp;
}

void Server::OnChannelClosed(const std::string& peer_id) {
  channels_.erase(peer_id);
}

void Server::OnRxBFrame(const std::string& peer_id, const l2cap::SDU& sdu) {
  uint16_t length = sdu.length();
  if (length < sizeof(Header)) {
    bt_log(TRACE, "sdp", "PDU too short; dropping");
    return;
  }

  auto it = channels_.find(peer_id);
  if (it == channels_.end()) {
    bt_log(TRACE, "sdp", "can't find peer to respond to; dropping");
    return;
  }
  l2cap::SDU::Reader reader(&sdu);

  reader.ReadNext(length, [this, length, chan = it->second.share()](
                              const common::ByteBuffer& pdu) {
    ZX_ASSERT(pdu.size() == length);
    common::PacketView<Header> packet(&pdu);
    TransactionId tid = betoh16(packet.header().tid);
    uint16_t param_length = betoh16(packet.header().param_length);

    if (param_length != (pdu.size() - sizeof(Header))) {
      ErrorResponse response(ErrorCode::kInvalidSize);
      chan->Send(response.GetPDU(0 /* ignored */, tid, common::BufferView()));
      return;
    }

    packet.Resize(param_length);

    switch (packet.header().pdu_id) {
      case kServiceSearchRequest: {
        ServiceSearchRequest request(packet.payload_data());
        if (!request.valid()) {
          bt_log(TRACE, "sdp", "ServiceSearchRequest not valid");
          ErrorResponse response(ErrorCode::kInvalidRequestSyntax);
          chan->Send(
              response.GetPDU(0 /* ignored */, tid, common::BufferView()));
          return;
        }
        auto resp = SearchServices(request.service_search_pattern());
        chan->Send(resp.GetPDU(request.max_service_record_count(), tid,
                               common::BufferView()));
        return;
      }
      case kErrorResponse: {
        ErrorResponse response(ErrorCode::kInvalidRequestSyntax);
        chan->Send(response.GetPDU(0 /* ignored */, tid, common::BufferView()));
        return;
      }
      default: {
        ErrorResponse response(ErrorCode::kInvalidRequestSyntax);
        chan->Send(response.GetPDU(0 /* ignored */, tid, common::BufferView()));
        return;
      }
    }
  });
}

}  // namespace sdp
}  // namespace btlib
