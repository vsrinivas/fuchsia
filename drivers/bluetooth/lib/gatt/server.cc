// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <fbl/function.h>

#include "garnet/drivers/bluetooth/lib/att/database.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "lib/fxl/logging.h"

#include "gatt.h"

namespace btlib {
namespace gatt {

Server::Server(fxl::RefPtr<att::Database> database,
               fxl::RefPtr<att::Bearer> bearer)
    : db_(database), att_(bearer) {
  FXL_DCHECK(db_);
  FXL_DCHECK(att_);

  exchange_mtu_id_ = att_->RegisterHandler(
      att::kExchangeMTURequest, fbl::BindMember(this, &Server::OnExchangeMTU));
  read_by_group_type_id_ =
      att_->RegisterHandler(att::kReadByGroupTypeRequest,
                            fbl::BindMember(this, &Server::OnReadByGroupType));
}

Server::~Server() {
  att_->UnregisterHandler(read_by_group_type_id_);
  att_->UnregisterHandler(exchange_mtu_id_);
}

void Server::OnExchangeMTU(att::Bearer::TransactionId tid,
                           const att::PacketReader& packet) {
  FXL_DCHECK(packet.opcode() == att::kExchangeMTURequest);

  if (packet.payload_size() != sizeof(att::ExchangeMTURequestParams)) {
    att_->ReplyWithError(tid, att::kInvalidHandle, att::ErrorCode::kInvalidPDU);
    return;
  }

  const auto& params = packet.payload<att::ExchangeMTURequestParams>();
  uint16_t client_mtu = le16toh(params.client_rx_mtu);
  uint16_t server_mtu = att_->preferred_mtu();

  auto buffer = common::NewSlabBuffer(sizeof(att::Header) +
                                      sizeof(att::ExchangeMTURequestParams));
  FXL_CHECK(buffer);

  att::PacketWriter writer(att::kExchangeMTUResponse, buffer.get());
  auto rsp_params = writer.mutable_payload<att::ExchangeMTUResponseParams>();
  rsp_params->server_rx_mtu = htole16(server_mtu);

  att_->Reply(tid, std::move(buffer));

  // If the minimum value is less than the default MTU, then go with the default
  // MTU (Vol 3, Part F, 3.4.2.2).
  // TODO(armansito): This needs to use on kBREDRMinATTMTU for BR/EDR. Make the
  // default MTU configurable.
  att_->set_mtu(std::max(att::kLEMinMTU, std::min(client_mtu, server_mtu)));
}

void Server::OnReadByGroupType(att::Bearer::TransactionId tid,
                               const att::PacketReader& packet) {
  FXL_DCHECK(packet.opcode() == att::kReadByGroupTypeRequest);

  att::Handle start, end;
  common::UUID group_type;

  // The group type is represented as either a 16-bit or 128-bit UUID.
  if (packet.payload_size() == sizeof(att::ReadByTypeRequestParams16)) {
    const auto& params = packet.payload<att::ReadByTypeRequestParams16>();
    start = le16toh(params.start_handle);
    end = le16toh(params.end_handle);
    group_type = common::UUID(le16toh(params.type));
  } else if (packet.payload_size() == sizeof(att::ReadByTypeRequestParams128)) {
    const auto& params = packet.payload<att::ReadByTypeRequestParams128>();
    start = le16toh(params.start_handle);
    end = le16toh(params.end_handle);
    group_type = common::UUID(params.type);
  } else {
    att_->ReplyWithError(tid, att::kInvalidHandle, att::ErrorCode::kInvalidPDU);
    return;
  }

  if (group_type != types::kPrimaryService &&
      group_type != types::kSecondaryService) {
    att_->ReplyWithError(tid, start, att::ErrorCode::kUnsupportedGroupType);
    return;
  }

  constexpr size_t kRspStructSize = sizeof(att::ReadByGroupTypeResponseParams);
  constexpr size_t kHeaderSize = sizeof(att::Header) + kRspStructSize;
  FXL_DCHECK(kHeaderSize <= att_->mtu());

  uint8_t value_size;
  std::list<att::AttributeGrouping*> results;
  auto error_code = db_->ReadByGroupType(
      start, end, group_type, att_->mtu() - kHeaderSize, &value_size, &results);
  if (error_code != att::ErrorCode::kNoError) {
    att_->ReplyWithError(tid, start, error_code);
    return;
  }

  FXL_DCHECK(!results.empty());

  uint8_t entry_size = value_size + sizeof(att::AttributeGroupDataEntry);
  size_t pdu_size = kHeaderSize + entry_size * results.size();
  FXL_DCHECK(pdu_size <= att_->mtu());

  auto buffer = common::NewSlabBuffer(pdu_size);
  FXL_CHECK(buffer);

  att::PacketWriter writer(att::kReadByGroupTypeResponse, buffer.get());
  auto params = writer.mutable_payload<att::ReadByGroupTypeResponseParams>();
  params->length = entry_size;

  // Points to the next entry in the target PDU.
  auto next_entry = writer.mutable_payload_data().mutable_view(kRspStructSize);
  for (const auto& group : results) {
    auto* entry = reinterpret_cast<att::AttributeGroupDataEntry*>(
        next_entry.mutable_data());
    entry->start_handle = htole16(group->start_handle());
    entry->group_end_handle = htole16(group->end_handle());
    next_entry.Write(group->decl_value().view(0, value_size),
                     sizeof(att::AttributeGroupDataEntry));

    next_entry = next_entry.mutable_view(entry_size);
  }

  att_->Reply(tid, std::move(buffer));
}

}  // namespace gatt
}  // namespace btlib
