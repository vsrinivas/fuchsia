// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "garnet/lib/callback/capture.h"
#include "garnet/lib/gtest/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/ledger/fidl/ledger.fidl-sync.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/svc/cpp/services.h"
#include "peridot/bin/ledger/fidl/internal.fidl-sync.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"
#include "peridot/bin/ledger/testing/cloud_provider/fake_cloud_provider.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"
#include "peridot/bin/ledger/testing/e2e/e2e_test.h"

namespace test {
namespace e2e_local {
namespace {

template <class A>
bool Equals(const f1dl::Array<uint8_t>& a1, const A& a2) {
  if (a1.size() != a2.size())
    return false;
  return memcmp(a1.data(), a2.data(), a1.size()) == 0;
}

f1dl::Array<uint8_t> TestArray() {
  std::string value = "value";
  f1dl::Array<uint8_t> result = f1dl::Array<uint8_t>::New(value.size());
  memcpy(&result[0], &value[0], value.size());
  return result;
}

class LedgerEndToEndTest : public gtest::TestWithMessageLoop {
 public:
  LedgerEndToEndTest()
      : application_context_(
            component::ApplicationContext::CreateFromStartupInfoNotChecked()) {}
  ~LedgerEndToEndTest() override {}

 protected:
  void Init(std::vector<std::string> additional_args) {
    component::Services child_services;
    auto launch_info = component::ApplicationLaunchInfo::New();
    launch_info->url = "ledger";
    launch_info->directory_request = child_services.NewRequest();
    launch_info->arguments.push_back("--no_minfs_wait");
    launch_info->arguments.push_back("--no_statistics_reporting_for_testing");
    for (auto& additional_arg : additional_args) {
      launch_info->arguments.push_back(additional_arg);
    }
    application_context()->launcher()->CreateApplication(
        std::move(launch_info), ledger_controller_.NewRequest());

    ledger_controller_.set_error_handler([this] {
      for (const auto& callback : ledger_shutdown_callbacks_) {
        callback();
      }
    });

    child_services.ConnectToService(ledger_repository_factory_.NewRequest());
    child_services.ConnectToService(f1dl::GetSynchronousProxy(&controller_));
  }

  void RegisterShutdownCallback(std::function<void()> callback) {
    ledger_shutdown_callbacks_.push_back(std::move(callback));
  }

  ::testing::AssertionResult GetRootPage(
      ledger::LedgerRepositoryPtr* ledger_repository,
      f1dl::Array<uint8_t> ledger_name,
      ledger::PagePtr* page) {
    ledger::Status status;
    ledger::LedgerPtr ledger;
    (*ledger_repository)
        ->GetLedger(std::move(ledger_name), ledger.NewRequest(),
                    callback::Capture(MakeQuitTask(), &status));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure()
             << "GetLedger failed with status " << status;
    }

    ledger->GetRootPage(page->NewRequest(),
                        callback::Capture(MakeQuitTask(), &status));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure()
             << "GetRootPage failed with status " << status;
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult GetPageEntryCount(ledger::PagePtr* page,
                                               size_t* entry_count) {
    ledger::Status status;
    ledger::PageSnapshotPtr snapshot;
    (*page)->GetSnapshot(snapshot.NewRequest(), nullptr, nullptr,
                         callback::Capture(MakeQuitTask(), &status));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure()
             << "GetSnapshot failed with status " << status;
    }
    f1dl::Array<ledger::InlinedEntryPtr> entries;
    f1dl::Array<uint8_t> next_token;
    snapshot->GetEntriesInline(
        nullptr, nullptr,
        callback::Capture(MakeQuitTask(), &status, &entries, &next_token));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure()
             << "GetEntriesInline failed with status " << status;
    }
    *entry_count = entries.size();
    return ::testing::AssertionSuccess();
  }

  component::ApplicationContext* application_context() {
    return application_context_.get();
  }

 private:
  component::ApplicationControllerPtr ledger_controller_;
  std::vector<std::function<void()>> ledger_shutdown_callbacks_;
  std::unique_ptr<component::ApplicationContext> application_context_;

 protected:
  ledger::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  f1dl::SynchronousInterfacePtr<ledger::Ledger> ledger_;
  f1dl::SynchronousInterfacePtr<ledger::LedgerController> controller_;
};

TEST_F(LedgerEndToEndTest, PutAndGet) {
  Init({});
  ledger::Status status;
  f1dl::SynchronousInterfacePtr<ledger::LedgerRepository> ledger_repository;
  files::ScopedTempDir tmp_dir;
  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), nullptr, f1dl::GetSynchronousProxy(&ledger_repository),
      callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);

  ledger_repository->GetLedger(TestArray(), f1dl::GetSynchronousProxy(&ledger_),
                               &status);
  ASSERT_EQ(ledger::Status::OK, status);

  f1dl::SynchronousInterfacePtr<ledger::Page> page;
  ledger_->GetRootPage(f1dl::GetSynchronousProxy(&page), &status);
  ASSERT_EQ(ledger::Status::OK, status);
  page->Put(TestArray(), TestArray(), &status);
  EXPECT_EQ(ledger::Status::OK, status);
  f1dl::SynchronousInterfacePtr<ledger::PageSnapshot> snapshot;
  page->GetSnapshot(GetSynchronousProxy(&snapshot), nullptr, nullptr, &status);
  EXPECT_EQ(ledger::Status::OK, status);
  fsl::SizedVmoTransportPtr value;
  snapshot->Get(TestArray(), &status, &value);
  EXPECT_EQ(ledger::Status::OK, status);
  std::string value_as_string;
  EXPECT_TRUE(fsl::StringFromVmo(value, &value_as_string));
  EXPECT_TRUE(Equals(TestArray(), value_as_string));
}

