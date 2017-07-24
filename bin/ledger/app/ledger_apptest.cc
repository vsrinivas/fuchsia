// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_environment.fidl.h"
#include "apps/ledger/services/internal/internal.fidl-sync.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl-sync.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "apps/test_runner/lib/reporting/gtest_listener.h"
#include "apps/test_runner/lib/reporting/reporter.h"
#include "apps/test_runner/lib/reporting/results_queue.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace ledger {
namespace {

app::ApplicationContext* context_;
mtl::MessageLoop* loop_;

template <class A>
bool Equals(const fidl::Array<uint8_t>& a1, const A& a2) {
  if (a1.size() != a2.size())
    return false;
  return memcmp(a1.data(), a2.data(), a1.size()) == 0;
}

fidl::Array<uint8_t> TestArray() {
  std::string value = "value";
  fidl::Array<uint8_t> result = fidl::Array<uint8_t>::New(value.size());
  memcpy(&result[0], &value[0], value.size());
  return result;
}

class LedgerAppTest : public ::testing::Test {
 public:
  LedgerAppTest() {}
  ~LedgerAppTest() override {}

 protected:
  void Init(std::vector<std::string> additional_args) {
    app::ServiceProviderPtr child_services;
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "ledger";
    launch_info->services = child_services.NewRequest();
    launch_info->arguments.push_back("--no_minfs_wait");
    launch_info->arguments.push_back("--no_persisted_config");
    launch_info->arguments.push_back("--no_statistics_reporting_for_testing");
    for (auto& additional_arg : additional_args) {
      launch_info->arguments.push_back(std::move(additional_arg));
    }
    context_->launcher()->CreateApplication(std::move(launch_info),
                                            ledger_controller_.NewRequest());

    ledger_controller_.set_connection_error_handler([this] {
      for (const auto& callback : ledger_shutdown_callbacks_) {
        callback();
      }
    });

    app::ConnectToService(
        child_services.get(),
        fidl::GetSynchronousProxy(&ledger_repository_factory_));
    app::ConnectToService(child_services.get(),
                          fidl::GetSynchronousProxy(&controller_));
  }

  void RegisterShutdownCallback(std::function<void()> callback) {
    ledger_shutdown_callbacks_.push_back(std::move(callback));
  }

 private:
  app::ApplicationControllerPtr ledger_controller_;
  std::vector<std::function<void()>> ledger_shutdown_callbacks_;

 protected:
  fidl::SynchronousInterfacePtr<ledger::LedgerRepositoryFactory>
      ledger_repository_factory_;
  fidl::SynchronousInterfacePtr<ledger::Ledger> ledger_;
  fidl::SynchronousInterfacePtr<ledger::LedgerController> controller_;
};

TEST_F(LedgerAppTest, PutAndGet) {
  Init({});
  Status status;
  fidl::SynchronousInterfacePtr<ledger::LedgerRepository> ledger_repository;
  files::ScopedTempDir tmp_dir;
  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), nullptr, nullptr,
      fidl::GetSynchronousProxy(&ledger_repository), &status);
  ASSERT_EQ(Status::OK, status);

  ledger_repository->GetLedger(TestArray(), fidl::GetSynchronousProxy(&ledger_),
                               &status);
  ASSERT_EQ(Status::OK, status);

  fidl::SynchronousInterfacePtr<ledger::Page> page;
  ledger_->GetRootPage(fidl::GetSynchronousProxy(&page), &status);
  ASSERT_EQ(Status::OK, status);
  page->Put(TestArray(), TestArray(), &status);
  EXPECT_EQ(Status::OK, status);
  fidl::SynchronousInterfacePtr<ledger::PageSnapshot> snapshot;
  page->GetSnapshot(GetSynchronousProxy(&snapshot), nullptr, nullptr, &status);
  EXPECT_EQ(Status::OK, status);
  mx::vmo value;
  snapshot->Get(TestArray(), &status, &value);
  EXPECT_EQ(Status::OK, status);
  std::string value_as_string;
  EXPECT_TRUE(mtl::StringFromVmo(value, &value_as_string));
  EXPECT_TRUE(Equals(TestArray(), value_as_string));
}

TEST_F(LedgerAppTest, Terminate) {
  Init({});
  bool called = false;
  RegisterShutdownCallback([&called] {
    called = true;
    loop_->PostQuitTask();
  });
  controller_->Terminate();
  loop_->task_runner()->PostDelayedTask([] { loop_->PostQuitTask(); },
                                        ftl::TimeDelta::FromSeconds(1));
  loop_->Run();
  EXPECT_TRUE(called);
}

// Triggers the cloud erased recovery codepath and verifies that:
//  - Ledger disconnects the clients
//  - the repository directory is cleared
TEST_F(LedgerAppTest, CloudErasedRecovery) {
  Init({"--no_network_for_testing", "--trigger_cloud_erased_for_testing"});
  bool ledger_shut_down = false;
  RegisterShutdownCallback([&ledger_shut_down] { ledger_shut_down = true; });

  Status status;
  ledger::LedgerRepositoryPtr ledger_repository;
  files::ScopedTempDir tmp_dir;
  std::string content_path = tmp_dir.path() + "/content";
  std::string deletion_sentinel_path = content_path + "/sentinel";
  ASSERT_TRUE(files::CreateDirectory(content_path));
  ASSERT_TRUE(files::WriteFile(deletion_sentinel_path, "", 0));
  ASSERT_TRUE(files::IsFile(deletion_sentinel_path));

  ledger::FirebaseConfigPtr firebase_config = ledger::FirebaseConfig::New();
  firebase_config->server_id = "network_is_disabled_anyway";
  // Has to be empty so that we don't try to obtain auth tokens.
  firebase_config->api_key = "";
  ledger_repository_factory_->GetRepository(
      tmp_dir.path(), std::move(firebase_config), nullptr,
      ledger_repository.NewRequest(), &status);
  ASSERT_EQ(Status::OK, status);

  bool repo_disconnected = false;
  ledger_repository.set_connection_error_handler(
      [&repo_disconnected] { repo_disconnected = true; });

  // Run the message loop until Ledger clears the repo directory and disconnects
  // the client.
  bool cleared = test::RunGivenLoopUntil(
      loop_, [deletion_sentinel_path, &repo_disconnected] {
        return !files::IsFile(deletion_sentinel_path) && repo_disconnected;
      });
  EXPECT_FALSE(files::IsFile(deletion_sentinel_path));
  EXPECT_TRUE(repo_disconnected);
  EXPECT_TRUE(cleared);

  // Verify that the Ledger app didn't crash.
  EXPECT_FALSE(ledger_shut_down);
}

}  // namespace
}  // namespace ledger

int main(int argc, char** argv) {
  test_runner::ResultsQueue queue;
  test_runner::Reporter reporter(argv[0], &queue);
  test_runner::GTestListener listener(argv[0], &queue);

  mtl::MessageLoop loop;
  ledger::loop_ = &loop;
  std::unique_ptr<app::ApplicationContext> context =
      app::ApplicationContext::CreateFromStartupInfo();
  ledger::context_ = context.get();
  reporter.Start(context.get());

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);
  return status;
}
