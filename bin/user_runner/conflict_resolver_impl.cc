// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LtICENSE file.

#include "apps/modular/src/user_runner/conflict_resolver_impl.h"

#include "apps/modular/lib/fidl/array_to_string.h"

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

}  // namespace

ConflictResolverImpl::ConflictResolverImpl() = default;

ConflictResolverImpl::~ConflictResolverImpl() = default;

fidl::InterfaceHandle<ledger::ConflictResolverFactory>
ConflictResolverImpl::AddBinding() {
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
    fidl::InterfaceHandle<ledger::PageSnapshot> left_version,
    ledger::PageChangePtr change_left,
    fidl::InterfaceHandle<ledger::PageSnapshot> right_version,
    ledger::PageChangePtr change_right,
    fidl::InterfaceHandle<ledger::PageSnapshot> common_version,
    const ResolveCallback& callback) {
  FTL_LOG(WARNING) << "Conflict in root page. Doing nothing.";

  for (auto& change : change_left->changes) {
    FTL_LOG(INFO) << "changed right " << to_string(change->key);
  }

  for (auto& change : change_right->changes) {
    FTL_LOG(INFO) << "changed left " << to_string(change->key);
  }

  fidl::Array<ledger::MergedValuePtr> ret;
  ret.resize(0);
  callback(std::move(ret));
}

}  // namespace modular
