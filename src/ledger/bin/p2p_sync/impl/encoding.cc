// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/encoding.h"

#include "src/ledger/bin/p2p_sync/impl/message_generated.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace p2p_sync {
namespace {

bool IsValidCommitRequest(flatbuffers::Verifier& verifier, const CommitRequest* request) {
  return request && request->Verify(verifier) && request->commit_ids();
}

bool IsValidObjectRequest(flatbuffers::Verifier& verifier, const ObjectRequest* request) {
  return request && request->Verify(verifier) && request->object_ids();
}

bool IsValidWatchStartRequest(flatbuffers::Verifier& verifier, const WatchStartRequest* request) {
  return request && request->Verify(verifier);
}

bool IsValidWatchStopRequest(flatbuffers::Verifier& verifier, const WatchStopRequest* request) {
  return request && request->Verify(verifier);
}

bool IsValidRequest(flatbuffers::Verifier& verifier, const Request* request) {
  if (!request || !request->Verify(verifier)) {
    return false;
  }

  const NamespacePageId* namespace_page_id = request->namespace_page();
  if (!namespace_page_id || !namespace_page_id->Verify(verifier)) {
    return false;
  }

  if (!namespace_page_id->namespace_id() || !namespace_page_id->page_id()) {
    return false;
  }

  switch (request->request_type()) {
    case RequestMessage_NONE:
      return false;
    case RequestMessage_CommitRequest:
      return IsValidCommitRequest(verifier, static_cast<const CommitRequest*>(request->request()));
    case RequestMessage_ObjectRequest:
      return IsValidObjectRequest(verifier, static_cast<const ObjectRequest*>(request->request()));
    case RequestMessage_WatchStartRequest:
      return IsValidWatchStartRequest(verifier,
                                      static_cast<const WatchStartRequest*>(request->request()));
    case RequestMessage_WatchStopRequest:
      return IsValidWatchStopRequest(verifier,
                                     static_cast<const WatchStopRequest*>(request->request()));
  }
}

bool IsValidCommitResponse(flatbuffers::Verifier& verifier, const CommitResponse* response) {
  if (!response || !response->Verify(verifier)) {
    return false;
  }

  if (!response->commits()) {
    return false;
  }

  for (const Commit* commit : *response->commits()) {
    if (!commit || !commit->Verify(verifier)) {
      return false;
    }

    const CommitId* id = commit->id();
    if (!id || !id->Verify(verifier) || !id->id()) {
      return false;
    }

    const Data* data = commit->commit();
    if (!data || !data->Verify(verifier) || !data->bytes()) {
      return false;
    }
  }

  return true;
}

bool IsValidObjectResponse(flatbuffers::Verifier& verifier, const ObjectResponse* response) {
  if (!response || !response->Verify(verifier)) {
    return false;
  }

  if (!response->objects()) {
    return false;
  }

  for (const Object* object : *response->objects()) {
    if (!object || !object->Verify(verifier)) {
      return false;
    }

    const ObjectId* id = object->id();
    if (!id || !id->Verify(verifier) || !id->digest()) {
      return false;
    }

    const Data* data = object->data();
    // No data is a valid response: it means the object was not found.
    if (data && (!data->Verify(verifier) || !data->bytes())) {
      return false;
    }
  }

  return true;
}

bool IsValidResponse(flatbuffers::Verifier& verifier, const Response* response) {
  if (!response) {
    return false;
  }

  if (!response->Verify(verifier)) {
    return false;
  }

  const NamespacePageId* namespace_page_id = response->namespace_page();
  if (!namespace_page_id || !namespace_page_id->Verify(verifier)) {
    return false;
  }

  if (!namespace_page_id->namespace_id() || !namespace_page_id->page_id()) {
    return false;
  }

  switch (response->response_type()) {
    case ResponseMessage_NONE:
      // Response returned in case of unknown namespace or page.
      return true;
    case ResponseMessage_CommitResponse:
      return IsValidCommitResponse(verifier,
                                   static_cast<const CommitResponse*>(response->response()));
    case ResponseMessage_ObjectResponse:
      return IsValidObjectResponse(verifier,
                                   static_cast<const ObjectResponse*>(response->response()));
  }
}

}  // namespace

const Message* ParseMessage(convert::ExtendedStringView data) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(data.data()), data.size());

  if (!VerifyMessageBuffer(verifier)) {
    return nullptr;
  }

  const Message* message = GetMessage(data.data());

  if (!message || !message->Verify(verifier)) {
    return nullptr;
  }

  switch (message->message_type()) {
    case MessageUnion_NONE:
      return nullptr;
    case MessageUnion_Request:
      if (!IsValidRequest(verifier, static_cast<const Request*>(message->message()))) {
        return nullptr;
      }
      break;
    case MessageUnion_Response:
      if (!IsValidResponse(verifier, static_cast<const Response*>(message->message()))) {
        return nullptr;
      }
      break;
  }

  return message;
}

}  // namespace p2p_sync
