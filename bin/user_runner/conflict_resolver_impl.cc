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

class ConflictResolverImpl::LogConflictDiffCall : Operation<void> {
 public:
  LogConflictDiffCall(OperationContainer* const container,
                      ledger::MergeResultProviderPtr result_provider)
      : Operation(container, [] {}),
        result_provider_(std::move(result_provider)) {
    Ready();
  }

 private:
  void Run() override { GetDiff(nullptr); }

  void GetDiff(fidl::Array<uint8_t> continuation_token) {
    result_provider_->GetDiff(
        std::move(continuation_token),
        [this](ledger::Status status, ledger::PageChangePtr change_left,
               ledger::PageChangePtr change_right,
               fidl::Array<uint8_t> next_token) {
          if (status != ledger::Status::OK &&
              status != ledger::Status::PARTIAL_RESULT) {
            FTL_LOG(INFO) << "MergeResultProvider::GetDiff failed with status "
                          << status;
            return;
          }
          for (auto& change : change_left->changes) {
            FTL_LOG(INFO) << "changed right " << to_string(change->key);
          }

          for (auto& change : change_right->changes) {
            FTL_LOG(INFO) << "changed left " << to_string(change->key);
          }
          if (status == ledger::Status::PARTIAL_RESULT) {
            GetDiff(std::move(next_token));
          } else {
            result_provider_->Done([this](ledger::Status status) {
              if (status != ledger::Status::OK) {
                FTL_LOG(INFO) << "MergeResultProvider::Done failed with status "
                              << status;
              }
              Done();
            });
          }
        });
  }

  ledger::MergeResultProviderPtr result_provider_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LogConflictDiffCall);
};

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
    fidl::InterfaceHandle<ledger::PageSnapshot> right_version,
    fidl::InterfaceHandle<ledger::PageSnapshot> common_version,
    fidl::InterfaceHandle<ledger::MergeResultProvider> result_provider) {
  FTL_LOG(WARNING) << "Conflict in root page. Doing nothing.";

  ledger::MergeResultProviderPtr result_provider_ptr =
      ledger::MergeResultProviderPtr::Create(std::move(result_provider));
  new LogConflictDiffCall(&operation_queue_, std::move(result_provider_ptr));
}

}  // namespace modular
