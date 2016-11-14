// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_repository_impl.h"

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/impl/ledger_storage_impl.h"

namespace ledger {

LedgerRepositoryImpl::LedgerRepositoryImpl(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const std::string& base_storage_dir)
    : task_runner_(task_runner), base_storage_dir_(base_storage_dir) {
  bindings_.set_on_empty_set_handler([this] { CheckEmpty(); });
  ledger_managers_.set_on_empty([this] { CheckEmpty(); });
}

LedgerRepositoryImpl::~LedgerRepositoryImpl() {}

void LedgerRepositoryImpl::BindRepository(
    fidl::InterfaceRequest<LedgerRepository> repository_request) {
  bindings_.AddBinding(this, std::move(repository_request));
}

void LedgerRepositoryImpl::GetLedger(
    fidl::Array<uint8_t> ledger_name,
    fidl::InterfaceRequest<Ledger> ledger_request,
    const GetLedgerCallback& callback) {
  if (ledger_name.size() == 0) {
    callback(Status::AUTHENTICATION_ERROR);
    return;
  }

  auto it = ledger_managers_.find(ledger_name);
  if (it == ledger_managers_.end()) {
    std::string name_as_string = convert::ToString(ledger_name);
    std::unique_ptr<storage::LedgerStorage> ledger_storage =
        std::make_unique<storage::LedgerStorageImpl>(
            task_runner_, base_storage_dir_, name_as_string);
    auto result = ledger_managers_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(std::move(name_as_string)),
        std::forward_as_tuple(std::move(ledger_storage)));
    FTL_DCHECK(result.second);
    it = result.first;
  }

  it->second.BindLedger(std::move(ledger_request));
  callback(Status::OK);
}

void LedgerRepositoryImpl::CheckEmpty() {
  if (!on_empty_callback_)
    return;
  if (ledger_managers_.empty() && bindings_.size() == 0)
    on_empty_callback_();
}

}  // namespace ledger
