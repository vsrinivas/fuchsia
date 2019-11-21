// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_ENTRY_PAYLOAD_ENCODING_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_ENTRY_PAYLOAD_ENCODING_H_

#include <string>

#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"

namespace cloud_sync {

// Serializes an entry payload for sending to the cloud. The entry identifier is not included in the
// serialization.
std::string EncodeEntryPayload(const storage::Entry& entry,
                               storage::ObjectIdentifierFactory* factory);

// Deserializes an entry payload. The entry identifier needs to be provided separately.
bool DecodeEntryPayload(convert::ExtendedStringView entry_id, convert::ExtendedStringView payload,
                        storage::ObjectIdentifierFactory* factory, storage::Entry* entry);

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_ENTRY_PAYLOAD_ENCODING_H_