TEST_F(LedgerEndToEndTest, Terminate) {
  Init({});
  bool called = false;
  RegisterShutdownCallback([this, &called] {
    called = true;
    message_loop_.PostQuitTask();
  });
  controller_->Terminate();
  RunLoop();
  EXPECT_TRUE(called);
}

// Verifies the cloud erase recovery in case of a cloud that was erased before
// startup.
//
// Expected behavior: Ledger disconnects the clients and the local state is
// cleared.
TEST_F(LedgerEndToEndTest, CloudEraseRecoveryOnInitialCheck) {
  Init({});
  bool ledger_shut_down = false;
  RegisterShutdownCallback([&ledger_shut_down] { ledger_shut_down = true; });

  ledger::Status status;
  ledger::LedgerRepositoryPtr ledger_repository;
  files::ScopedTempDir tmp_dir;
  const std::string content_path = tmp_dir.path() + "/content";
  const std::string deletion_sentinel_path = content_path + "/sentinel";
  ASSERT_TRUE(files::CreateDirectory(content_path));
  ASSERT_TRUE(files::WriteFile(deletion_sentinel_path, "", 0));
  ASSERT_TRUE(files::IsFile(deletion_sentinel_path));

  // Write a fingerprint file, so that Ledger will check if it is still in the
  // cloud device set.
  const std::string fingerprint_path = content_path + "/fingerprint";
  const std::string fingerprint = "bazinga";
  ASSERT_TRUE(files::WriteFile(fingerprint_path, fingerprint.c_str(),
                               fingerprint.size()));

  // Create a cloud provider configured to trigger the cloude erase recovery on
  // initial check.
  ledger::FakeCloudProvider cloud_provider(ledger::CloudEraseOnCheck::YES);
  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  f1dl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      &cloud_provider, cloud_provider_ptr.NewRequest());

  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), std::move(cloud_provider_ptr),
      ledger_repository.NewRequest(),
      callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);

  bool repo_disconnected = false;
  ledger_repository.set_error_handler(
      [&repo_disconnected] { repo_disconnected = true; });

  // Run the message loop until Ledger clears the repo directory and disconnects
  // the client.
  RunLoopUntil([deletion_sentinel_path, &repo_disconnected] {
    return !files::IsFile(deletion_sentinel_path) && repo_disconnected;
  });
  EXPECT_FALSE(files::IsFile(deletion_sentinel_path));
  EXPECT_TRUE(repo_disconnected);

  // Verify that the Ledger app didn't crash.
  EXPECT_FALSE(ledger_shut_down);
}

// Verifies the cloud erase recovery in case of a cloud that is erased while
// Ledger is connected to it.
//
// Expected behavior: Ledger disconnects the clients and the local state is
// cleared.
TEST_F(LedgerEndToEndTest, CloudEraseRecoveryFromTheWatcher) {
  Init({});
  bool ledger_shut_down = false;
  RegisterShutdownCallback([&ledger_shut_down] { ledger_shut_down = true; });

  ledger::Status status;
  ledger::LedgerRepositoryPtr ledger_repository;
  files::ScopedTempDir tmp_dir;
  const std::string content_path = tmp_dir.path() + "/content";
  const std::string deletion_sentinel_path = content_path + "/sentinel";
  ASSERT_TRUE(files::CreateDirectory(content_path));
  ASSERT_TRUE(files::WriteFile(deletion_sentinel_path, "", 0));
  ASSERT_TRUE(files::IsFile(deletion_sentinel_path));

  // Create a cloud provider configured to trigger the cloud erase recovery
  // while Ledger is connected.
  ledger::FakeCloudProvider cloud_provider(ledger::CloudEraseOnCheck::NO,
                                           ledger::CloudEraseFromWatcher::YES);
  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  f1dl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      &cloud_provider, cloud_provider_ptr.NewRequest());

  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), std::move(cloud_provider_ptr),
      ledger_repository.NewRequest(),
      callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);

  bool repo_disconnected = false;
  ledger_repository.set_error_handler(
      [&repo_disconnected] { repo_disconnected = true; });

  // Run the message loop until Ledger clears the repo directory and disconnects
  // the client.
  RunLoopUntil([deletion_sentinel_path, &repo_disconnected] {
    return !files::IsFile(deletion_sentinel_path) && repo_disconnected;
  });
  EXPECT_FALSE(files::IsFile(deletion_sentinel_path));
  EXPECT_TRUE(repo_disconnected);

  // Verify that the Ledger app didn't crash.
  EXPECT_FALSE(ledger_shut_down);
}

TEST_F(LedgerEndToEndTest, ShutDownWhenCloudProviderDisconnects) {
  Init({});
  bool ledger_app_shut_down = false;
  RegisterShutdownCallback(
      [&ledger_app_shut_down] { ledger_app_shut_down = true; });
  ledger::Status status;
  files::ScopedTempDir tmp_dir;

  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  ledger::LedgerRepositoryPtr ledger_repository;
  ledger::FakeCloudProvider cloud_provider;
  f1dl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      &cloud_provider, cloud_provider_ptr.NewRequest());
  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), std::move(cloud_provider_ptr),
      ledger_repository.NewRequest(),
      callback::Capture(MakeQuitTask(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);

  bool repo_disconnected = false;
  ledger_repository.set_error_handler(
      [&repo_disconnected] { repo_disconnected = true; });

  cloud_provider_binding.Unbind();

  RunLoopUntil([&repo_disconnected] { return repo_disconnected; });

  // Verify that the Ledger app didn't crash.
  EXPECT_FALSE(ledger_app_shut_down);
}

}  // namespace
}  // namespace e2e_local
}  // namespace test
