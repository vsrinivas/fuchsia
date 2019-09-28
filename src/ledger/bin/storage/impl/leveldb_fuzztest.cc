// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/fuzz_data.h"
#include "src/ledger/bin/testing/run_in_coroutine.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"

// Fuzz test for LevelDb implementation of Db.
//
// This tests repeatedly picks an operation to perform on the Db and them performs it, validating
// that each operation succeeds.

namespace {

// Initialize a Db instance backed by temporary file system.
std::unique_ptr<storage::Db> GetDb(scoped_tmpfs::ScopedTmpFS* tmpfs,
                                   async_dispatcher_t* dispatcher) {
  ledger::DetachedPath db_path(tmpfs->root_fd(), "db");
  auto db = std::make_unique<storage::LevelDb>(dispatcher, db_path);
  auto status = db->Init();
  if (status != storage::Status::OK) {
    return nullptr;
  }
  return db;
}

// Types of the operation to perform on the db.
enum class Operation { PUT = 0, DELETE = 1, EXECUTE = 2, QUERY_HAS_KEY = 3 };

// Picks the next operation to perform, using the given source of fuzz data.
std::optional<Operation> GetNextOperation(ledger::FuzzData* fuzz_data) {
  auto maybe_int = fuzz_data->GetNextSmallInt();
  if (!maybe_int.has_value()) {
    return {};
  }
  return static_cast<Operation>(maybe_int.value() % 4);
}

// Starts a new batch of mutation operations.
void DoStartBatch(async::TestLoop* test_loop, coroutine::CoroutineService* coroutine_service,
                  storage::Db* db, std::unique_ptr<storage::Db::Batch>* batch) {
  bool completed = ledger::RunInCoroutine(test_loop, coroutine_service,
                                          [batch, db](coroutine::CoroutineHandler* handler) {
                                            FXL_VLOG(1) << " - StartBatch";
                                            auto status = db->StartBatch(handler, batch);
                                            FXL_DCHECK(status == storage::Status::OK);
                                          });
  FXL_DCHECK(completed);
}

// Executes (commits) the given batch.
void DoExecute(async::TestLoop* test_loop, coroutine::CoroutineService* coroutine_service,
               storage::Db::Batch* batch) {
  bool completed = ledger::RunInCoroutine(test_loop, coroutine_service,
                                          [batch](coroutine::CoroutineHandler* handler) {
                                            FXL_VLOG(1) << " - Batch > Execute";
                                            storage::Status status = batch->Execute(handler);
                                            FXL_DCHECK(status == storage::Status::OK);
                                          });
  FXL_DCHECK(completed);
}

// Deletes an entry with the given key from the db.
void DoDelete(async::TestLoop* test_loop, coroutine::CoroutineService* coroutine_service,
              storage::Db::Batch* batch, std::string key) {
  bool completed = ledger::RunInCoroutine(test_loop, coroutine_service,
                                          [batch, key](coroutine::CoroutineHandler* handler) {
                                            FXL_VLOG(1) << " - Batch > Delete " << key;
                                            storage::Status status = batch->Delete(handler, key);
                                            FXL_DCHECK(status == storage::Status::OK);
                                          });
  FXL_DCHECK(completed);
}

// Writes an entry to the db.
void DoPut(async::TestLoop* test_loop, coroutine::CoroutineService* coroutine_service,
           storage::Db::Batch* batch, std::string key, std::string value) {
  bool completed = ledger::RunInCoroutine(
      test_loop, coroutine_service, [batch, key, value](coroutine::CoroutineHandler* handler) {
        FXL_VLOG(1) << " - Batch > Put " << key << "=" << value;
        storage::Status status = batch->Put(handler, key, value);
        FXL_DCHECK(status == storage::Status::OK);
      });
  FXL_DCHECK(completed);
}

// Queries the db to see if the given key is present.
void DoQueryHasKey(async::TestLoop* test_loop, coroutine::CoroutineService* coroutine_service,
                   storage::Db* db, std::string key) {
  bool completed = ledger::RunInCoroutine(
      test_loop, coroutine_service, [&db, key](coroutine::CoroutineHandler* handler) {
        FXL_VLOG(1) << " - Batch > QueryHasKey " << key;
        storage::Status status = db->HasKey(handler, key);
        FXL_DCHECK(status == storage::Status::OK || status == storage::Status::INTERNAL_NOT_FOUND);
      });
  FXL_DCHECK(completed);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t remaining_size) {
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  async::TestLoop test_loop;
  coroutine::CoroutineServiceImpl coroutine_service;
  ledger::FuzzData fuzz_data(data, remaining_size);

  std::unique_ptr<storage::Db::Batch> batch;
  auto db = GetDb(&tmpfs_, test_loop.dispatcher());

  FXL_VLOG(1) << "Let's try to break LevelDb!";
  // Start the batch.
  DoStartBatch(&test_loop, &coroutine_service, db.get(), &batch);

  const int kMaxIterations = 20;
  for (int i = 0; i < kMaxIterations; i++) {
    // Derive the operation and arguments from the fuzz data.
    auto maybe_operation = GetNextOperation(&fuzz_data);
    if (!maybe_operation.has_value()) {
      // Not enough fuzz data.
      break;
    }

    auto maybe_arg1 = fuzz_data.GetNextShortString();
    if (!maybe_arg1.has_value()) {
      // Not enough fuzz data.
      break;
    }
    std::string arg1 = maybe_arg1.value();

    auto maybe_arg2 = fuzz_data.GetNextShortString();
    if (!maybe_arg2.has_value()) {
      // Not enough fuzz data.
      break;
    }
    std::string arg2 = maybe_arg2.value();

    // Perform the db operation.
    switch (maybe_operation.value()) {
      case Operation::PUT:
        DoPut(&test_loop, &coroutine_service, batch.get(), arg1, arg2);
        break;
      case Operation::DELETE:
        DoDelete(&test_loop, &coroutine_service, batch.get(), arg1);
        break;
      case Operation::EXECUTE:
        DoExecute(&test_loop, &coroutine_service, batch.get());
        DoStartBatch(&test_loop, &coroutine_service, db.get(), &batch);
        break;
      case Operation::QUERY_HAS_KEY:
        DoQueryHasKey(&test_loop, &coroutine_service, db.get(), arg1);
        break;
    }
  }

  // The current batch needs to be executed before shutdown.
  DoExecute(&test_loop, &coroutine_service, batch.get());
  return 0;
}
