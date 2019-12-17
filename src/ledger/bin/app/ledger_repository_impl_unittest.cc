// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_impl.h"

#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/db_view_factory.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/clocks/impl/device_id_manager_impl.h"
#include "src/ledger/bin/clocks/public/device_id_manager.h"
#include "src/ledger/bin/clocks/public/types.h"
#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/sync_coordinator/testing/fake_ledger_sync.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

// BlockingFakeDb is a database that blocks all its calls.
class BlockingFakeDb : public storage::Db {
 public:
  Status StartBatch(coroutine::CoroutineHandler* handler,
                    std::unique_ptr<Batch>* /*batch*/) override {
    return Block(handler);
  }

  Status Get(coroutine::CoroutineHandler* handler, convert::ExtendedStringView /*key*/,
             std::string* /*value*/) override {
    return Block(handler);
  }

  Status HasKey(coroutine::CoroutineHandler* handler,
                convert::ExtendedStringView /*key*/) override {
    return Block(handler);
  }

  Status HasPrefix(coroutine::CoroutineHandler* handler,
                   convert::ExtendedStringView /*prefix*/) override {
    return Block(handler);
  }

  Status GetObject(coroutine::CoroutineHandler* handler, convert::ExtendedStringView /*key*/,
                   storage::ObjectIdentifier /*object_identifier*/,
                   std::unique_ptr<const storage::Piece>* /*piece*/) override {
    return Block(handler);
  }

  Status GetByPrefix(coroutine::CoroutineHandler* handler, convert::ExtendedStringView /*prefix*/,
                     std::vector<std::string>* /*key_suffixes*/) override {
    return Block(handler);
  }

  Status GetEntriesByPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView /*prefix*/,
      std::vector<std::pair<std::string, std::string>>* /*entries*/) override {
    return Block(handler);
  }

  Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView /*prefix*/,
      std::unique_ptr<storage::Iterator<
          const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>* /*iterator*/)
      override {
    return Block(handler);
  }

 private:
  Status Block(coroutine::CoroutineHandler* handler) {
    if (coroutine::SyncCall(handler, [this](fit::closure closure) {
          callbacks_.push_back(std::move(closure));
        }) == coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    return Status::ILLEGAL_STATE;
  }

  std::vector<fit::closure> callbacks_;
};

// BlockingFakeDbFactory returns BlockingFakeDb objects
class BlockingFakeDbFactory : public storage::DbFactory {
 public:
  void GetOrCreateDb(DetachedPath /*db_path*/, DbFactory::OnDbNotFound /*on_db_not_found*/,
                     fit::function<void(Status, std::unique_ptr<storage::Db>)> callback) override {
    callback(Status::OK, std::make_unique<BlockingFakeDb>());
  }
};

// Provides empty implementation of user-level synchronization. Helps to track ledger-level
// synchronization of pages.
class FakeUserSync : public sync_coordinator::UserSync {
 public:
  FakeUserSync() = default;
  FakeUserSync(const FakeUserSync&) = delete;
  FakeUserSync& operator=(const FakeUserSync&) = delete;
  ~FakeUserSync() override = default;

  // Returns the number of times synchronization was started for the given page.
  int GetSyncCallsCount(storage::PageId page_id) {
    return ledger_sync_ptr_->GetSyncCallsCount(page_id);
  }

  // UserSync:
  void Start() override {}

  void SetWatcher(sync_coordinator::SyncStateWatcher* watcher) override {}

  // Creates a FakeLedgerSync to allow tracking of the page synchronization.
  std::unique_ptr<sync_coordinator::LedgerSync> CreateLedgerSync(
      absl::string_view app_id, encryption::EncryptionService* encryption_service) override {
    auto ledger_sync = std::make_unique<sync_coordinator::FakeLedgerSync>();
    ledger_sync_ptr_ = ledger_sync.get();
    return std::move(ledger_sync);
  }

 private:
  sync_coordinator::FakeLedgerSync* ledger_sync_ptr_;
};

class FailingDeviceIdManager : public clocks::DeviceIdManager {
 public:
  Status OnPageDeleted(coroutine::CoroutineHandler* handler) override {
    return Status::INTERRUPTED;
  }

