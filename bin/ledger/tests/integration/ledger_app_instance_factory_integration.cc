// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <thread>

#include <lib/async/cpp/task.h>

#include "garnet/lib/callback/synchronous_task.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/bin/ledger/testing/cloud_provider/fake_cloud_provider.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/socket/socket_pair.h"
#include "peridot/lib/socket/socket_writer.h"

namespace test {
namespace integration {
namespace {


// TODO(ZX-1819): Delete the following two functions once there is no
//longer a dependency on Environment's main_runner() method.
void RunMessageLoop(std::mutex* mtx,
                    std::condition_variable* cv,
                    fxl::RefPtr<fxl::TaskRunner>* out_task_runner,
                    async_t** out_dispatcher) {
  fsl::SetCurrentThreadName("message_loop");

  fsl::MessageLoop message_loop;
  {
    std::lock_guard<std::mutex> lock(*mtx);
    *out_task_runner = message_loop.task_runner();
    *out_dispatcher = message_loop.async();
  }
  cv->notify_one();

  message_loop.Run();
}

// This method blocks until the thread is spawned and provides us with an
 // async dispatcher pointer.
std::thread CreateThread(fxl::RefPtr<fxl::TaskRunner>* out_task_runner,
                         async_t** out_dispatcher) {
  FXL_DCHECK(out_task_runner);
  FXL_DCHECK(out_dispatcher);

  std::mutex mtx;
  std::condition_variable cv;

  fxl::RefPtr<fxl::TaskRunner> task_runner;
  async_t* dispatcher = nullptr;
  std::thread thrd(RunMessageLoop, &mtx, &cv, &task_runner, &dispatcher);

  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [&dispatcher] { return dispatcher != nullptr; });

  FXL_DCHECK(task_runner);
  FXL_DCHECK(dispatcher);
  *out_task_runner = task_runner;
  *out_dispatcher = dispatcher;

  return thrd;
}



class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      fxl::RefPtr<fxl::TaskRunner> services_task_runner,
      fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
          repository_factory_request,
      fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
          repository_factory_ptr,
      ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                              ledger::FakeCloudProvider>*
          cloud_provider);
  ~LedgerAppInstanceImpl() override;

 private:
  class LedgerRepositoryFactoryContainer {
   public:
    LedgerRepositoryFactoryContainer(
        fxl::RefPtr<fxl::TaskRunner> task_runner,
        async_t* async,
        fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
            request)
        : environment_(async),
          factory_impl_(&environment_, nullptr),
          factory_binding_(&factory_impl_, std::move(request)) {}
    ~LedgerRepositoryFactoryContainer() {}

   private:
    ledger::Environment environment_;
    ledger::LedgerRepositoryFactoryImpl factory_impl_;
    fidl::Binding<ledger_internal::LedgerRepositoryFactory> factory_binding_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryContainer);
  };

  cloud_provider::CloudProviderPtr MakeCloudProvider() override;

  fxl::RefPtr<fxl::TaskRunner> services_task_runner_;
  std::thread thread_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  async_t* async_;
  ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                          ledger::FakeCloudProvider>* const
      cloud_provider_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    fxl::RefPtr<fxl::TaskRunner> services_task_runner,
    fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
        repository_factory_request,
    fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
        repository_factory_ptr,
    ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                            ledger::FakeCloudProvider>*
        cloud_provider)
    : test::LedgerAppInstanceFactory::LedgerAppInstance(
          integration::RandomArray(1),
          std::move(repository_factory_ptr)),
      services_task_runner_(std::move(services_task_runner)),
      cloud_provider_(cloud_provider) {
  thread_ = CreateThread(&task_runner_, &async_);
  async::PostTask(async_, fxl::MakeCopyable(
      [this, request = std::move(repository_factory_request)]() mutable {
        factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
            task_runner_, async_, std::move(request));
      }));
}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  cloud_provider::CloudProviderPtr cloud_provider;
  services_task_runner_->PostTask(fxl::MakeCopyable(
      [this, request = cloud_provider.NewRequest()]() mutable {
        cloud_provider_->AddBinding(std::move(request));
      }));
  return cloud_provider;
}

LedgerAppInstanceImpl::~LedgerAppInstanceImpl() {
  async::PostTask(async_, [this]() {
    fsl::MessageLoop::GetCurrent()->QuitNow();
    factory_container_.reset();
  });
  thread_.join();
}

}  // namespace

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl(ledger::InjectNetworkError inject_network_error)
      : cloud_provider_(
            ledger::FakeCloudProvider::Builder().SetInjectNetworkError(
                inject_network_error)) {}
  ~LedgerAppInstanceFactoryImpl() override;
  void Init();

  void SetServerId(std::string server_id) override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() override;

 private:
  // Thread used to run services.
  std::thread services_thread_;
  fxl::RefPtr<fxl::TaskRunner> services_task_runner_;
  std::string server_id_ = "server-id";
  ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                          ledger::FakeCloudProvider>
      cloud_provider_;
};

void LedgerAppInstanceFactoryImpl::Init() {
  services_thread_ = fsl::CreateThread(&services_task_runner_);
}

LedgerAppInstanceFactoryImpl::~LedgerAppInstanceFactoryImpl() {
  services_task_runner_->PostTask(
      [] { fsl::MessageLoop::GetCurrent()->QuitNow(); });
  services_thread_.join();
}

void LedgerAppInstanceFactoryImpl::SetServerId(std::string server_id) {
  server_id_ = server_id;
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance() {
  ledger_internal::LedgerRepositoryFactoryPtr repository_factory_ptr;
  fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
      repository_factory_request = repository_factory_ptr.NewRequest();

  auto result = std::make_unique<LedgerAppInstanceImpl>(
      services_task_runner_, std::move(repository_factory_request),
      std::move(repository_factory_ptr), &cloud_provider_);
  return result;
}

}  // namespace integration

std::vector<LedgerAppInstanceFactory*> GetLedgerAppInstanceFactories() {
  static std::vector<std::unique_ptr<LedgerAppInstanceFactory>> factories_impl;
  static std::once_flag flag;

  auto factories_ptr = &factories_impl;
  std::call_once(flag, [factories_ptr] {
    for (auto inject_error :
         {ledger::InjectNetworkError::NO, ledger::InjectNetworkError::YES}) {
      auto factory =
          std::make_unique<integration::LedgerAppInstanceFactoryImpl>(
              inject_error);
      factory->Init();
      factories_ptr->push_back(std::move(factory));
    }
  });
  std::vector<LedgerAppInstanceFactory*> factories;
  for (const auto& factory : factories_impl) {
    factories.push_back(factory.get());
  }

  return factories;
}

}  // namespace test
