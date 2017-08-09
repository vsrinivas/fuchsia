// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/integration/integration_test.h"

#include <thread>

#include "apps/ledger/src/app/erase_remote_repository_operation.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/callback/synchronous_task.h"
#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "apps/ledger/src/glue/socket/socket_writer.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/ledger/src/test/fake_token_provider.h"
#include "apps/ledger/src/test/integration/test_utils.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"

namespace test {
namespace integration {
namespace {
class LedgerAppInstanceImpl final : public IntegrationTest::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      ftl::RefPtr<ftl::TaskRunner> services_task_runner,
      std::function<network::NetworkServicePtr()> network_factory);
  ~LedgerAppInstanceImpl() override;
  void Init();

  ledger::LedgerRepositoryFactory* ledger_repository_factory() override {
    return ledger_repository_factory_.get();
  }

  ledger::LedgerRepositoryPtr GetTestLedgerRepository() override;
  ledger::LedgerPtr GetTestLedger() override;
  ledger::PagePtr GetTestPage() override;
  ledger::PagePtr GetPage(const fidl::Array<uint8_t>& page_id,
                          ledger::Status expected_status) override;
  void DeletePage(const fidl::Array<uint8_t>& page_id,
                  ledger::Status expected_status) override;
  void UnbindTokenProvider() override;

 private:
  class LedgerRepositoryFactoryContainer
      : public ledger::LedgerRepositoryFactoryImpl::Delegate {
   public:
    LedgerRepositoryFactoryContainer(
        ftl::RefPtr<ftl::TaskRunner> task_runner,
        std::function<network::NetworkServicePtr()> network_factory,
        fidl::InterfaceRequest<ledger::LedgerRepositoryFactory> request)
        : network_service_(task_runner, std::move(network_factory)),
          environment_(task_runner, &network_service_),
          factory_impl_(
              this,
              &environment_,
              ledger::LedgerRepositoryFactoryImpl::ConfigPersistence::FORGET),
          factory_binding_(&factory_impl_, std::move(request)) {}
    ~LedgerRepositoryFactoryContainer() override {}

   private:
    // LedgerRepositoryFactoryImpl::Delegate:
    void EraseRepository(
        ledger::
            EraseRemoteRepositoryOperation /*erase_remote_repository_operation*/
        ,
        std::function<void(bool)> callback) override {
      FTL_NOTIMPLEMENTED();
      callback(false);
    }
    ledger::NetworkServiceImpl network_service_;
    ledger::Environment environment_;
    ledger::LedgerRepositoryFactoryImpl factory_impl_;
    fidl::Binding<ledger::LedgerRepositoryFactory> factory_binding_;

    FTL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryContainer);
  };

  ftl::RefPtr<ftl::TaskRunner> services_task_runner_;
  files::ScopedTempDir tmp_dir_;
  test::FakeTokenProvider token_provider_;
  fidl::BindingSet<modular::auth::TokenProvider> token_provider_bindings_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  std::thread thread_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  ledger::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  ledger::LedgerPtr ledger_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    ftl::RefPtr<ftl::TaskRunner> services_task_runner,
    std::function<network::NetworkServicePtr()> network_factory)
    : services_task_runner_(std::move(services_task_runner)),
      token_provider_("token", "user_id@exmaple.com", "user_id", "client_id") {
  thread_ = mtl::CreateThread(&task_runner_);
  task_runner_->PostTask(ftl::MakeCopyable([
    this, request = ledger_repository_factory_.NewRequest(),
    network_factory = std::move(network_factory)
  ]() mutable {
    factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
        task_runner_, std::move(network_factory), std::move(request));
  }));
}

LedgerAppInstanceImpl::~LedgerAppInstanceImpl() {
  task_runner_->PostTask([this]() {
    mtl::MessageLoop::GetCurrent()->QuitNow();
    factory_container_.reset();
  });
  thread_.join();
}

void LedgerAppInstanceImpl::Init() {
  ledger_ = GetTestLedger();
}

