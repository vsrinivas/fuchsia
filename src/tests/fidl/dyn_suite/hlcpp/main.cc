// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/dynsuite/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

#include <iostream>

class MethodObserver final {
 public:
  MethodObserver(fidl::InterfacePtr<fidl::dynsuite::Observer>& observer_client,
                 fidl::dynsuite::Method method)
      : observer_client_(observer_client), method_(method) {
    auto on_method_invocation = fidl::dynsuite::OnMethodInvocation();
    on_method_invocation.method = method_;
    on_method_invocation.method_point = fidl::dynsuite::MethodPoint::ENTER;
    observer_client_->Observe(
        fidl::dynsuite::Observation::WithOnMethodInvocation(std::move(on_method_invocation)));
  }
  ~MethodObserver() {
    auto on_method_invocation = fidl::dynsuite::OnMethodInvocation();
    on_method_invocation.method = method_;
    on_method_invocation.method_point = fidl::dynsuite::MethodPoint::EXIT;
    observer_client_->Observe(
        fidl::dynsuite::Observation::WithOnMethodInvocation(std::move(on_method_invocation)));
  }

 private:
  fidl::InterfacePtr<fidl::dynsuite::Observer>& observer_client_;
  fidl::dynsuite::Method method_;
};

class TestOrchestrator;
class ClientTestOrchestrator;

class EntryImpl : public fidl::dynsuite::Entry {
 public:
  void StartServerTest(
      fidl::InterfaceRequest<fidl::dynsuite::ServerTest> server_end_to_test,
      fidl::InterfaceHandle<fidl::dynsuite::Observer> client_end_to_observer) override;
  void StartClientTest(
      fidl::InterfaceHandle<fidl::dynsuite::ClientTest> client_end_to_test,
      fidl::InterfaceHandle<fidl::dynsuite::Observer> client_end_to_observer) override;
  bool remove_test_orchestrator(TestOrchestrator* to_be_removed);
  bool remove_client_test_orchestrator(ClientTestOrchestrator* to_be_removed);

 private:
  std::vector<std::unique_ptr<TestOrchestrator>> test_orchestrators_;
  std::vector<std::unique_ptr<ClientTestOrchestrator>> client_tests_;
};

class TestImpl : public fidl::dynsuite::ServerTest {
 public:
  TestImpl(fidl::InterfacePtr<fidl::dynsuite::Observer>& observer_client)
      : observer_client_(observer_client) {}
  void OneWayInteractionNoPayload() override {
    MethodObserver method_observer(observer_client_,
                                   fidl::dynsuite::Method::ONE_WAY_INTERACTION_NO_PAYLOAD);
  }

 private:
  [[maybe_unused]] fidl::InterfacePtr<fidl::dynsuite::Observer>& observer_client_;
};

class TestOrchestrator final {
 public:
  TestOrchestrator(fidl::InterfacePtr<fidl::dynsuite::Observer> observer_client,
                   EntryImpl* entry_impl)
      : impl_(observer_client),
        test_server_(&impl_),
        observer_client_(std::move(observer_client)),
        entry_impl_(entry_impl) {
    observer_client_.events().OnProgramPoint = [&](uint64_t program_point) {
      observer_client_->Observe(
          fidl::dynsuite::Observation::WithProgramPoint(std::move(program_point)));
    };
  }
  ~TestOrchestrator() {
    observer_client_->Observe(
        fidl::dynsuite::Observation::WithOnComplete(fidl::dynsuite::OnComplete()));
  }

  void Bind(fidl::InterfaceRequest<fidl::dynsuite::ServerTest> server_end_to_test) {
    observer_client_->Observe(fidl::dynsuite::Observation::WithOnBind(fidl::dynsuite::OnBind()));
    test_server_.Bind(std::move(server_end_to_test));
    watch_is_bound_to_observe_unbinding();
  }

  fidl::InterfacePtr<fidl::dynsuite::Observer>& observer_client() { return observer_client_; }

 private:
  void watch_is_bound_to_observe_unbinding() {
    if (test_server_.is_bound()) {
      async::PostDelayedTask(
          async_get_default_dispatcher(), [&] { watch_is_bound_to_observe_unbinding(); },
          zx::duration(zx_duration_from_msec(50)));
    } else {
      observer_client_->Observe(
          fidl::dynsuite::Observation::WithOnUnbind(fidl::dynsuite::OnUnbind()));
      entry_impl_->remove_test_orchestrator(this);
    }
  }

  TestImpl impl_;
  fidl::Binding<fidl::dynsuite::ServerTest> test_server_;
  fidl::InterfacePtr<fidl::dynsuite::Observer> observer_client_;
  EntryImpl* entry_impl_;
};