  Status GetNewDeviceId(coroutine::CoroutineHandler* handler,
                        clocks::DeviceId* device_id) override {
    *device_id = clocks::DeviceId{"fingerprint", 1};
    return Status::OK;
  }
};

// A fake DeviceId manager that yields before returning OK, to allow testing |DeletePageStorage|
// edge cases with precise interleaving control.
class YieldingDeviceIdManager : public clocks::DeviceIdManager {
 public:
  YieldingDeviceIdManager(coroutine::CoroutineHandler** handler) : handler_(handler) {}

  Status OnPageDeleted(coroutine::CoroutineHandler* handler) override {
    *handler_ = handler;
    if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED)
      return Status::INTERRUPTED;
    return Status::OK;
  }

  Status GetNewDeviceId(coroutine::CoroutineHandler* handler,
                        clocks::DeviceId* device_id) override {
    *device_id = clocks::DeviceId{"fingerprint", 1};
    return Status::OK;
  }

 private:
  // A pointer to a test-controlled variable where the coroutine handler of each OnPageDeleted call
  // is saved before yielding. This allows the test to resume the coroutine.
  coroutine::CoroutineHandler** handler_;
};

class LedgerRepositoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryImplTest()
      : tmp_location_(environment_.file_system()->CreateScopedTmpLocation()) {}
  LedgerRepositoryImplTest(const LedgerRepositoryImplTest&) = delete;
  LedgerRepositoryImplTest& operator=(const LedgerRepositoryImplTest&) = delete;

  void SetUp() override {
    std::unique_ptr<storage::fake::FakeDbFactory> db_factory =
        std::make_unique<storage::fake::FakeDbFactory>(environment_.file_system(), dispatcher());
    ResetLedgerRepository(std::move(db_factory), [this](DbViewFactory* dbview_factory) {
      auto clock = std::make_unique<clocks::DeviceIdManagerImpl>(
          &environment_, dbview_factory->CreateDbView(RepositoryRowPrefix::CLOCKS));
      EXPECT_TRUE(RunInCoroutine([&clock](coroutine::CoroutineHandler* handler) {
        EXPECT_EQ(clock->Init(handler), Status::OK);
      }));
      return clock;
    });
  }

  void ResetLedgerRepository(std::unique_ptr<storage::DbFactory> db_factory,
                             std::function<std::unique_ptr<clocks::DeviceIdManager>(DbViewFactory*)>
                                 device_id_manager_factory) {
    auto fake_page_eviction_manager = std::make_unique<FakeDiskCleanupManager>();
    disk_cleanup_manager_ = fake_page_eviction_manager.get();

    Status status;
    std::unique_ptr<DbViewFactory> dbview_factory;
    std::unique_ptr<clocks::DeviceIdManager> device_id_manager;
    std::unique_ptr<PageUsageDb> page_usage_db;
    DetachedPath detached_path = tmp_location_->path();
    EXPECT_TRUE(RunInCoroutine([&, this](coroutine::CoroutineHandler* handler) mutable {
      std::unique_ptr<storage::Db> leveldb;
      if (coroutine::SyncCall(
              handler,
              [&](fit::function<void(Status, std::unique_ptr<storage::Db>)> callback) mutable {
                db_factory->GetOrCreateDb(detached_path.SubPath("db"),
                                          storage::DbFactory::OnDbNotFound::CREATE,
                                          std::move(callback));
              },
              &status, &leveldb) == coroutine::ContinuationStatus::INTERRUPTED) {
        status = Status::INTERRUPTED;
        return;
      }
      LEDGER_CHECK(status == Status::OK);
      dbview_factory = std::make_unique<DbViewFactory>(std::move(leveldb));
      device_id_manager = device_id_manager_factory(dbview_factory.get());
      page_usage_db = std::make_unique<PageUsageDb>(
          &environment_, dbview_factory->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB));
    }));

    auto background_sync_manager =
        std::make_unique<BackgroundSyncManager>(&environment_, page_usage_db.get());

    auto user_sync = std::make_unique<FakeUserSync>();
    user_sync_ = user_sync.get();
    device_id_manager_ptr_ = device_id_manager.get();
    repository_ = std::make_unique<LedgerRepositoryImpl>(
        detached_path.SubPath("ledgers"), &environment_, std::move(db_factory),
        std::move(dbview_factory), std::move(page_usage_db), nullptr, std::move(user_sync),
        std::move(fake_page_eviction_manager), std::move(background_sync_manager),
        std::vector<PageUsageListener*>{disk_cleanup_manager_}, std::move(device_id_manager));
  }

  ~LedgerRepositoryImplTest() override = default;

 protected:
  std::unique_ptr<ScopedTmpLocation> tmp_location_;
  FakeDiskCleanupManager* disk_cleanup_manager_;
  FakeUserSync* user_sync_;
  clocks::DeviceIdManager* device_id_manager_ptr_;
  std::unique_ptr<LedgerRepositoryImpl> repository_;

};  // namespace

