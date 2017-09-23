// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functions and Operation classes from LinkImpl that exist
// solely to implement the history of change operations for Links.

#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/story/fidl/link_change.fidl.h"
#include "peridot/bin/story_runner/link_impl.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/storage.h"
#include "peridot/lib/util/debug.h"

namespace modular {

// Not in anonymous namespace; needs extern linkage for use by test.
void XdrLinkChange(XdrContext* const xdr, LinkChange* const data) {
  xdr->Field("key", &data->key);
  xdr->Field("op", &data->op);
  xdr->Field("path", &data->pointer);
  xdr->Field("json", &data->json);
}

namespace {

std::string MakeSequencedLinkKey(const LinkPathPtr& link_path,
                                 const std::string& sequence_key) {
  // |sequence_key| uses characters that never require escaping
  return MakeLinkKey(link_path) + kSeparator + sequence_key;
}

}  // namespace

// Reload needs to run if:
// 1. LinkImpl was just constructed
// 2. IncrementalChangeCall sees an out-of-order change
class LinkImpl::ReloadCall : Operation<> {
 public:
  ReloadCall(OperationContainer* const container,
             LinkImpl* const impl, ResultCall result_call)
      : Operation("LinkImpl::ReloadCall", container, result_call), impl_(impl) {
    Ready();
  }

 private:
  void Run() {
    FlowToken flow{this};
    new ReadAllDataCall<LinkChange>(
        &operation_queue_,
        impl_->page(),
        MakeLinkKey(impl_->link_path_),
        XdrLinkChange,
        [this, flow](fidl::Array<LinkChangePtr> changes) {
          impl_->Replay(std::move(changes));
        });
  }

  LinkImpl* const impl_;  // not owned
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ReloadCall);
};

class LinkImpl::IncrementalWriteCall : Operation<> {
 public:
  IncrementalWriteCall(
      OperationContainer* const container, LinkImpl* const impl,
      LinkChangePtr data, ResultCall result_call)
      : Operation("LinkImpl::IncrementalWriteCall", container,
                  std::move(result_call)),
        impl_(impl),
        data_(std::move(data)) {
    FXL_DCHECK(!data_->key.is_null());
    Ready();
  }

  std::string key() { return data_->key; }

 private:
  void Run() {
    FlowToken flow{this};
    new WriteDataCall<LinkChange>(
        &operation_queue_,
        impl_->page(),
        MakeSequencedLinkKey(impl_->link_path_, data_->key),
        XdrLinkChange, std::move(data_), [this, flow] {});
  }

  LinkImpl* const impl_;  // not owned
  LinkChangePtr data_;
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(IncrementalWriteCall);
};

class LinkImpl::IncrementalChangeCall : Operation<> {
 public:
  IncrementalChangeCall(
      OperationContainer* const container, LinkImpl* const impl,
      LinkChangePtr data, uint32_t src)
      : Operation("LinkImpl::IncrementalChangeCall", container, [] {}),
        impl_(impl),
        data_(std::move(data)),
        src_(src) {
    Ready();
  }

 private:
  void Run() {
    FlowToken flow{this};

    // If the change already exists in pending_ops_, then the Ledger has
    // processed the change and the change can be removed from pending_ops_. For
    // operations coming directly from the API, data_->key is empty, so this
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
          FXL_LOG(ERROR) << "LinkImpl::IncrementalChangeCall::Run() "
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
      new IncrementalWriteCall(&operation_queue_, impl_, data_.Clone(), [flow] {});
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
        FXL_LOG(WARNING) << "IncrementalChangeCall::Run - ApplyChange() failed ";
      }
      impl_->latest_key_ = data_->key;
      Cont1(flow, src_);
    }
  }

  void Cont1(FlowToken flow, uint32_t src) {
    if (old_json_ != JsonValueToString(impl_->doc_)) {
      impl_->NotifyWatchers(src);
    }
  }

  LinkImpl* const impl_;  // not owned
  LinkChangePtr data_;
  std::string old_json_;
  uint32_t src_;

  // IncrementalWriteCall is executed here.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(IncrementalChangeCall);
};

void LinkImpl::Replay(fidl::Array<LinkChangePtr> changes) {
  doc_ = CrtJsonDoc();
  auto it1 = changes.begin();
  auto it2 = pending_ops_.begin();

  LinkChange* change{};
  for (;;) {
    bool it1_done = it1 == changes.end();
    bool it2_done = it2 == pending_ops_.end();

    FXL_DCHECK(it1_done || !(*it1)->key.is_null());
    FXL_DCHECK(it2_done || !(*it2)->key.is_null());

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
      FXL_DCHECK(false);
      return false;
  }
}

void LinkImpl::MakeReloadCall(std::function<void()> done) {
  new ReloadCall(&operation_queue_, this, std::move(done));
}

void LinkImpl::MakeIncrementalWriteCall(LinkChangePtr data, std::function<void()> done) {
  new IncrementalWriteCall(&operation_queue_, this, std::move(data), std::move(done));
}

void LinkImpl::MakeIncrementalChangeCall(LinkChangePtr data, uint32_t src) {
  new IncrementalChangeCall(&operation_queue_, this, std::move(data), src);
}

void LinkImpl::OnPageChange(const std::string& key, const std::string& value) {
  LinkChangePtr data;
  if (!XdrRead(value, &data, XdrLinkChange)) {
    FXL_LOG(ERROR) << EncodeLinkPath(link_path_)
                   << "LinkImpl::OnChange() XdrRead failed: "
                   << key << " " << value;
    return;
  }

  MakeIncrementalChangeCall(std::move(data), kOnChangeConnectionId);
}

}  // namespace modular
