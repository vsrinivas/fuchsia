// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "lib/app/cpp/application_context.h"
#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "<fuchsia/cpp/network.h>"

namespace {

class RetryingLoader {
 public:
  RetryingLoader(
      network::URLLoaderPtr url_loader, const std::string& url,
      const component::ApplicationLoader::LoadApplicationCallback& callback)
      : url_loader_(std::move(url_loader)),
        url_(url),
        callback_(callback),
        // TODO(rosswang): deadline support
        quiet_tries_(5),
        // TODO(rosswang): randomness
        retry_delay_(fxl::TimeDelta::FromSeconds(1)),
        weak_ptr_factory_(this) {}

  void Attempt() {
    url_loader_->Start(NewRequest(),
                       [weak_this = weak_ptr_factory_.GetWeakPtr()](
                           const network::URLResponsePtr& response) {
                         if (weak_this) {
                           weak_this->ProcessResponse(response);
                         }
                       });
  }

  void SetDeleter(const fxl::Closure& fn) { deleter_ = fn; }

 private:
  // Need to create a new request each time because a URLRequest's body can
  // potentially contain a VMO handle and so can't be cloned.
  network::URLRequestPtr NewRequest() const {
    auto request = network::URLRequest::New();
    request->method = "GET";
    request->url = url_;
    request->auto_follow_redirects = true;
    request->response_body_mode =
        network::URLRequest::ResponseBodyMode::SIZED_BUFFER;
    return request;
  }

  void ProcessResponse(const network::URLResponsePtr& response) {
    if (response->status_code == 200) {
      auto package = component::ApplicationPackage::New();
      package->data = std::move(response->body->get_sized_buffer());
      package->resolved_url = std::move(response->url);
      SendResponse(std::move(package));
    } else if (response->error) {
      Retry(response);
    } else {
      FXL_LOG(WARNING) << "Failed to load application from " << url_ << ": "
                       << response->status_line << " (" << response->status_code
                       << ")";
      SendResponse(nullptr);
    }
  }

  void Retry(const network::URLResponsePtr& response) {
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [weak_this = weak_ptr_factory_.GetWeakPtr()] {
          if (weak_this) {
            weak_this->Attempt();
          }
        },
        retry_delay_);

    if (quiet_tries_ > 0) {
      FXL_VLOG(2) << "Retrying load of " << url_ << " due to "
                  << response->error->description << " ("
                  << response->error->code << ")";

      quiet_tries_--;
      // TODO(rosswang): Randomness, and factor out the delay fn.
      retry_delay_ =
          fxl::TimeDelta::FromSecondsF(retry_delay_.ToSecondsF() * 1.5f);
    } else if (quiet_tries_ == 0) {
      FXL_LOG(WARNING) << "Error while attempting to load application from "
                       << url_ << ": " << response->error->description << " ("
                       << response->error->code
                       << "); continuing to retry every "
                       << retry_delay_.ToSeconds() << " s.";
      quiet_tries_ = -1;
    }
  }

  void SendResponse(component::ApplicationPackagePtr package) {
    FXL_DCHECK(!package || package->resolved_url);
    callback_(std::move(package));
    deleter_();
  }

  const network::URLLoaderPtr url_loader_;
  const std::string url_;
  const component::ApplicationLoader::LoadApplicationCallback callback_;
  fxl::Closure deleter_;
  // Tries before an error is printed. No errors will be printed afterwards
  // either.
  int quiet_tries_;
  fxl::TimeDelta retry_delay_;

  fxl::WeakPtrFactory<RetryingLoader> weak_ptr_factory_;
};

class NetworkApplicationLoader : public component::ApplicationLoader {
 public:
  NetworkApplicationLoader()
      : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing_services()->AddService<component::ApplicationLoader>(
        [this](f1dl::InterfaceRequest<component::ApplicationLoader> request) {
          bindings_.AddBinding(this, std::move(request));
        });

    context_->ConnectToEnvironmentService(net_.NewRequest());
  }

  void LoadApplication(
      const f1dl::StringPtr& url,
      const ApplicationLoader::LoadApplicationCallback& callback) override {
    network::URLLoaderPtr loader;
    net_->CreateURLLoader(loader.NewRequest());

    auto retrying_loader =
        std::make_unique<RetryingLoader>(std::move(loader), url, callback);
    RetryingLoader* ref = retrying_loader.get();
    loaders_.emplace(ref, std::move(retrying_loader));
    ref->SetDeleter([this, ref] { loaders_.erase(ref); });
    ref->Attempt();
  }

 private:
  std::unique_ptr<component::ApplicationContext> context_;
  f1dl::BindingSet<component::ApplicationLoader> bindings_;

  network::NetworkServicePtr net_;
  std::unordered_map<RetryingLoader*, std::unique_ptr<RetryingLoader>> loaders_;
};

}  // namespace

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  NetworkApplicationLoader app;
  loop.Run();
  return 0;
}