TEST_F(LedgerRepositoryImplTest, ConcurrentCalls) {
  // Ensure the repository is not empty.
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Make a first call to DiskCleanUp.
  bool callback_called1 = false;
  Status status1;
  repository_->DiskCleanUp(Capture(SetWhenCalled(&callback_called1), &status1));

  // Make a second one before the first one has finished.
  bool callback_called2 = false;
  Status status2;
  repository_->DiskCleanUp(Capture(SetWhenCalled(&callback_called2), &status2));

  // Make sure both of them start running.
  RunLoopUntilIdle();

  // Both calls must wait for the cleanup manager.
  EXPECT_FALSE(callback_called1);
  EXPECT_FALSE(callback_called2);

  // Call the cleanup manager callback and expect to see an ok status for both
  // pending callbacks.
  disk_cleanup_manager_->cleanup_callback(Status::OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called1);
  EXPECT_TRUE(callback_called2);
  EXPECT_EQ(status1, Status::OK);
  EXPECT_EQ(status2, Status::OK);
}

// Verifies that closing a ledger repository closes the LedgerRepository
// connections once all Ledger connections are themselves closed.
TEST_F(LedgerRepositoryImplTest, Close) {
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr2;
  LedgerPtr ledger_ptr;

  repository_->BindRepository(ledger_repository_ptr1.NewRequest());
  repository_->BindRepository(ledger_repository_ptr2.NewRequest());

  bool on_discardable_called;
  repository_->SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  bool ptr1_closed;
  zx_status_t ptr1_closed_status;
  ledger_repository_ptr1.set_error_handler(
      Capture(SetWhenCalled(&ptr1_closed), &ptr1_closed_status));
  bool ptr2_closed;
  zx_status_t ptr2_closed_status;
  ledger_repository_ptr2.set_error_handler(
      Capture(SetWhenCalled(&ptr2_closed), &ptr2_closed_status));
  bool ledger_closed;
  zx_status_t ledger_closed_status;
  ledger_ptr.set_error_handler(Capture(SetWhenCalled(&ledger_closed), &ledger_closed_status));

  ledger_repository_ptr1->GetLedger(convert::ToArray("ledger"), ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_repository_ptr2->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_ptr.Unbind();
  RunLoopUntilIdle();

  EXPECT_TRUE(on_discardable_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);

  // Delete the repository, as it would be done by LedgerRepositoryFactory when
  // the |on_discardable| callback is called.
  repository_.reset();
  RunLoopUntilIdle();
  EXPECT_TRUE(ptr1_closed);
  EXPECT_TRUE(ptr2_closed);

  EXPECT_EQ(ptr1_closed_status, ZX_OK);
  EXPECT_EQ(ptr2_closed_status, ZX_OK);
}

TEST_F(LedgerRepositoryImplTest, CloseEmpty) {
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;

  repository_->BindRepository(ledger_repository_ptr1.NewRequest());

  bool on_discardable_called;
  repository_->SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  bool ptr1_closed;
  zx_status_t ptr1_closed_status;
  ledger_repository_ptr1.set_error_handler(
      Capture(SetWhenCalled(&ptr1_closed), &ptr1_closed_status));

  ledger_repository_ptr1->Close();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  // The connection is not closed by LedgerRepositoryImpl, but by its holder.
  EXPECT_FALSE(ptr1_closed);
}

// Verifies that the callback on closure is called, even if the on_discardable is not set.
TEST_F(LedgerRepositoryImplTest, CloseWithoutOnDiscardableCallback) {
  bool ptr1_closed;
  Status ptr1_closed_status;

  repository_->Close(Capture(SetWhenCalled(&ptr1_closed), &ptr1_closed_status));
  RunLoopUntilIdle();

  EXPECT_TRUE(ptr1_closed);
}

// Verifies that the object remains alive is the no on_discardable nor close_callback are
// set.
TEST_F(LedgerRepositoryImplTest, AliveWithNoCallbacksSet) {
  // Ensure the repository is not empty.
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Make a first call to DiskCleanUp.
  bool callback_called1 = false;
  Status status1;
  repository_->DiskCleanUp(Capture(SetWhenCalled(&callback_called1), &status1));

  // Make sure it starts running.
  RunLoopUntilIdle();

  // The call must wait for the cleanup manager.
  EXPECT_FALSE(callback_called1);

  // Call the cleanup manager callback and expect to see an ok status for a pending callback.
  disk_cleanup_manager_->cleanup_callback(Status::OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called1);
  EXPECT_EQ(status1, Status::OK);
}

// Verifies that the object is not destroyed until the initialization of PageUsageDb is finished.
TEST_F(LedgerRepositoryImplTest, CloseWhileDbInitRunning) {
  std::unique_ptr<BlockingFakeDbFactory> db_factory = std::make_unique<BlockingFakeDbFactory>();
  ResetLedgerRepository(std::move(db_factory), [this](DbViewFactory* dbview_factory) {
    return std::make_unique<clocks::DeviceIdManagerImpl>(
        &environment_, dbview_factory->CreateDbView(RepositoryRowPrefix::CLOCKS));
  });

  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;

  repository_->BindRepository(ledger_repository_ptr1.NewRequest());

  bool on_discardable_called;
  repository_->SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  bool ptr1_closed;
  zx_status_t ptr1_closed_status;
  ledger_repository_ptr1.set_error_handler(
      Capture(SetWhenCalled(&ptr1_closed), &ptr1_closed_status));

  // The call should not trigger destruction, as the initialization of PageUsageDb is not finished.
  ledger_repository_ptr1->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(ptr1_closed);
}

PageId RandomId(const Environment& environment) {
  PageId result;
  environment.random()->Draw(&result.id);
  return result;
}

// Verifies that the LedgerRepositoryImpl triggers page sync for a page that exists and was closed.
TEST_F(LedgerRepositoryImplTest, TrySyncClosedPageSyncStarted) {
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageId page_id = convert::ToString(id.id);
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Opens the Ledger and creates LedgerManager.
  std::string ledger_name = "ledger";
  LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(ledger_name), first_ledger_ptr.NewRequest());

  // Opens the page and starts the sync with the cloud for the first time.
  first_ledger_ptr->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(user_sync_->GetSyncCallsCount(page_id), 1);

  page.Unbind();
  RunLoopUntilIdle();

  // Starts the sync of the reopened page.
  repository_->TrySyncClosedPage(convert::ExtendedStringView(ledger_name),
                                 convert::ExtendedStringView(id.id));
  RunLoopUntilIdle();
  EXPECT_EQ(user_sync_->GetSyncCallsCount(page_id), 2);
}

