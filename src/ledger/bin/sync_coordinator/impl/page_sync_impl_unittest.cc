// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/page_sync_impl.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_storage.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/sync_coordinator/impl/page_sync_impl.h"
#include "src/ledger/bin/sync_coordinator/public/sync_state_watcher.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/backoff/testing/test_backoff.h"

namespace sync_coordinator {

// The fixture can't be called PageSyncImplTest because an other fixture
// is already using that name, and gtest does not like that.
class PageSyncImplTest2 : public ledger::TestWithEnvironment {
 public:
  PageSyncImplTest2() {}
  ~PageSyncImplTest2() override {}
};

// Verifies that the commit watchers owned by the PageSyncImpl deregister
// themselves if the PageCloud channel has an error.
TEST_F(PageSyncImplTest2, PageCloudError) {
  // Creates the PageCloud
  fuchsia::ledger::cloud::PageCloudPtr page_cloud_ptr;
  auto page_cloud = std::make_unique<cloud_sync::TestPageCloud>(page_cloud_ptr.NewRequest());

  // Creates the cloud_sync::PageSync using the PageCloud
  cloud_sync::TestPageStorage storage(dispatcher());
  sync_coordinator::PageSyncImpl page_sync(&storage, &storage);
  page_sync.CreateCloudSyncClient();
  encryption::FakeEncryptionService encryption_service(dispatcher());
  auto download_backoff = std::make_unique<ledger::TestBackoff>(zx::msec(50));
  auto upload_backoff = std::make_unique<ledger::TestBackoff>(zx::msec(50));

  EXPECT_TRUE(storage.watcher_ == nullptr);
  auto cloud_sync = std::make_unique<cloud_sync::PageSyncImpl>(
      dispatcher(), environment_.coroutine_service(), &storage, &storage, &encryption_service,
      std::move(page_cloud_ptr), std::move(download_backoff), std::move(upload_backoff));
  EXPECT_TRUE(storage.watcher_ != nullptr);

  // Start sync
  page_sync.SetCloudSync(std::move(cloud_sync));
  page_sync.Start();

  // Simulate an error on the channel by deconnecting the PageCloud.
  page_cloud.reset();
  RunLoopUntilIdle();

  // Verify that the CommitWatcher deregistered itself.
  EXPECT_TRUE(storage.watcher_ == nullptr);
}

}  // namespace sync_coordinator
