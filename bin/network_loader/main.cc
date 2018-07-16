// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include <unordered_map>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zx/time.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/time/time_delta.h"

namespace {

namespace http = ::fuchsia::net::oldhttp;

class RetryingLoader {
 public:
  RetryingLoader(http::URLLoaderPtr url_loader, const std::string& url,
                 fuchsia::sys::Loader::LoadComponentCallback callback)
      : url_loader_(std::move(url_loader)),
        url_(url),
        callback_(std::move(callback)),
        // TODO(rosswang): deadline support
        quiet_tries_(5),
        // TODO(rosswang): randomness
        retry_delay_(fxl::TimeDelta::FromSeconds(1)),
        weak_ptr_factory_(this) {}

  void Attempt() {
    url_loader_->Start(NewRequest(),
                       [weak_this = weak_ptr_factory_.GetWeakPtr()](
                           const http::URLResponse& response) {
                         if (weak_this) {
                           weak_this->ProcessResponse(response);
                         }
                       });
  }

  void SetDeleter(fit::closure fn) { deleter_ = std::move(fn); }

 private:
  // Need to create a new request each time because a URLRequest's body can
  // potentially contain a VMO handle and so can't be cloned.
  http::URLRequest NewRequest() const {
    http::URLRequest request;
    request.method = "GET";
    request.url = url_;
    request.auto_follow_redirects = true;
    request.response_body_mode = http::ResponseBodyMode::SIZED_BUFFER;
    return request;
  }

  void ProcessResponse(const http::URLResponse& response) {
    if (response.status_code == 200) {
      auto package = fuchsia::sys::Package::New();
      package->data =
          fidl::MakeOptional(std::move(response.body->sized_buffer()));
      package->resolved_url = std::move(response.url);
      SendResponse(std::move(package));
    } else if (response.error) {
      Retry(response);
    } else {
      FXL_LOG(WARNING) << "Failed to load application from " << url_ << ": "
                       << response.status_line << " (" << response.status_code
                       << ")";
      SendResponse(nullptr);
    }
  }

  void Retry(const http::URLResponse& response) {
    async::PostDelayedTask(async_get_default_dispatcher(),
                           [weak_this = weak_ptr_factory_.GetWeakPtr()] {
                             if (weak_this) {
                               weak_this->Attempt();
                             }
                           },
                           zx::nsec(retry_delay_.ToNanoseconds()));

    if (quiet_tries_ > 0) {
      FXL_VLOG(2) << "Retrying load of " << url_ << " due to "
                  << response.error->description << " (" << response.error->code
                  << ")";

      quiet_tries_--;
      // TODO(rosswang): Randomness, and factor out the delay fn.
      retry_delay_ =
          fxl::TimeDelta::FromSecondsF(retry_delay_.ToSecondsF() * 1.5f);
    } else if (quiet_tries_ == 0) {
      FXL_LOG(WARNING) << "Error while attempting to load application from "
                       << url_ << ": " << response.error->description << " ("
                       << response.error->code
                       << "); continuing to retry every "
                       << retry_delay_.ToSeconds() << " s.";
      quiet_tries_ = -1;
    }
  }

  void SendResponse(fuchsia::sys::PackagePtr package) {
    FXL_DCHECK(!package || package->resolved_url);
    callback_(std::move(package));
    deleter_();
  }

  const http::URLLoaderPtr url_loader_;
  const std::string url_;
  const fuchsia::sys::Loader::LoadComponentCallback callback_;
  fit::closure deleter_;
  // Tries before an error is printed. No errors will be printed afterwards
  // either.
  int quiet_tries_;
  fxl::TimeDelta retry_delay_;

  fxl::WeakPtrFactory<RetryingLoader> weak_ptr_factory_;
};

class NetworkLoader : public fuchsia::sys::Loader {
 public:
  NetworkLoader()
      : context_(component::StartupContext::CreateFromStartupInfo()) {
    context_->outgoing().AddPublicService(bindings_.GetHandler(this));
    context_->ConnectToEnvironmentService(http_.NewRequest());
  }

  void LoadComponent(fidl::StringPtr url,
                     LoadComponentCallback callback) override {
    http::URLLoaderPtr loader;
    http_->CreateURLLoader(loader.NewRequest());

    auto retrying_loader = std::make_unique<RetryingLoader>(
        std::move(loader), url, std::move(callback));
    RetryingLoader* ref = retrying_loader.get();
    loaders_.emplace(ref, std::move(retrying_loader));
    ref->SetDeleter([this, ref] { loaders_.erase(ref); });
    ref->Attempt();
  }

 private:
  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::sys::Loader> bindings_;

  http::HttpServicePtr http_;
  std::unordered_map<RetryingLoader*, std::unique_ptr<RetryingLoader>> loaders_;
};

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  NetworkLoader app;
  loop.Run();
  return 0;
}
