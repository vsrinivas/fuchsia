// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox_service.h"
#include <lib/async/task.h>
#include <lib/component/cpp/startup_context.h>

namespace netemul {

class SandboxBinding : public fuchsia::netemul::sandbox::Sandbox {
 public:
  using FSandbox = fuchsia::netemul::sandbox::Sandbox;
  using LaunchOptions = fuchsia::netemul::sandbox::LaunchOptions;
  using OnDestroyedCallback = fit::function<void(const SandboxBinding*)>;

  SandboxBinding(fidl::InterfaceRequest<FSandbox> req,
                 std::unique_ptr<async::Loop> loop, SandboxService* parent)
      : loop_(std::move(loop)),
        binding_(this, std::move(req), loop_->dispatcher()),
        parent_(parent) {
    binding_.set_error_handler([this](zx_status_t err) {
      sandboxes_.clear();
      environments_.clear();
      parent_->BindingClosed(this);
    });

    auto startup_context = component::StartupContext::CreateFromStartupInfo();
    startup_context->ConnectToEnvironmentService(
        parent_env_.NewRequest(loop_->dispatcher()));
    parent_env_.set_error_handler([this](zx_status_t err) {
      FXL_LOG(ERROR) << "Lost connection to parent environment";
    });
  }

  ~SandboxBinding() {
    // Sandbox binding can't be destroyed on the thread of its loop,
    // it'll cause a deadlock upon loop destruction
    ZX_ASSERT(loop_->dispatcher() != async_get_default_dispatcher());
  }

  void CreateEnvironment(
      fidl::InterfaceRequest<ManagedEnvironment::FManagedEnvironment> req,
      ManagedEnvironment::Options options) override {
    SandboxEnv::Ptr env = std::make_shared<SandboxEnv>();
    auto root =
        ManagedEnvironment::CreateRoot(parent_env_, env, std::move(options));
    root->SetRunningCallback(
        [root = root.get(), req = std::move(req)]() mutable {
          root->Bind(std::move(req));
        });

    environments_.push_back(std::move(root));
  }

  void RunTest(
      LaunchOptions options,
      fidl::InterfaceRequest<ManagedEnvironment::FManagedEnvironment> root_env,
      RunTestCallback callback) override {
    SandboxArgs args = {.package = std::move(options.package_url),
                        .args = std::move(options.arguments),
                        .cmx_facet_override = std::move(options.cmx_override)};
    auto& sandbox = sandboxes_.emplace_back(
        std::make_unique<::netemul::Sandbox>(std::move(args)));

    if (root_env.is_valid()) {
      sandbox->SetRootEnvironmentCreatedCallback(
          [root_env = std::move(root_env)](ManagedEnvironment* root) mutable {
            root->Bind(std::move(root_env));
          });
    }

    sandbox->SetTerminationCallback(
        [box = sandbox.get(), this, callback = std::move(callback)](
            int64_t code, ::netemul::Sandbox::TerminationReason reason) {
          callback(code, reason);
          DeleteSandbox(box);
        });

    sandbox->Start(loop_->dispatcher());
  }

  void DeleteSandbox(const ::netemul::Sandbox* sandbox) {
    for (auto i = sandboxes_.begin(); i != sandboxes_.end(); i++) {
      if (i->get() == sandbox) {
        sandboxes_.erase(i);
        return;
      }
    }
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  fidl::Binding<FSandbox> binding_;
  std::vector<std::unique_ptr<::netemul::Sandbox>> sandboxes_;
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
        if (loop->StartThread("sandbox-thread") != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to start thread for sandbox";
          return;
        }

        bindings_.push_back(std::make_unique<SandboxBinding>(
            std::move(req), std::move(loop), this));
      };
}

SandboxService::SandboxService(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

SandboxService::~SandboxService() = default;

}  // namespace netemul