// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functions and Operation classes from LinkImpl that exist
// solely to implement the history of change operations for Links.

#include <fuchsia/modular/cpp/fidl.h>
#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/bin/user_runner/story_runner/link_impl.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/util/debug.h"

namespace modular {

namespace {

std::string MakeSequencedLinkKey(const fuchsia::modular::LinkPath& link_path,
                                 const std::string& sequence_key) {
  // |sequence_key| uses characters that never require escaping
  return MakeLinkKey(link_path) + kSeparator + sequence_key;
}

std::string MakeSequencedLinkKeyPrefix(
    const fuchsia::modular::LinkPath& link_path) {
  return MakeLinkKey(link_path) + kSeparator;
}

void XdrLinkChange_v1(XdrContext* const xdr,
                      fuchsia::modular::internal::LinkChange* const data) {
  xdr->Field("key", &data->key);
  xdr->Field("op", &data->op);
  xdr->Field("path", &data->pointer);
  xdr->Field("json", &data->json);
}

void XdrLinkChange_v2(XdrContext* const xdr,
                      fuchsia::modular::internal::LinkChange* const data) {
  if (!xdr->Version(2)) {
    return;
  }
  xdr->Field("key", &data->key);
  xdr->Field("op", &data->op);
  xdr->Field("path", &data->pointer);
  xdr->Field("json", &data->json);
}

}  // namespace

// Not in anonymous namespace; needs extern linkage for use by test.
extern const XdrFilterType<fuchsia::modular::internal::LinkChange>
    XdrLinkChange[] = {
        XdrLinkChange_v2,
        XdrLinkChange_v1,
        nullptr,
};

// Reload needs to run if:
// 1. LinkImpl was just constructed
// 2. IncrementalChangeCall sees an out-of-order change
class LinkImpl::ReloadCall : public Operation<> {
 public:
  ReloadCall(LinkImpl* const impl, ResultCall result_call)
      : Operation("LinkImpl::ReloadCall", result_call), impl_(impl) {}

 private:
  void Run();

  LinkImpl* const impl_;  // not owned
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ReloadCall);
};

class LinkImpl::IncrementalWriteCall : public Operation<> {
 public:
  IncrementalWriteCall(LinkImpl* const impl,
                       fuchsia::modular::internal::LinkChangePtr data,
                       ResultCall result_call)
      : Operation("LinkImpl::IncrementalWriteCall", std::move(result_call)),
        impl_(impl),
        data_(std::move(data)) {
    FXL_DCHECK(!data_->key.is_null());
  }

  std::string key() { return data_->key; }

 private:
  void Run() {
    FlowToken flow{this};
    operation_queue_.Add(
        new WriteDataCall<fuchsia::modular::internal::LinkChange>(
            impl_->page(), MakeSequencedLinkKey(impl_->link_path_, data_->key),
            XdrLinkChange, std::move(data_), [this, flow] {}));
  }

  LinkImpl* const impl_;  // not owned
  fuchsia::modular::internal::LinkChangePtr data_;
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(IncrementalWriteCall);
};

class LinkImpl::ChangeCall : public Operation<> {
 public:
  ChangeCall(LinkImpl* const impl, fidl::StringPtr json)
      : Operation("LinkImpl::ChangeCall", [] {}), impl_(impl), json_(json) {}

 private:
  void Run() override {
    FlowToken flow{this};

    auto change =
        std::make_pair(MakeLinkKey(impl_->link_path_), std::string(json_));
    auto it = std::find_if(
        impl_->pending_writes_.begin(), impl_->pending_writes_.end(),
        [&change](const std::pair<std::string, std::string>& entry) {
          return entry == change;
        });
    if (it != impl_->pending_writes_.end()) {
      impl_->pending_writes_.erase(it);
      return;
    }

    // NOTE(jimbe) With rapidjson, the opposite check is more expensive, O(n^2),
    // so we won't do it for now. See case kObjectType in operator==() in
    // include/rapidjson/document.h.
    //
    //  if (doc_.Equals(json)) {
    //    return;
    //  }
    //
    // Since all json in a link was written by the same serializer, this check
    // is mostly accurate. This test has false negatives when only order
    // differs.
    if (json_ == JsonValueToString(impl_->doc_)) {
      return;
    }

    impl_->doc_.Parse(json_);
    impl_->NotifyWatchers(kOnChangeConnectionId);
  }

