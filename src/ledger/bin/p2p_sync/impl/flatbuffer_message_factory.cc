// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/flatbuffer_message_factory.h"

#include "src/ledger/lib/convert/convert.h"

namespace p2p_sync {

void CreateUnknownResponseMessage(flatbuffers::FlatBufferBuilder* buffer,
                                  fxl::StringView namespace_id, fxl::StringView page_id,
                                  ResponseStatus status) {
  flatbuffers::Offset<NamespacePageId> namespace_page_id_offset =
      CreateNamespacePageId(*buffer, convert::ToFlatBufferVector(buffer, namespace_id),
                            convert::ToFlatBufferVector(buffer, page_id));
  flatbuffers::Offset<Response> response =
      CreateResponse(*buffer, status, namespace_page_id_offset);
  flatbuffers::Offset<Message> message =
      CreateMessage(*buffer, MessageUnion_Response, response.Union());
  buffer->Finish(message);
}
}  // namespace p2p_sync
