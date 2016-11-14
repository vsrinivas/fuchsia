// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_repository_factory_impl.h"

namespace ledger {

LedgerRepositoryFactoryImpl::LedgerRepositoryFactoryImpl(
    ftl::RefPtr<ftl::TaskRunner> task_runner)
    : task_runner_(task_runner) {}

LedgerRepositoryFactoryImpl::~LedgerRepositoryFactoryImpl() {}

void LedgerRepositoryFactoryImpl::GetRepository(
    const fidl::String& repository_path,
    fidl::InterfaceRequest<LedgerRepository> repository_request,
    const GetRepositoryCallback& callback) {
  auto it = repositories_.find(repository_path);
  if (it == repositories_.end()) {
    auto result = repositories_.emplace(
        std::piecewise_construct, std::forward_as_tuple(repository_path.get()),
        std::forward_as_tuple(task_runner_, repository_path.get()));
    FTL_DCHECK(result.second);
    it = result.first;
  }
  it->second.BindRepository(std::move(repository_request));
  callback(Status::OK);
}

}  // namespace ledger
