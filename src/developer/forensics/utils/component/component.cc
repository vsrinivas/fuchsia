// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/component/component.h"

#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

namespace forensics {
namespace component {
namespace {

constexpr char kComponentDirectory[] = "/tmp/component";
constexpr char kInstanceIndexPath[] = "/tmp/component/instance_index.txt";

// Handles executing the passed callback when the Stop signal is received.
class Lifecycle : public fuchsia::process::lifecycle::Lifecycle {
 public:
  Lifecycle(::fit::closure on_stop) : on_stop_(std::move(on_stop)) {}

  // |fuchsia.process.lifecycle.Lifecycle|
  void Stop() override { on_stop_(); }

 private:
  ::fit::closure on_stop_;
};

}  // namespace

Component::Component(const bool lazy_outgoing_dir)
    : loop_(&kAsyncLoopConfigAttachToCurrentThread),
      dispatcher_(loop_.dispatcher()),
      context_((lazy_outgoing_dir) ? sys::ComponentContext::Create()
                                   : sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      inspector_(context_.get()),
      clock_(),
      instance_index_(InitialInstanceIndex()),
      serving_outgoing_(!lazy_outgoing_dir),
      lifecycle_(nullptr),
      lifecycle_connection_(nullptr) {
  WriteInstanceIndex();

  if (!serving_outgoing_) {
    FX_LOGS(INFO) << "Serving outgoing directory is delayed";
  }
}

Component::Component(async_dispatcher_t* dispatcher, std::unique_ptr<sys::ComponentContext> context,
                     const bool serving_outgoing)
    : loop_(&kAsyncLoopConfigNeverAttachToThread),
      dispatcher_(dispatcher),
      context_(std::move(context)),
      inspector_(context_.get()),
      clock_(),
      instance_index_(InitialInstanceIndex()),
      serving_outgoing_(serving_outgoing),
      lifecycle_(nullptr),
      lifecycle_connection_(nullptr) {
  WriteInstanceIndex();
}

async_dispatcher_t* Component::Dispatcher() { return dispatcher_; }

std::shared_ptr<sys::ServiceDirectory> Component::Services() { return context_->svc(); }

inspect::Node* Component::InspectRoot() { return &(inspector_.root()); }

timekeeper::Clock* Component::Clock() { return &clock_; }

bool Component::IsFirstInstance() const { return instance_index_ == 1; }

zx_status_t Component::RunLoop() { return loop_.Run(); }

void Component::ShutdownLoop() { return loop_.Shutdown(); }

void Component::OnStopSignal(::fit::function<void(::fit::deferred_callback)> on_stop) {
  using ProcLifecycle = fuchsia::process::lifecycle::Lifecycle;

  lifecycle_ = std::make_unique<Lifecycle>([this, on_stop = std::move(on_stop)] {
    on_stop(::fit::defer<::fit::callback<void()>>([this] {
      // Close the channel to indicate to the client that stop procedures have completed.
      lifecycle_connection_->Close(ZX_OK);
    }));
  });
  lifecycle_connection_ = std::make_unique<::fidl::Binding<ProcLifecycle>>(lifecycle_.get());

  ::fidl::InterfaceRequestHandler<ProcLifecycle> handler(
      [this](::fidl::InterfaceRequest<ProcLifecycle> request) mutable {
        lifecycle_connection_->Bind(std::move(request), Dispatcher());
        lifecycle_connection_->set_error_handler([](const zx_status_t status) {
          FX_PLOGS(WARNING, status) << "Lost connection to lifecycle client";
        });
      });
  FX_CHECK(AddPublicService(std::move(handler), "fuchsia.process.lifecycle.Lifecycle") == ZX_OK);
}

size_t Component::InitialInstanceIndex() const {
  // The default is this is the first instance.
  size_t instance_index{1};
  if (!files::IsDirectory(kComponentDirectory) && !files::CreateDirectory(kComponentDirectory)) {
    FX_LOGS(INFO) << "Unable to create " << kComponentDirectory
                  << ", assuming first instance of component";
    return instance_index;
  }

  std::string starts_str;
  if (files::ReadFileToString(kInstanceIndexPath, &starts_str)) {
    instance_index = std::stoull(starts_str) + 1;
  }

  return instance_index;
}

void Component::WriteInstanceIndex() const {
  files::WriteFile(kInstanceIndexPath, std::to_string(instance_index_));
}

}  // namespace component
}  // namespace forensics