  LinkImpl* const impl_;  // not owned
  const fidl::StringPtr json_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChangeCall);
};

class LinkImpl::IncrementalChangeCall : public Operation<> {
 public:
  IncrementalChangeCall(LinkImpl* const impl,
                        fuchsia::modular::internal::LinkChangePtr data,
                        uint32_t src, ResultCall result_call)
      : Operation("LinkImpl::IncrementalChangeCall", std::move(result_call)),
        impl_(impl),
        data_(std::move(data)),
        src_(src) {}

 private:
  void Run() {
    FlowToken flow{this};

    // If the change already exists in pending_ops_, then the Ledger has
    // processed the change and the change can be removed from pending_ops_. For
    // operations coming directly from the API, data_->key is empty, so this
    // block will do nothing.
    if (!impl_->pending_ops_.empty() &&
        data_->key == impl_->pending_ops_[0].key) {
      impl_->pending_ops_.erase(impl_->pending_ops_.begin());
      return;
    }

    old_json_ = JsonValueToString(impl_->doc_);

    if (data_->key.is_null()) {
      if (!data_->json.is_null()) {
        rapidjson::Document doc;
        doc.Parse(data_->json.get());
        if (doc.HasParseError()) {
          FXL_LOG(ERROR) << trace_name() << " "
                         << EncodeLinkPath(impl_->link_path_)
                         << " JSON parse failed error #" << doc.GetParseError()
                         << std::endl
                         << data_->json.get();
          return;
        }

        data_->json = JsonValueToString(doc);
      }

      data_->key = impl_->key_generator_.Create();
      impl_->pending_ops_.push_back(CloneStruct(*data_));
      operation_queue_.Add(
          new IncrementalWriteCall(impl_, CloneOptional(data_), [flow] {}));
    }

    const bool reload = data_->key < impl_->latest_key_;
    if (reload) {
      // Use kOnChangeConnectionId because the interaction of this change with
      // later changes is unpredictable.
      operation_queue_.Add(new ReloadCall(
          impl_, [this, flow] { Cont1(flow, kOnChangeConnectionId); }));
    } else {
      if (!impl_->ApplyChange(data_.get())) {
        FXL_LOG(WARNING) << trace_name() << " "
                         << "ApplyChange() failed ";
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
  fuchsia::modular::internal::LinkChangePtr data_;
  std::string old_json_;
  uint32_t src_;

  // IncrementalWriteCall is executed here.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(IncrementalChangeCall);
};

// This function is factored out of ReloadCall because of the circular reference
// caused by |new IncrementalChangeCall|. Although it's possible that this Run()
// function gets called recursively, it will stop recursing because of the
// following sequence of events:
// (1) the SET operation will be applied to the fuchsia::modular::Link
// (2) |changes| will no longer be empty
// (3) the |Replay()| path will be taken in any recursive call
void LinkImpl::ReloadCall::Run() {
  FlowToken flow{this};
  operation_queue_.Add(
      new ReadAllDataCall<fuchsia::modular::internal::LinkChange>(
          impl_->page(), MakeSequencedLinkKeyPrefix(impl_->link_path_),
          XdrLinkChange,
          [this, flow](
              fidl::VectorPtr<fuchsia::modular::internal::LinkChange> changes) {
            // NOTE(mesch): Initial link data must be applied only at the time
            // the fuchsia::modular::Intent is originally issued, not when the
            // story is resumed and modules are restarted from the
            // fuchsia::modular::Intent stored in the story record. Therefore,
            // initial data from create_link_info are ignored if there are
            // increments to replay.
            //
            // Presumably, it is possible that at the time the
            // fuchsia::modular::Intent is issued with initial data for a link,
            // a link of the same name already exists. In that case the initial
            // data are not applied either. Unclear whether that should be
            // considered wrong or not.
            if (changes->empty()) {
              if (impl_->create_link_info_ &&
                  !impl_->create_link_info_->initial_data.is_null() &&
                  !impl_->create_link_info_->initial_data->empty()) {
                fuchsia::modular::internal::LinkChangePtr data =
                    fuchsia::modular::internal::LinkChange::New();
                // Leave data->key null to signify a new entry
                data->op = fuchsia::modular::internal::LinkChangeOp::SET;
                data->pointer = fidl::VectorPtr<fidl::StringPtr>::New(0);
                data->json = std::move(impl_->create_link_info_->initial_data);
                operation_queue_.Add(new IncrementalChangeCall(
                    impl_, std::move(data), kWatchAllConnectionId, [flow] {}));
              }
            } else {
              impl_->Replay(std::move(changes));
            }
          }));
}

void LinkImpl::Replay(
    fidl::VectorPtr<fuchsia::modular::internal::LinkChange> changes) {
  doc_ = CrtJsonDoc();
  auto it1 = changes->begin();
  auto it2 = pending_ops_.begin();

  fuchsia::modular::internal::LinkChange* change{};
  for (;;) {
    bool it1_done = it1 == changes->end();
    bool it2_done = it2 == pending_ops_.end();

    FXL_DCHECK(it1_done || !it1->key.is_null());
    FXL_DCHECK(it2_done || !it2->key.is_null());

    if (it1_done && it2_done) {
      // Done
      break;
    } else if (!it1_done && it2_done) {
      change = &*it1;
      ++it1;
    } else if (it1_done && !it2_done) {
      change = &*it2;
      ++it2;
    } else {
      // Both it1 and it2 are valid
      const std::string& s1 = it1->key.get();
      const std::string& s2 = it2->key.get();
      int sgn = s1.compare(s2);
      sgn = sgn < 0 ? -1 : (sgn > 0 ? 1 : 0);
      switch (sgn) {
        case 0:
          change = &*it1;
          ++it1;
          ++it2;
          break;
        case -1:
          change = &*it1;
          ++it1;
          break;
        case 1:
          change = &*it2;
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

bool LinkImpl::ApplyChange(
    fuchsia::modular::internal::LinkChange* const change) {
  CrtJsonPointer ptr = CreatePointer(doc_, *change->pointer);

  switch (change->op) {
    case fuchsia::modular::internal::LinkChangeOp::SET:
      return ApplySetOp(ptr, change->json);
    case fuchsia::modular::internal::LinkChangeOp::UPDATE:
      return ApplyUpdateOp(ptr, change->json);
    case fuchsia::modular::internal::LinkChangeOp::ERASE:
      return ApplyEraseOp(ptr);
    default:
      FXL_DCHECK(false);
      return false;
  }
}

void LinkImpl::MakeReloadCall(std::function<void()> done) {
  operation_queue_.Add(new ReloadCall(this, std::move(done)));
}

void LinkImpl::MakeIncrementalWriteCall(
    fuchsia::modular::internal::LinkChangePtr data,
    std::function<void()> done) {
  operation_queue_.Add(
      new IncrementalWriteCall(this, std::move(data), std::move(done)));
}

void LinkImpl::MakeIncrementalChangeCall(
    fuchsia::modular::internal::LinkChangePtr data, uint32_t src) {
  operation_queue_.Add(
      new IncrementalChangeCall(this, std::move(data), src, [] {}));
}

void LinkImpl::OnPageChange(const std::string& key, const std::string& value) {
  if (kEnableIncrementalLinks) {
    fuchsia::modular::internal::LinkChangePtr data;
    if (!XdrRead(value, &data, XdrLinkChange)) {
      FXL_LOG(ERROR) << EncodeLinkPath(link_path_)
                     << "LinkImpl::OnChange() XdrRead failed: " << key << " "
                     << value;
      return;
    }

    MakeIncrementalChangeCall(std::move(data), kOnChangeConnectionId);
  } else {
    operation_queue_.Add(new ChangeCall(this, value));
  }
}

}  // namespace modular
