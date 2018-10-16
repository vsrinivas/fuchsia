// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/fake_db_factory.h"

namespace storage {

void FakeDbFactory::CreateDb(
    ledger::DetachedPath db_path,
    fit::function<void(Status, std::unique_ptr<LevelDb>)> callback) {
  callback(Status::OK,
           std::make_unique<LevelDb>(dispatcher_, std::move(db_path)));
}

void FakeDbFactory::GetDb(
    ledger::DetachedPath db_path,
    fit::function<void(Status, std::unique_ptr<LevelDb>)> callback) {
  callback(Status::OK,
           std::make_unique<LevelDb>(dispatcher_, std::move(db_path)));
}

}  // namespace storage