// Verifies that the LedgerRepositoryImpl does not trigger the sync for a currently open page.
TEST_F(LedgerRepositoryImplTest, TrySyncClosedPageWithOpenedPage) {
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageId page_id = convert::ToString(id.id);
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Opens the Ledger and creates LedgerManager.
  std::string ledger_name = "ledger";
  LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(ledger_name), first_ledger_ptr.NewRequest());

  // Opens the page and starts the sync with the cloud for the first time.
  first_ledger_ptr->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(user_sync_->GetSyncCallsCount(page_id), 1);

  // Tries to reopen the already-open page.
  repository_->TrySyncClosedPage(convert::ExtendedStringView(ledger_name),
                                 convert::ExtendedStringView(id.id));
  RunLoopUntilIdle();
  EXPECT_EQ(user_sync_->GetSyncCallsCount(page_id), 1);
}

TEST_F(LedgerRepositoryImplTest, PageDeletionNewDeviceId) {
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageId page_id = convert::ToString(id.id);
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Opens the Ledger and creates LedgerManager.
  std::string ledger_name = "ledger";
  LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(ledger_name), first_ledger_ptr.NewRequest());

  // Opens the page, and get the clock device id.
  first_ledger_ptr->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();

  clocks::DeviceId device_id_1;
  EXPECT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    EXPECT_EQ(device_id_manager_ptr_->GetNewDeviceId(handler, &device_id_1), Status::OK);
  }));
  page.Unbind();
  RunLoopUntilIdle();

  bool called;
  Status status;
  repository_->DeletePageStorage(ledger_name, page_id, Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // The clock device ID should have changed.
  clocks::DeviceId device_id_2;
  EXPECT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    EXPECT_EQ(device_id_manager_ptr_->GetNewDeviceId(handler, &device_id_2), Status::OK);
  }));

  EXPECT_NE(device_id_1, device_id_2);

  PagePredicateResult predicate;
  repository_->PageIsClosedAndSynced(ledger_name, page_id,
                                     Capture(SetWhenCalled(&called), &status, &predicate));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  // Page is deleted;
  EXPECT_EQ(status, Status::PAGE_NOT_FOUND);
}

