// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/entry_payload_encoding.h"

#include "src/ledger/bin/cloud_sync/impl/entry_payload_generated.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"

namespace cloud_sync {

std::string EncodeEntryPayload(const storage::Entry& entry,
                               storage::ObjectIdentifierFactory* factory) {
  flatbuffers::FlatBufferBuilder builder;
  KeyPriority priority =
      entry.priority == storage::KeyPriority::EAGER ? KeyPriority_EAGER : KeyPriority_LAZY;
  builder.Finish(CreateEntryPayload(
      builder, convert::ToFlatBufferVector(&builder, entry.key),
      convert::ToFlatBufferVector(&builder,
                                  factory->ObjectIdentifierToStorageBytes(entry.object_identifier)),
      priority));
  return convert::ToString(builder);
}

bool DecodeEntryPayload(convert::ExtendedStringView entry_id, convert::ExtendedStringView payload,
                        storage::ObjectIdentifierFactory* factory, storage::Entry* entry) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(payload.data()),
                                 payload.size());
  if (!VerifyEntryPayloadBuffer(verifier)) {
    LEDGER_LOG(ERROR) << "Received invalid entry payload from the cloud.";
    return false;
  }
  const EntryPayload* entry_payload =
      GetEntryPayload(reinterpret_cast<const unsigned char*>(payload.data()));
  if (entry_payload->entry_name() == nullptr || entry_payload->object_identifier() == nullptr) {
    LEDGER_LOG(ERROR) << "Received invalid entry payload from the cloud.";
    return false;
  }

  entry->entry_id = convert::ToString(entry_id);
  entry->key = convert::ToString(entry_payload->entry_name());
  entry->priority = entry_payload->priority() == KeyPriority_EAGER ? storage::KeyPriority::EAGER
                                                                   : storage::KeyPriority::LAZY;
  if (!factory->MakeObjectIdentifierFromStorageBytes(entry_payload->object_identifier(),
                                                     &entry->object_identifier)) {
    LEDGER_LOG(ERROR) << "Received invalid entry payload from the cloud.";
    return false;
  }

  return true;
}

}  // namespace cloud_sync