void EntryImpl::StartServerTest(
    fidl::InterfaceRequest<fidl::dynsuite::ServerTest> server_end_to_test,
    fidl::InterfaceHandle<fidl::dynsuite::Observer> client_end_to_observer) {
  auto observer_client = client_end_to_observer.Bind();
  auto test_orchestrator = std::make_unique<TestOrchestrator>(std::move(observer_client), this);

  MethodObserver method_observer(test_orchestrator->observer_client(),
                                 fidl::dynsuite::Method::START_SERVER_TEST);

  test_orchestrator->Bind(std::move(server_end_to_test));
  test_orchestrators_.push_back(std::move(test_orchestrator));
}

class ClientTestOrchestrator final {
 public:
  ClientTestOrchestrator(EntryImpl* entry_impl,
                         fidl::InterfaceHandle<fidl::dynsuite::ClientTest> client_end_to_test,
                         fidl::InterfaceHandle<fidl::dynsuite::Observer> client_end_to_observer)
      : entry_impl_(entry_impl) {
    observer_client_ = client_end_to_observer.Bind();
    observer_client_.events().OnProgramPoint = [&](uint64_t program_point) {
      observer_client_->Observe(
          fidl::dynsuite::Observation::WithProgramPoint(std::move(program_point)));
    };
    client_test_client_ = client_end_to_test.Bind();
    client_test_client_.events().OnPleaseDo = [&](auto action) {
      switch (action.Which()) {
        case fidl::dynsuite::ClientAction::kCloseChannel:
          entry_impl_->remove_client_test_orchestrator(this);
          break;
        case fidl::dynsuite::ClientAction::kInvoke:
          switch (action.invoke()) {
            case fidl::dynsuite::Method::ONE_WAY_INTERACTION_NO_PAYLOAD:
              client_test_client_->OneWayInteractionNoPayload();
              break;
            default:
              ZX_PANIC("unexpected");
              break;
          }
          break;
        default:
          ZX_PANIC("unexpected");
          break;
      }
    };
    watch_is_bound_to_observe_unbinding();
  }

  ~ClientTestOrchestrator() {
    observer_client_->Observe(
        fidl::dynsuite::Observation::WithOnComplete(fidl::dynsuite::OnComplete()));
  }

 private:
  void watch_is_bound_to_observe_unbinding() {
    if (client_test_client_.is_bound()) {
      async::PostDelayedTask(
          async_get_default_dispatcher(), [&] { watch_is_bound_to_observe_unbinding(); },
          zx::duration(zx_duration_from_msec(50)));
    } else {
      observer_client_->Observe(
          fidl::dynsuite::Observation::WithOnUnbind(fidl::dynsuite::OnUnbind()));
      entry_impl_->remove_client_test_orchestrator(this);
    }
  }

  EntryImpl* entry_impl_;
  fidl::InterfacePtr<fidl::dynsuite::ClientTest> client_test_client_;
  fidl::InterfacePtr<fidl::dynsuite::Observer> observer_client_;
};

void EntryImpl::StartClientTest(
    fidl::InterfaceHandle<fidl::dynsuite::ClientTest> client_end_to_test,
    fidl::InterfaceHandle<fidl::dynsuite::Observer> client_end_to_observer) {
  client_tests_.push_back(std::make_unique<ClientTestOrchestrator>(
      this, std::move(client_end_to_test), std::move(client_end_to_observer)));
}

bool EntryImpl::remove_test_orchestrator(TestOrchestrator* to_be_removed) {
  for (auto it = test_orchestrators_.begin(); it != test_orchestrators_.end(); ++it) {
    if (it->get() == to_be_removed) {
      test_orchestrators_.erase(it);
      return true;
    }
  }
  return false;
}

bool EntryImpl::remove_client_test_orchestrator(ClientTestOrchestrator* to_be_removed) {
  for (auto it = client_tests_.begin(); it != client_tests_.end(); ++it) {
    if (it->get() == to_be_removed) {
      client_tests_.erase(it);
      return true;
    }
  }
  return false;
}

int main(int argc, const char** argv) {
  std::cout << "HLCPP server: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  EntryImpl impl;
  fidl::Binding<fidl::dynsuite::Entry> binding(&impl);
  fidl::InterfaceRequestHandler<fidl::dynsuite::Entry> handler =
      [&](fidl::InterfaceRequest<fidl::dynsuite::Entry> server_end) {
        binding.Bind(std::move(server_end));
      };
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(std::move(handler));

  std::cout << "HLCPP server: ready!" << std::endl;
  return loop.Run();
}
