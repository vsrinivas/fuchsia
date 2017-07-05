// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_environment.fidl.h"
#include "apps/ledger/services/internal/internal.fidl-sync.h"
#include "apps/ledger/services/public/ledger.fidl-sync.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
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
  // ::testing::Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    app::ServiceProviderPtr child_services;
    fidl::SynchronousInterfacePtr<ledger::LedgerRepositoryFactory>
        ledger_repository_factory;

    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "file:///system/apps/ledger";
    launch_info->services = child_services.NewRequest();
    launch_info->arguments.push_back("--no_minfs_wait");
    launch_info->arguments.push_back("--no_persisted_config");
    context_->launcher()->CreateApplication(
        std::move(launch_info),
        ledger_repository_factory_controller_.NewRequest());

    ledger_repository_factory_controller_.set_connection_error_handler([this] {
      for (const auto& callback : ledger_shutdown_callbacks_) {
        callback();
      }
    });

    app::ConnectToService(
        child_services.get(),
        fidl::GetSynchronousProxy(&ledger_repository_factory));
    app::ConnectToService(child_services.get(),
                          fidl::GetSynchronousProxy(&controller_));

    Status status;
    fidl::SynchronousInterfacePtr<ledger::LedgerRepository> ledger_repository;
    ledger_repository_factory->GetRepository(
        tmp_dir_.path(), nullptr, nullptr,
        fidl::GetSynchronousProxy(&ledger_repository), &status);
    ASSERT_EQ(Status::OK, status);

    ledger_repository->GetLedger(TestArray(),
                                 fidl::GetSynchronousProxy(&ledger_), &status);
    ASSERT_EQ(Status::OK, status);
  };

  void RegisterShutdownCallback(std::function<void()> callback) {
    ledger_shutdown_callbacks_.push_back(std::move(callback));
  }

 private:
  files::ScopedTempDir tmp_dir_;
  app::ApplicationControllerPtr ledger_repository_factory_controller_;
  std::vector<std::function<void()>> ledger_shutdown_callbacks_;

 protected:
  fidl::SynchronousInterfacePtr<ledger::Ledger> ledger_;
  fidl::SynchronousInterfacePtr<ledger::LedgerController> controller_;
};

TEST_F(LedgerAppTest, PutAndGet) {
  ledger::Status status;
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

}  // namespace
}  // namespace ledger

int main(int argc, char** argv) {
  mtl::MessageLoop loop;
  ledger::loop_ = &loop;
  std::unique_ptr<app::ApplicationContext> context =
      app::ApplicationContext::CreateFromStartupInfo();
  ledger::context_ = context.get();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
