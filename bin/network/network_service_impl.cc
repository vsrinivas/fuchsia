// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_service_impl.h"

#include <thread>
#include <utility>

#include "apps/network/net_adapters.h"
#include "apps/network/net_errors.h"
#include "apps/network/network_service_impl.h"
#include "apps/network/url_loader_impl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"

namespace network {

// Container for the url loader implementation. The loader is run on his own
// thread.
class NetworkServiceImpl::UrlLoaderContainer {
 public:
  UrlLoaderContainer(fidl::InterfaceRequest<URLLoader> request)
      : request_(std::move(request)),
        main_task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()) {}

  ~UrlLoaderContainer() { Stop(); }

  void Start() {
    stopped_ = false;
    thread_ = mtl::CreateThread(&io_task_runner_);
    io_task_runner_->PostTask([this] { StartOnIOThread(); });
  }

  void set_on_done(ftl::Closure on_done) { on_done_ = std::move(on_done); }

 private:
  void JoinAndNotify() {
    if (joined_)
      return;
    joined_ = true;
    thread_.join();
    if (on_done_)
      on_done_();
  }

  void Stop() {
    if (stopped_)
      return;
    stopped_ = true;
    io_task_runner_->PostTask([this] { StopOnIOThread(); });
  }

  void StartOnIOThread() {
    url_loader_ = std::make_unique<URLLoaderImpl>();
    binding_ = std::make_unique<fidl::Binding<URLLoader>>(url_loader_.get(),
                                                          std::move(request_));
    binding_->set_connection_error_handler([this] { StopOnIOThread(); });
  }

  void StopOnIOThread() {
    binding_.reset();
    url_loader_.reset();
    mtl::MessageLoop::GetCurrent()->QuitNow();
    main_task_runner_->PostTask([this] { JoinAndNotify(); });
  }

  // This is set on the constructor, and then accessed on the io thread.
  fidl::InterfaceRequest<URLLoader> request_;

  // These variables can only be accessed on the main thread.
  ftl::Closure on_done_;
  std::thread thread_;
  bool stopped_ = true;
  bool joined_ = false;

  // There are thread-safe.
  ftl::RefPtr<ftl::TaskRunner> main_task_runner_;
  ftl::RefPtr<ftl::TaskRunner> io_task_runner_;

  // The binding and the implementation can only be accessed on the io thread.
  std::unique_ptr<fidl::Binding<URLLoader>> binding_;
  std::unique_ptr<URLLoaderImpl> url_loader_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UrlLoaderContainer);
};

NetworkServiceImpl::NetworkServiceImpl() = default;

NetworkServiceImpl::~NetworkServiceImpl() = default;

void NetworkServiceImpl::AddBinding(
    fidl::InterfaceRequest<NetworkService> request) {
  bindings_.AddBinding(this, std::move(request));
}

void NetworkServiceImpl::CreateURLLoader(
    fidl::InterfaceRequest<URLLoader> loader) {
  loaders_.emplace_back(std::move(loader));
  UrlLoaderContainer* container = &loaders_.back();
  container->set_on_done([this, container] {
    loaders_.erase(std::find_if(loaders_.begin(), loaders_.end(),
                                [container](const UrlLoaderContainer& value) {
                                  return container == &value;
                                }));
  });
  container->Start();
}

void NetworkServiceImpl::GetCookieStore(mx::channel cookie_store) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateWebSocket(mx::channel socket) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateTCPBoundSocket(
    NetAddressPtr local_address,
    mx::channel bound_socket,
    const CreateTCPBoundSocketCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateTCPConnectedSocket(
    NetAddressPtr remote_address,
    mx::datapipe_consumer send_stream,
    mx::datapipe_producer receive_stream,
    mx::channel client_socket,
    const CreateTCPConnectedSocketCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateUDPSocket(mx::channel request) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHttpServer(
    NetAddressPtr local_address,
    mx::channel delegate,
    const CreateHttpServerCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::RegisterURLLoaderInterceptor(mx::channel factory) {
  FTL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHostResolver(mx::channel host_resolver) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace network
