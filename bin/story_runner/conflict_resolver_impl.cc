// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LtICENSE file.

#include "apps/modular/src/story_runner/conflict_resolver_impl.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"

namespace modular {

namespace {

bool IsRootPageId(const fidl::Array<uint8_t>& id) {
  if (id.is_null()) {
    return false;
  }
  if (id.size() != 16) {
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    if (id[i] != 0) {
      return false;
    }
  }
  return true;
}

void MaybeMergeDeviceMap(
    ledger::PageChange* const change_left,
    ledger::PageChange* const change_right,
    fidl::Array<ledger::MergedValuePtr>* ret) {
  if (change_left == nullptr || change_right == nullptr) {
    return;
  }

  bool found_left = false;
  std::string bytes_left;
  for (auto& change : change_left->changes) {
    if (to_string(change->key) == kDeviceMapKey) {
      found_left = true;
      bytes_left = to_string(change->value->get_bytes());
      break;
    }
  }

  if (!found_left) {
    return;
  }

  bool found_right = false;
  std::string bytes_right;
  for (auto& change : change_right->changes) {
    if (to_string(change->key) == kDeviceMapKey) {
      found_right = true;
      bytes_right = to_string(change->value->get_bytes());
      break;
    }
  }

  if (!found_right) {
    return;
  }

  rapidjson::Document doc_left;
  doc_left.Parse(bytes_left);
  FTL_DCHECK(doc_left.IsObject());

  rapidjson::Document doc_right;
  doc_right.Parse(bytes_right);
  FTL_DCHECK(doc_right.IsObject());

  bool changed = false;
  for (auto& m : doc_right.GetObject()) {
    if (!doc_left.GetObject().HasMember(m.name)) {
      doc_left.AddMember(m.name, m.value, doc_left.GetAllocator());
      changed = true;
    }
  }

  if (!changed) {
    return;
  }

  ledger::MergedValuePtr result = ledger::MergedValue::New();
  result->key = to_array(kDeviceMapKey);
  result->source = ledger::ValueSource::NEW;

  ledger::BytesOrReferencePtr bytes_result = ledger::BytesOrReference::New();
  bytes_result->set_bytes(to_array(JsonValueToString(doc_left)));

  result->new_value = std::move(bytes_result);

  ret->push_back(std::move(result));
}

}  // namespace

ConflictResolverImpl::ConflictResolverImpl() = default;

ConflictResolverImpl::~ConflictResolverImpl() = default;

fidl::InterfaceHandle<ledger::ConflictResolverFactory> ConflictResolverImpl::AddBinding() {
  return factory_bindings_.AddBinding(this);
}

void ConflictResolverImpl::GetPolicy(fidl::Array<uint8_t> page_id,
                                     const GetPolicyCallback& callback) {
  if (IsRootPageId(page_id)) {
    callback(ledger::MergePolicy::AUTOMATIC_WITH_FALLBACK);
  } else {
    callback(ledger::MergePolicy::LAST_ONE_WINS);
  }
}

void ConflictResolverImpl::NewConflictResolver(
    fidl::Array<uint8_t> page_id,
    fidl::InterfaceRequest<ConflictResolver> request) {
  FTL_DCHECK(IsRootPageId(page_id));
  bindings_.AddBinding(this, std::move(request));
}

void ConflictResolverImpl::Resolve(
    ledger::PageChangePtr change_left,
    ledger::PageChangePtr change_right,
    fidl::InterfaceHandle<ledger::PageSnapshot> common_version,
    const ResolveCallback& callback) {
  fidl::Array<ledger::MergedValuePtr> ret;
  ret.resize(0);

  MaybeMergeDeviceMap(change_left.get(), change_right.get(), &ret);

  callback(std::move(ret));
}

}  // namespace modular
