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

class ConflictResolverImpl::LogConflictDiffCall : Operation<> {
 public:
  LogConflictDiffCall(OperationContainer* const container,
                      ledger::MergeResultProviderPtr result_provider)
      : Operation("ConflictResolverImpl::LogConflictDiffCall",
                  container,
                  [] {}),
        result_provider_(std::move(result_provider)) {
    Ready();
  }

 private:
  void Run() override {
    GetDiff(
        nullptr, "left",
        [this](fidl::Array<uint8_t> continuation_token,
               const std::function<void(ledger::Status, ledger::PageChangePtr,
                                        fidl::Array<uint8_t>)>& callback) {
          result_provider_->GetLeftDiff(std::move(continuation_token),
                                        callback);
        });

    GetDiff(
        nullptr, "right",
        [this](fidl::Array<uint8_t> continuation_token,
               const std::function<void(ledger::Status, ledger::PageChangePtr,
                                        fidl::Array<uint8_t>)>& callback) {
          result_provider_->GetRightDiff(std::move(continuation_token),
                                         callback);
        });
  }

  void GetDiff(
      fidl::Array<uint8_t> continuation_token,
      std::string left_or_right,
      std::function<void(fidl::Array<uint8_t>,
                         const std::function<void(ledger::Status,
                                                  ledger::PageChangePtr,
                                                  fidl::Array<uint8_t>)>&)>
          get_left_or_right_diff) {
    get_left_or_right_diff(
        std::move(continuation_token),
        [
          this, left_or_right = std::move(left_or_right), get_left_or_right_diff
        ](ledger::Status status, ledger::PageChangePtr change,
          fidl::Array<uint8_t> next_token) {
          if (status != ledger::Status::OK &&
              status != ledger::Status::PARTIAL_RESULT) {
            FXL_LOG(INFO)
                << "Getting diff from MergeResultProvider failed with status "
                << status;
            return;
          }
          for (auto& change : change->changes) {
            FXL_LOG(INFO) << "changed " << left_or_right << " "
                          << to_string(change->key);
          }
          if (status == ledger::Status::PARTIAL_RESULT) {
            GetDiff(std::move(next_token), left_or_right,
                    get_left_or_right_diff);
          } else {
            ++finished_;
            CheckIfDone();
          }
        });
  }

  void CheckIfDone() {
    if (finished_ != 2) {
      return;
    }
    result_provider_->Done([this](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FXL_LOG(INFO) << "MergeResultProvider::Done failed with status "
                      << status;
      }
      Done();
    });
  }

  ledger::MergeResultProviderPtr result_provider_;
  uint8_t finished_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogConflictDiffCall);
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
  FXL_DCHECK(IsRootPageId(page_id));
  bindings_.AddBinding(this, std::move(request));
}

void ConflictResolverImpl::Resolve(
    fidl::InterfaceHandle<ledger::PageSnapshot> /*left_version*/,
    fidl::InterfaceHandle<ledger::PageSnapshot> /*right_version*/,
    fidl::InterfaceHandle<ledger::PageSnapshot> /*common_version*/,
    fidl::InterfaceHandle<ledger::MergeResultProvider> result_provider) {
  FXL_LOG(WARNING) << "Conflict in root page. Doing nothing.";

  ledger::MergeResultProviderPtr result_provider_ptr =
      ledger::MergeResultProviderPtr::Create(std::move(result_provider));
  new LogConflictDiffCall(&operation_queue_, std::move(result_provider_ptr));
}

}  // namespace modular
