// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox_service.h"

#include <lib/async/task.h>
#include <lib/sys/cpp/service_directory.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <zircon/status.h>

#include <random>

namespace netemul {

class SandboxBinding : public fuchsia::netemul::sandbox::Sandbox {
 public:
  using FSandbox = fuchsia::netemul::sandbox::Sandbox;
  using OnDestroyedCallback = fit::function<void(const SandboxBinding*)>;

  SandboxBinding(fidl::InterfaceRequest<FSandbox> req,
                 fuchsia::sys::EnvironmentControllerPtr env_controller,
                 fuchsia::sys::EnvironmentPtr environment,
                 std::unique_ptr<async::Loop> loop, SandboxService* parent)
      : loop_(std::move(loop)),
        binding_(this),
        parent_env_(std::move(environment)),
        parent_env_ctlr_(std::move(env_controller)),
        parent_(parent) {
    binding_.set_error_handler([this](zx_status_t err) { BindingClosed(); });

    parent_env_ctlr_.set_error_handler([this](zx_status_t err) {
      FXL_LOG(ERROR) << "Lost connection to parent environment controller: "
                     << zx_status_get_string(err);
      BindingClosed();
    });

    parent_env_.set_error_handler([this](zx_status_t err) {
      FXL_LOG(ERROR) << "Lost connection to parent environment: "
                     << zx_status_get_string(err);
      BindingClosed();
    });

    parent_env_ctlr_.events().OnCreated = [this,
                                           req = std::move(req)]() mutable {
      binding_.Bind(std::move(req), loop_->dispatcher());
    };

    if (loop_->StartThread("sandbox-thread") != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to start thread for sandbox";
      parent_->BindingClosed(this);
      return;
    }
  }

  ~SandboxBinding() {
    // Sandbox binding can't be destroyed on the thread of its loop,
    // it'll cause a deadlock upon loop destruction
    ZX_ASSERT(loop_->dispatcher() != async_get_default_dispatcher());
  }

  void CreateEnvironment(
      fidl::InterfaceRequest<ManagedEnvironment::FManagedEnvironment> req,
      ManagedEnvironment::Options options) override {
    auto root = ManagedEnvironment::CreateRoot(parent_env_, shared_env(),
                                               std::move(options));
    root->SetRunningCallback(
        [root = root.get(), req = std::move(req)]() mutable {
          root->Bind(std::move(req));
        });

    environments_.push_back(std::move(root));
  }

  // Gets this sandbox's NetworkContext
  void GetNetworkContext(
      fidl::InterfaceRequest<::fuchsia::netemul::network::NetworkContext>
          network_context) override {
    shared_env()->network_context().GetHandler()(std::move(network_context));
  };

  // Gets this sandbox's SyncManager
  void GetSyncManager(
      fidl::InterfaceRequest<fuchsia::netemul::sync::SyncManager> sync_manager)
      override {
    shared_env()->sync_manager().GetHandler()(std::move(sync_manager));
  };

 private:
  std::shared_ptr<SandboxEnv>& shared_env() {
    ZX_ASSERT(async_get_default_dispatcher() == loop_->dispatcher());
    if (!shared_env_) {
      shared_env_ = std::make_shared<SandboxEnv>();
    }
    return shared_env_;
  }

  void BindingClosed() {
    // can only be called from within the dispatcher loop
    ZX_ASSERT(loop_->dispatcher() == async_get_default_dispatcher());
    // we don't care about environment anymore.
    // unbinding will prevent the error handlers from firing
    // when we destroy the dispatcher.
    parent_env_.Unbind();
    parent_env_ctlr_.Unbind();
    // get rid of child environments and shared services:
    environments_.clear();
    shared_env_ = nullptr;
    parent_->BindingClosed(this);
  }

  std::unique_ptr<async::Loop> loop_;
  std::shared_ptr<SandboxEnv> shared_env_;
  fidl::Binding<FSandbox> binding_;
  std::vector<std::unique_ptr<ManagedEnvironment>> environments_;
  fuchsia::sys::EnvironmentPtr parent_env_;
  fuchsia::sys::EnvironmentControllerPtr parent_env_ctlr_;
  // Pointer to parent SandboxService. Not owned.
  SandboxService* parent_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SandboxBinding);
};

void SandboxService::BindingClosed(netemul::SandboxBinding* binding) {
  async::PostTask(dispatcher_, [binding, this]() {
    for (auto i = bindings_.begin(); i != bindings_.end(); i++) {
      if (i->get() == binding) {
        bindings_.erase(i);
        return;
      }
    }
  });
}

fidl::InterfaceRequestHandler<fuchsia::netemul::sandbox::Sandbox>
SandboxService::GetHandler() {
  return [this](
             fidl::InterfaceRequest<fuchsia::netemul::sandbox::Sandbox> req) {
    // Create each SandboxBinding in its own thread.
    // A common usage pattern for SandboxService is to connect to the
    // service in each test in a rust create test suite. Rust crate tests
    // run in parallel, so enclosing each binding in its own thread will
    // makes a bit more sense to service everything independently.
    auto loop =
        std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);

    fuchsia::sys::EnvironmentPtr env;
    fuchsia::sys::EnvironmentControllerPtr env_ctlr;
    parent_env_->CreateNestedEnvironment(
        env.NewRequest(loop->dispatcher()),
        env_ctlr.NewRequest(loop->dispatcher()),
        fxl::StringPrintf("netemul-%08X-%08X", random_, counter_++), nullptr,
        fuchsia::sys::EnvironmentOptions{
            .delete_storage_on_death = false,
            .kill_on_oom = true,
            .inherit_parent_services = true,
            .allow_parent_runners = true,
        });

    bindings_.push_back(std::make_unique<SandboxBinding>(
        std::move(req), std::move(env_ctlr), std::move(env), std::move(loop),
        this));
  };
}

SandboxService::SandboxService(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher), counter_(0) {
  std::random_device dev;
  std::uniform_int_distribution<uint32_t> rd(0, 0xFFFFFFFF);
  random_ = rd(dev);

  auto services = sys::ServiceDirectory::CreateFromNamespace();
  services->Connect(parent_env_.NewRequest(dispatcher));
}

SandboxService::~SandboxService() = default;

}  // namespace netemul
