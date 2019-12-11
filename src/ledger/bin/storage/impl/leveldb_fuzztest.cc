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

#include <fuzzer/FuzzedDataProvider.h>

#include "src/ledger/bin/environment/test_loop_notification.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/run_in_coroutine.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_impl.h"

// Fuzz test for LevelDb implementation of Db.
//
// This tests repeatedly picks an operation to perform on the Db and them performs it, validating
// that each operation succeeds.

namespace {

// Initialize a Db instance backed by temporary file system.
std::unique_ptr<storage::Db> GetDb(ledger::Environment* environment,
                                   ledger::ScopedTmpLocation* tmp_location) {
  ledger::DetachedPath db_path = tmp_location->path().SubPath("db");
  auto db = std::make_unique<storage::LevelDb>(environment->file_system(),
                                               environment->dispatcher(), db_path);
  auto status = db->Init();
  if (status != storage::Status::OK) {
    return nullptr;
  }
  return db;
}

// Types of the operation to perform on the db.
enum class Operation { PUT = 0, DELETE = 1, EXECUTE = 2, QUERY_HAS_KEY = 3 };

// Picks the next operation to perform, using the given source of fuzz data.
Operation GetNextOperation(FuzzedDataProvider* data_provider) {
  return static_cast<Operation>(data_provider->ConsumeIntegralInRange<uint8_t>(0, 3));
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
  async::TestLoop test_loop;
  sys::testing::ComponentContextProvider component_context_provider;

  std::unique_ptr<ledger::Platform> platform = ledger::MakePlatform();
  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform->file_system()->CreateScopedTmpLocation();
  auto io_loop = test_loop.StartNewLoop();
  ledger::Environment environment =
      ledger::EnvironmentBuilder()
          .SetStartupContext(component_context_provider.context())
          .SetPlatform(ledger::MakePlatform())
          .SetAsync(test_loop.dispatcher())
          .SetIOAsync(io_loop->dispatcher())
          .SetNotificationFactory(ledger::TestLoopNotification::NewFactory(&test_loop))
          .Build();
  coroutine::CoroutineServiceImpl coroutine_service;
  FuzzedDataProvider data_provider(data, remaining_size);

  std::unique_ptr<storage::Db::Batch> batch;
  auto db = GetDb(&environment, tmp_location.get());

  FXL_VLOG(1) << "Let's try to break LevelDb!";
  // Start the batch.
  DoStartBatch(&test_loop, &coroutine_service, db.get(), &batch);

  uint8_t operation_count = data_provider.ConsumeIntegral<uint8_t>();
  for (int i = 0; i < operation_count; i++) {
    // Break if no more random operation can be generated.
    if (data_provider.remaining_bytes() == 0) {
      break;
    }

    // Derive the operation and arguments from the fuzz data.
    auto operation = GetNextOperation(&data_provider);
    auto arg1 = data_provider.ConsumeRandomLengthString(255);
    auto arg2 = data_provider.ConsumeRandomLengthString(255);

    // Perform the db operation.
    switch (operation) {
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
