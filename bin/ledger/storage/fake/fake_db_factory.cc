// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_db_factory.h"

#include "peridot/bin/ledger/storage/fake/fake_db.h"

namespace storage {
namespace fake {

void FakeDbFactory::CreateDb(
    ledger::DetachedPath /*db_path*/,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  CreateInitializedDb(std::move(callback));
}

void FakeDbFactory::GetDb(
    ledger::DetachedPath /*db_path*/,
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  CreateInitializedDb(std::move(callback));
}

void FakeDbFactory::CreateInitializedDb(
    fit::function<void(Status, std::unique_ptr<Db>)> callback) {
  callback(Status::OK, std::make_unique<FakeDb>(dispatcher_));
}

}  // namespace fake
}  // namespace storage
