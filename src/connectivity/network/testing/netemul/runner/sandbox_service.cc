// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox_service.h"

#include <lib/async/task.h>
#include <lib/sys/cpp/service_directory.h>

namespace netemul {

class SandboxBinding : public fuchsia::netemul::sandbox::Sandbox {
 public:
  using FSandbox = fuchsia::netemul::sandbox::Sandbox;
  using OnDestroyedCallback = fit::function<void(const SandboxBinding*)>;

  SandboxBinding(fidl::InterfaceRequest<FSandbox> req,
                 std::unique_ptr<async::Loop> loop, SandboxService* parent)
      : loop_(std::move(loop)),
        binding_(this, std::move(req), loop_->dispatcher()),
        parent_(parent) {
    binding_.set_error_handler([this](zx_status_t err) {
      environments_.clear();
      parent_->BindingClosed(this);
    });

    auto services = sys::ServiceDirectory::CreateFromNamespace();
    services->Connect(parent_env_.NewRequest(loop_->dispatcher()));
    parent_env_.set_error_handler([this](zx_status_t err) {
      FXL_LOG(ERROR) << "Lost connection to parent environment";
    });

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

  std::unique_ptr<async::Loop> loop_;
  std::shared_ptr<SandboxEnv> shared_env_;
  fidl::Binding<FSandbox> binding_;
  std::vector<std::unique_ptr<ManagedEnvironment>> environments_;
  fuchsia::sys::EnvironmentPtr parent_env_;
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
  return
      [this](fidl::InterfaceRequest<fuchsia::netemul::sandbox::Sandbox> req) {
        // Create each SandboxBinding in its own thread.
        // A common usage pattern for SandboxService is to connect to the
        // service in each test in a rust create test suite. Rust crate tests
        // run in parallel, so enclosing each binding in its own thread will
        // makes a bit more sense to service everything independently.
        auto loop =
            std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);

        bindings_.push_back(std::make_unique<SandboxBinding>(
            std::move(req), std::move(loop), this));
      };
}

SandboxService::SandboxService(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

SandboxService::~SandboxService() = default;

}  // namespace netemul
