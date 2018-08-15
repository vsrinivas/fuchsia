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
  sdp->SetAttribute(kSDP_VersionNumberList,
                    DataElement(std::vector<DataElement>{kVersion}));

  // ServiceDatabaseState attribute. Changes when a service gets added or
  // removed.
  sdp->SetAttribute(kSDP_ServiceDatabaseState, DataElement(kInitialDbState));
}

void SendErrorResponse(const fbl::RefPtr<l2cap::Channel>& chan,
                       TransactionId tid, ErrorCode code) {
  ErrorResponse response(code);
  chan->Send(response.GetPDU(0 /* ignored */, tid, common::BufferView()));
}

}  // namespace

Server::Server(fbl::RefPtr<l2cap::L2CAP> l2cap)
    : l2cap_(l2cap),
      next_handle_(kFirstUnreservedHandle),
      db_state_(0),
      weak_ptr_factory_(this) {
  ZX_DEBUG_ASSERT(l2cap_);

  auto* sdp_record = MakeNewRecord(kSDPHandle);
  ZX_DEBUG_ASSERT(sdp_record);
  PopulateServiceDiscoveryService(sdp_record);

  bool registered = l2cap_->RegisterService(
      l2cap::kSDP,
      [self = weak_ptr_factory_.GetWeakPtr()](auto channel) {
        if (self)
          self->AddConnection(channel);
      },
      async_get_default_dispatcher());
  if (!registered) {
    bt_log(WARN, "sdp", "L2CAP service not registered");
  }
}

Server::~Server() { l2cap_->UnregisterService(l2cap::kSDP); }

bool Server::AddConnection(fbl::RefPtr<l2cap::Channel> channel) {
  bt_log(TRACE, "sdp", "add conneciton handle %#.4x", channel->link_handle());

  hci::ConnectionHandle handle = channel->link_handle();
  auto iter = channels_.find(channel->link_handle());
  if (iter != channels_.end()) {
    bt_log(WARN, "sdp", "handle %#.4x already connected", handle);
    return false;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  bool activated = channel->Activate(
      [self, handle](const l2cap::SDU& sdu) {
        if (self) {
          self->OnRxBFrame(handle, sdu);
        }
      },
      [self, handle] {
        if (self) {
          self->OnChannelClosed(handle);
        }
      },
      async_get_default_dispatcher());
  if (!activated) {
    bt_log(WARN, "sdp", "failed to activate channel (handle %#.4x)", handle);
    return false;
  }
  self->channels_.emplace(handle, std::move(channel));
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
  bt_log(SPEW, "sdp", "class ID list : %s", class_id_list.Describe().c_str());
  if (class_id_list.type() != DataElement::Type::kSequence) {
    bt_log(SPEW, "sdp", "class ID list isn't a sequence");
    return false;
  }
  size_t idx;
  const DataElement* elem;
  for (idx = 0; nullptr != (elem = class_id_list.At(idx)); idx++) {
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
    const std::unordered_set<common::UUID>& pattern) const {
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

ServiceAttributeResponse Server::GetServiceAttributes(
    ServiceHandle handle, const std::list<AttributeRange>& ranges) const {
  ServiceAttributeResponse resp;
  const auto& record = records_.at(handle);
  for (const auto& range : ranges) {
    auto attrs = record.GetAttributesInRange(range.start, range.end);
    for (const auto& attr : attrs) {
      resp.set_attribute(attr, record.GetAttribute(attr).Clone());
    }
  }
  bt_log(SPEW, "sdp", "ServiceAttribute %d attributes",
         resp.attributes().size());
  return resp;
}

ServiceSearchAttributeResponse Server::SearchAllServiceAttributes(
    const std::unordered_set<common::UUID>& search_pattern,
    const std::list<AttributeRange>& attribute_ranges) const {
  ServiceSearchAttributeResponse resp;
  for (const auto& it : records_) {
    const auto& rec = it.second;
    if (rec.FindUUID(search_pattern)) {
      for (const auto& range : attribute_ranges) {
        auto attrs = rec.GetAttributesInRange(range.start, range.end);
        for (const auto& attr : attrs) {
          resp.SetAttribute(it.first, attr, rec.GetAttribute(attr).Clone());
        }
      }
    }
  }

  bt_log(SPEW, "sdp", "ServiceSearchAttribute %d records",
         resp.num_attribute_lists());
  return resp;
}

void Server::OnChannelClosed(const hci::ConnectionHandle& handle) {
  channels_.erase(handle);
}

void Server::OnRxBFrame(const hci::ConnectionHandle& handle,
                        const l2cap::SDU& sdu) {
  uint16_t length = sdu.length();
  if (length < sizeof(Header)) {
    bt_log(TRACE, "sdp", "PDU too short; dropping");
    return;
  }

  auto it = channels_.find(handle);
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
      bt_log(SPEW, "sdp", "request isn't the correct size (%d != %d)",
             param_length, pdu.size() - sizeof(Header));
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
          SendErrorResponse(chan, tid, ErrorCode::kInvalidRequestSyntax);
          return;
        }
        auto resp = SearchServices(request.service_search_pattern());
        chan->Send(resp.GetPDU(request.max_service_record_count(), tid,
                               common::BufferView()));
        return;
      }
      case kServiceAttributeRequest: {
        ServiceAttributeRequest request(packet.payload_data());
        if (!request.valid()) {
          bt_log(SPEW, "sdp", "ServiceAttributeRequest not valid");
          SendErrorResponse(chan, tid, ErrorCode::kInvalidRequestSyntax);
          return;
        }
        auto handle = request.service_record_handle();
        if (records_.find(handle) == records_.end()) {
          bt_log(SPEW, "sdp", "ServiceAttributeRequest can't find handle %#.8x",
                 handle);
          SendErrorResponse(chan, tid, ErrorCode::kInvalidRecordHandle);
          return;
        }
        auto resp = GetServiceAttributes(handle, request.attribute_ranges());

        chan->Send(resp.GetPDU(request.max_attribute_byte_count(), tid,
                               request.ContinuationState()));
        return;
      }
      case kServiceSearchAttributeRequest: {
        ServiceSearchAttributeRequest request(packet.payload_data());
        if (!request.valid()) {
          bt_log(SPEW, "sdp", "ServiceSearchAttributeRequest not valid");
          SendErrorResponse(chan, tid, ErrorCode::kInvalidRequestSyntax);
          return;
        }
        auto resp = SearchAllServiceAttributes(request.service_search_pattern(),
                                               request.attribute_ranges());
        chan->Send(resp.GetPDU(request.max_attribute_byte_count(), tid,
                               request.ContinuationState()));
        return;
      }
      case kErrorResponse: {
        bt_log(SPEW, "sdp", "ErrorResponse isn't allowed as a request");
        SendErrorResponse(chan, tid, ErrorCode::kInvalidRequestSyntax);
        return;
      }
      default: {
        bt_log(SPEW, "sdp", "unhandled request, returning InvalidRequest");
        SendErrorResponse(chan, tid, ErrorCode::kInvalidRequestSyntax);
        return;
      }
    }
  });
}

}  // namespace sdp
}  // namespace btlib