ledger::LedgerRepositoryPtr LedgerAppInstanceImpl::GetTestLedgerRepository() {
  ledger::LedgerRepositoryPtr repository;

  ledger::FirebaseConfigPtr firebase_config = ledger::FirebaseConfig::New();
  firebase_config->server_id = "server-id";
  firebase_config->api_key = "api_key";

  modular::auth::TokenProviderPtr token_provider;
  EXPECT_TRUE(callback::RunSynchronously(
      services_task_runner_,
      ftl::MakeCopyable(
          [ this, request = token_provider.NewRequest() ]() mutable {
            token_provider_bindings_.AddBinding(&token_provider_,
                                                std::move(request));
          }),
      ftl::TimeDelta::FromSeconds(1)));

  ledger::Status status;
  ledger_repository_factory_->GetRepository(
      tmp_dir_.path(), std::move(firebase_config), std::move(token_provider),
      repository.NewRequest(), [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger_repository_factory_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(ledger::Status::OK, status);
  return repository;
}

ledger::LedgerPtr LedgerAppInstanceImpl::GetTestLedger() {
  ledger::LedgerPtr ledger;

  ledger::LedgerRepositoryPtr repository = GetTestLedgerRepository();
  ledger::Status status;
  repository->GetLedger(RandomArray(1), ledger.NewRequest(),
                        [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(repository.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(ledger::Status::OK, status);
  return ledger;
}

ledger::PagePtr LedgerAppInstanceImpl::GetTestPage() {
  fidl::InterfaceHandle<ledger::Page> page;
  ledger::Status status;

  ledger_->GetPage(nullptr, page.NewRequest(),
                   [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(ledger::Status::OK, status);

  return fidl::InterfacePtr<ledger::Page>::Create(std::move(page));
}

ledger::PagePtr LedgerAppInstanceImpl::GetPage(
    const fidl::Array<uint8_t>& page_id,
    ledger::Status expected_status) {
  ledger::PagePtr page_ptr;
  ledger::Status status;

  ledger_->GetPage(page_id.Clone(), page_ptr.NewRequest(),
                   [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(expected_status, status);

  return page_ptr;
}

void LedgerAppInstanceImpl::DeletePage(const fidl::Array<uint8_t>& page_id,
                                       ledger::Status expected_status) {
  fidl::InterfaceHandle<ledger::Page> page;
  ledger::Status status;

  ledger_->DeletePage(page_id.Clone(),
                      [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(expected_status, status);
}

void LedgerAppInstanceImpl::UnbindTokenProvider() {
  ASSERT_TRUE(callback::RunSynchronously(
      services_task_runner_,
      [this] { token_provider_bindings_.CloseAllBindings(); },
      ftl::TimeDelta::FromSeconds(1)));
}

}  // namespace

void IntegrationTest::SetUp() {
  ::testing::Test::SetUp();
  std::srand(0);
  socket_thread_ = mtl::CreateThread(&socket_task_runner_);
  services_thread_ = mtl::CreateThread(&services_task_runner_);
}

void IntegrationTest::TearDown() {
  socket_task_runner_->PostTask(
      [] { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  services_task_runner_->PostTask(
      [] { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  services_thread_.join();
  socket_thread_.join();

  ::testing::Test::TearDown();
}

mx::socket IntegrationTest::StreamDataToSocket(std::string data) {
  glue::SocketPair sockets;
  socket_task_runner_->PostTask(ftl::MakeCopyable([
    socket = std::move(sockets.socket1), data = std::move(data)
  ]() mutable {
    auto writer = new glue::StringSocketWriter();
    writer->Start(std::move(data), std::move(socket));
  }));
  return std::move(sockets.socket2);
}

std::unique_ptr<IntegrationTest::LedgerAppInstance>
IntegrationTest::NewLedgerAppInstance() {
  auto network_factory = [this]() {
    network::NetworkServicePtr result;
    services_task_runner_->PostTask(
        ftl::MakeCopyable([ this, request = result.NewRequest() ]() mutable {
          network_service_.AddBinding(std::move(request));
        }));
    return result;
  };
  auto result = std::make_unique<LedgerAppInstanceImpl>(
      services_task_runner_, std::move(network_factory));
  result->Init();
  return result;
}

}  // namespace integration
}  // namespace test
