// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/callback/capture.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/gtest/real_loop_fixture.h"
#include "lib/svc/cpp/services.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/cloud_provider/fake_cloud_provider.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"

namespace test {
namespace e2e_local {
namespace {

template <class A>
bool Equals(const fidl::VectorPtr<uint8_t>& a1, const A& a2) {
  if (a1->size() != a2.size())
    return false;
  return memcmp(a1->data(), a2.data(), a1->size()) == 0;
}

fidl::VectorPtr<uint8_t> TestArray() {
  std::string value = "value";
  fidl::VectorPtr<uint8_t> result(value.size());
  memcpy(&result->at(0), &value[0], value.size());
  return result;
}

class LedgerEndToEndTest : public gtest::RealLoopFixture {
 public:
  LedgerEndToEndTest()
      : startup_context_(
            fuchsia::sys::StartupContext::CreateFromStartupInfoNotChecked()) {}
  ~LedgerEndToEndTest() override {}

 protected:
  void Init(std::vector<std::string> additional_args) {
    fuchsia::sys::Services child_services;
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "ledger";
    launch_info.directory_request = child_services.NewRequest();
    launch_info.arguments.push_back("--no_minfs_wait");
    launch_info.arguments.push_back("--disable_reporting");
    for (auto& additional_arg : additional_args) {
      launch_info.arguments.push_back(additional_arg);
    }
    startup_context()->launcher()->CreateComponent(
        std::move(launch_info), ledger_controller_.NewRequest());

    ledger_controller_.set_error_handler([this] {
      for (const auto& callback : ledger_shutdown_callbacks_) {
        callback();
      }
    });

    child_services.ConnectToService(ledger_repository_factory_.NewRequest());
    child_services.ConnectToService(controller_.NewRequest());
  }

  void RegisterShutdownCallback(fit::function<void()> callback) {
    ledger_shutdown_callbacks_.push_back(std::move(callback));
  }

  ::testing::AssertionResult GetRootPage(
      ledger_internal::LedgerRepositoryPtr* ledger_repository,
      fidl::VectorPtr<uint8_t> ledger_name, ledger::PagePtr* page) {
    ledger::Status status;
    ledger::LedgerPtr ledger;
    (*ledger_repository)
        ->GetLedger(std::move(ledger_name), ledger.NewRequest(),
                    callback::Capture(QuitLoopClosure(), &status));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure()
             << "GetLedger failed with status " << status;
    }

    ledger->GetRootPage(page->NewRequest(),
                        callback::Capture(QuitLoopClosure(), &status));
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
    (*page)->GetSnapshot(snapshot.NewRequest(),
                         fidl::VectorPtr<uint8_t>::New(0), nullptr,
                         callback::Capture(QuitLoopClosure(), &status));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure()
             << "GetSnapshot failed with status " << status;
    }
    fidl::VectorPtr<ledger::InlinedEntry> entries;
    std::unique_ptr<ledger::Token> next_token;
    snapshot->GetEntriesInline(
        fidl::VectorPtr<uint8_t>::New(0), nullptr,
        callback::Capture(QuitLoopClosure(), &status, &entries, &next_token));
    RunLoop();
    if (status != ledger::Status::OK) {
      return ::testing::AssertionFailure()
             << "GetEntriesInline failed with status " << status;
    }
    *entry_count = entries->size();
    return ::testing::AssertionSuccess();
  }

  fuchsia::sys::StartupContext* startup_context() {
    return startup_context_.get();
  }

 private:
  fuchsia::sys::ComponentControllerPtr ledger_controller_;
  std::vector<fit::function<void()>> ledger_shutdown_callbacks_;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;

 protected:
  ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  fidl::SynchronousInterfacePtr<ledger::Ledger> ledger_;
  fidl::SynchronousInterfacePtr<ledger_internal::LedgerController> controller_;
};

TEST_F(LedgerEndToEndTest, PutAndGet) {
  Init({});
  ledger::Status status;
  fidl::SynchronousInterfacePtr<ledger_internal::LedgerRepository>
      ledger_repository;
  files::ScopedTempDir tmp_dir;
  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), nullptr, ledger_repository.NewRequest(),
      callback::Capture(QuitLoopClosure(), &status));
  RunLoop();
  ASSERT_EQ(ledger::Status::OK, status);

  ledger_repository->GetLedger(TestArray(), ledger_.NewRequest(), &status);
  ASSERT_EQ(ledger::Status::OK, status);

  fidl::SynchronousInterfacePtr<ledger::Page> page;
  ledger_->GetRootPage(page.NewRequest(), &status);
  ASSERT_EQ(ledger::Status::OK, status);
  page->Put(TestArray(), TestArray(), &status);
  EXPECT_EQ(ledger::Status::OK, status);
  fidl::SynchronousInterfacePtr<ledger::PageSnapshot> snapshot;
  page->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    nullptr, &status);
  EXPECT_EQ(ledger::Status::OK, status);
  fuchsia::mem::BufferPtr value;
  snapshot->Get(TestArray(), &status, &value);
  EXPECT_EQ(ledger::Status::OK, status);
  std::string value_as_string;
  EXPECT_TRUE(fsl::StringFromVmo(*value, &value_as_string));
  EXPECT_TRUE(Equals(TestArray(), value_as_string));
}

TEST_F(LedgerEndToEndTest, Terminate) {
  Init({});
  bool called = false;
  RegisterShutdownCallback([this, &called] {
    called = true;
    QuitLoop();
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
  ledger_internal::LedgerRepositoryPtr ledger_repository;
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
  auto cloud_provider =
      ledger::FakeCloudProvider::Builder()
          .SetCloudEraseOnCheck(ledger::CloudEraseOnCheck::YES)
          .Build();
  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  fidl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      cloud_provider.get(), cloud_provider_ptr.NewRequest());

  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), std::move(cloud_provider_ptr),
      ledger_repository.NewRequest(),
      callback::Capture(QuitLoopClosure(), &status));
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
  ledger_internal::LedgerRepositoryPtr ledger_repository;
  files::ScopedTempDir tmp_dir;
  const std::string content_path = tmp_dir.path() + "/content";
  const std::string deletion_sentinel_path = content_path + "/sentinel";
  ASSERT_TRUE(files::CreateDirectory(content_path));
  ASSERT_TRUE(files::WriteFile(deletion_sentinel_path, "", 0));
  ASSERT_TRUE(files::IsFile(deletion_sentinel_path));

  // Create a cloud provider configured to trigger the cloud erase recovery
  // while Ledger is connected.
  auto cloud_provider =
      ledger::FakeCloudProvider::Builder()
          .SetCloudEraseFromWatcher(ledger::CloudEraseFromWatcher::YES)
          .Build();
  cloud_provider::CloudProviderPtr cloud_provider_ptr;
  fidl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      cloud_provider.get(), cloud_provider_ptr.NewRequest());

  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), std::move(cloud_provider_ptr),
      ledger_repository.NewRequest(),
      callback::Capture(QuitLoopClosure(), &status));
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
  ledger_internal::LedgerRepositoryPtr ledger_repository;
  ledger::FakeCloudProvider cloud_provider;
  fidl::Binding<cloud_provider::CloudProvider> cloud_provider_binding(
      &cloud_provider, cloud_provider_ptr.NewRequest());
  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), std::move(cloud_provider_ptr),
      ledger_repository.NewRequest(),
      callback::Capture(QuitLoopClosure(), &status));
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
