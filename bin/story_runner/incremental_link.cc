// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functions from StoryStorageImpl and LinkImpl that exist
// solely to implement the history of change operations for Links.

#include "apps/modular/src/story_runner/incremental_link.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/operations.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/util/debug.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/link_change.fidl.h"
#include "apps/modular/src/story_runner/link_impl.h"
#include "apps/modular/src/story_runner/story_storage_impl.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"

namespace modular {

LinkImpl::IncrementalWriteCall::IncrementalWriteCall(
    OperationContainer* const container, LinkImpl* const impl,
    LinkChangePtr data, ResultCall result_call)
    : Operation("LinkImpl::IncrementalWriteCall", container,
                std::move(result_call)),
      impl_(impl),
      data_(std::move(data)) {
  FTL_DCHECK(!data_->key.is_null());
  Ready();
}

std::string LinkImpl::IncrementalWriteCall::key() { return data_->key; }

void LinkImpl::IncrementalWriteCall::Run() {
  FlowToken flow{this};

  impl_->link_storage_->WriteIncrementalLinkData(
      impl_->link_path_, data_->key, std::move(data_), [this, flow] {});
}

LinkImpl::IncrementalChangeCall::IncrementalChangeCall(
    OperationContainer* const container, LinkImpl* const impl,
    LinkChangePtr data, uint32_t src)
    : Operation("LinkImpl::IncrementalChangeCall", container, [] {}),
      impl_(impl),
      data_(std::move(data)),
      src_(src) {
  Ready();
}

void LinkImpl::IncrementalChangeCall::Run() {
  FlowToken flow{this};

  // If the change already exists in pending_ops_, then the Ledger has
  // processed the change and the change can be removed from pending_ops_.
  // For operations coming directly from the API, data_->key is empty, so this
  // block will do nothing.
  if (!impl_->pending_ops_.empty() &&
      data_->key == impl_->pending_ops_[0]->key) {
    impl_->pending_ops_.erase(impl_->pending_ops_.begin());
    return;
  }

  old_json_ = JsonValueToString(impl_->doc_);

  if (data_->key.is_null()) {
    if (!data_->json.is_null()) {
      rapidjson::Document doc;
      doc.Parse(data_->json.get());
      if (doc.HasParseError()) {
        FTL_LOG(ERROR) << "LinkImpl::IncrementalChangeCall::Run() "
                       << EncodeLinkPath(impl_->link_path_)
                       << " JSON parse failed error #" << doc.GetParseError()
                       << std::endl
                       << data_->json.get();
        return;
      }

      data_->json = JsonValueToString(doc);
    }

    data_->key = impl_->key_generator_.Create();
    impl_->pending_ops_.push_back(data_.Clone());
    new IncrementalWriteCall(&operation_queue_, impl_, data_.Clone(), [] {});
  }

  const bool reload = data_->key < impl_->latest_key_;
  if (reload) {
    // Use kOnChangeConnectionId because the interaction of this change with
    // later changes is unpredictable.
    new ReloadCall(&operation_queue_, impl_,
                   [this, flow] { Cont1(flow, kOnChangeConnectionId); });
  } else {
    if (impl_->ApplyChange(data_.get())) {
      CrtJsonPointer ptr = CreatePointer(impl_->doc_, data_->pointer);
      impl_->ValidateSchema("LinkImpl::IncrementalChangeCall::Run", ptr,
                            data_->json);
    } else {
      FTL_LOG(WARNING) << "IncrementalChangeCall::Run - ApplyChange() failed ";
    }
    impl_->latest_key_ = data_->key;
    Cont1(flow, src_);
  }
}

void LinkImpl::IncrementalChangeCall::Cont1(FlowToken flow, uint32_t src) {
  if (old_json_ != JsonValueToString(impl_->doc_)) {
    impl_->NotifyWatchers(src);
  }
}

void LinkImpl::Replay(fidl::Array<LinkChangePtr> changes) {
  doc_ = CrtJsonDoc();
  auto it1 = changes.begin();
  auto it2 = pending_ops_.begin();

  LinkChange* change{};
  for (;;) {
    bool it1_done = it1 == changes.end();
    bool it2_done = it2 == pending_ops_.end();

    FTL_DCHECK(it1_done || !(*it1)->key.is_null());
    FTL_DCHECK(it2_done || !(*it2)->key.is_null());

    if (it1_done && it2_done) {
      // Done
      break;
    } else if (!it1_done && it2_done) {
      change = it1->get();
      ++it1;
    } else if (it1_done && !it2_done) {
      change = it2->get();
      ++it2;
    } else {
      // Both it1 and it2 are valid
      const std::string& s1 = (*it1)->key.get();
      const std::string& s2 = (*it2)->key.get();
      int sgn = s1.compare(s2);
      sgn = sgn < 0 ? -1 : (sgn > 0 ? 1 : 0);
      switch (sgn) {
        case 0:
          change = it1->get();
          ++it1;
          ++it2;
          break;
        case -1:
          change = it1->get();
          ++it1;
          break;
        case 1:
          change = it2->get();
          ++it2;
          break;
      }
    }

    ApplyChange(change);
  }

  if (change) {
    latest_key_ = change->key;
  }
}

LinkImpl::ReloadCall::ReloadCall(OperationContainer* const container,
                                 LinkImpl* const impl, ResultCall result_call)
    : Operation("LinkImpl::ReloadCall", container, result_call), impl_(impl) {
  Ready();
}

void LinkImpl::ReloadCall::Run() {
  FlowToken flow{this};
  impl_->link_storage_->ReadAllLinkData(
      impl_->link_path_, [this, flow](fidl::Array<LinkChangePtr> changes) {
        Cont1(flow, std::move(changes));
      });
}

void LinkImpl::ReloadCall::Cont1(FlowToken flow,
                                 fidl::Array<LinkChangePtr> changes) {
  impl_->Replay(std::move(changes));
}

bool LinkImpl::ApplyChange(LinkChange* change) {
  CrtJsonPointer ptr = CreatePointer(doc_, change->pointer);

  switch (change->op) {
    case LinkChangeOp::SET:
      return ApplySetOp(ptr, change->json);
    case LinkChangeOp::UPDATE:
      return ApplyUpdateOp(ptr, change->json);
    case LinkChangeOp::ERASE:
      return ApplyEraseOp(ptr);
    default:
      FTL_DCHECK(false);
      return false;
  }
}

}  // namespace modular