TEST_F(LedgerRepositoryImplTest, PageDeletionNotDoneIfDeviceIdManagerFails) {
  std::unique_ptr<storage::fake::FakeDbFactory> db_factory =
      std::make_unique<storage::fake::FakeDbFactory>(environment_.file_system(), dispatcher());
  ResetLedgerRepository(std::move(db_factory), [](DbViewFactory* /*dbview_factory*/) {
    return std::make_unique<FailingDeviceIdManager>();
  });
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageId page_id = convert::ToString(id.id);
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Opens the Ledger and creates LedgerManager.
  std::string ledger_name = "ledger";
  LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(ledger_name), first_ledger_ptr.NewRequest());

  // Opens the page, and get the clock device id.
  first_ledger_ptr->GetPage(fidl::MakeOptional(id), page.NewRequest());
  // Make a commit so the page is not synced.
  page->Put(convert::ToArray("foo"), convert::ToArray("bar"));
  RunLoopUntilIdle();

  page.Unbind();
  RunLoopUntilIdle();

  bool called;
  Status status;
  repository_->DeletePageStorage(ledger_name, page_id, Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::INTERRUPTED);

  PagePredicateResult predicate;
  repository_->PageIsClosedAndSynced(ledger_name, page_id,
                                     Capture(SetWhenCalled(&called), &status, &predicate));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
}

// Regression test for a use-after-free bug when a page manager is deleted in the middle of
// DeletePageStorage (fxb/41628).
TEST_F(LedgerRepositoryImplTest, PageDeletionReopensPageManagerIfClosed) {
  coroutine::CoroutineHandler* handler = nullptr;
  std::unique_ptr<storage::fake::FakeDbFactory> db_factory =
      std::make_unique<storage::fake::FakeDbFactory>(environment_.file_system(), dispatcher());
  ResetLedgerRepository(std::move(db_factory), [&handler](DbViewFactory* /*dbview_factory*/) {
    return std::make_unique<YieldingDeviceIdManager>(&handler);
  });
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageId page_id = convert::ToString(id.id);
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Opens the Ledger and creates LedgerManager.
  std::string ledger_name = "ledger";
  LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(ledger_name), first_ledger_ptr.NewRequest());

  // Opens the page, and get the clock device id.
  first_ledger_ptr->GetPage(fidl::MakeOptional(id), page.NewRequest());
  // Make a commit so the page is not synced.
  page->Put(convert::ToArray("foo"), convert::ToArray("bar"));
  RunLoopUntilIdle();

  page.Unbind();
  RunLoopUntilIdle();

  bool called;
  Status status;
  repository_->DeletePageStorage(ledger_name, page_id, Capture(SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  // The call to DeletePageStorage is suspended in the middle of OnPageDeleted calback.
  EXPECT_FALSE(called);

  // Unbind to ensure automatic clean-up of LedgerManager from LedgerRepository. If
  // DeletePageStorage keeps a reference to the ledger manager, it will become invalid at this
  // point, which should trigger a failure when running the test under ASAN.
  first_ledger_ptr.Unbind();
  RunLoopUntilIdle();

  // Resume the call and ensure it completes successfully.
  ASSERT_NE(handler, nullptr);
  handler->Resume(coroutine::ContinuationStatus::OK);
  RunLoopUntilIdle();

  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
}

}  // namespace
}  // namespace ledger
