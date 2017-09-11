// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_service_impl.h"

#include <utility>

#include "garnet/bin/network/net_adapters.h"
#include "garnet/bin/network/net_errors.h"
#include "garnet/bin/network/network_service_impl.h"
#include "garnet/bin/network/url_loader_impl.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"

namespace network {

// Maximum number of slots used to run network requests concurrently.
constexpr size_t kMaxSlots = 255;

// Container for the url loader implementation. The loader is run on his own
// thread.
class NetworkServiceImpl::UrlLoaderContainer
    : public URLLoaderImpl::Coordinator {
 public:
  UrlLoaderContainer(URLLoaderImpl::Coordinator* top_coordinator,
                     fidl::InterfaceRequest<URLLoader> request)
      : request_(std::move(request)),
        top_coordinator_(top_coordinator),
        main_task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
        weak_ptr_factory_(this) {
    weak_ptr_ = weak_ptr_factory_.GetWeakPtr();
  }

  ~UrlLoaderContainer() { Stop(); }

  void Start() {
    stopped_ = false;
    thread_.Run();
    io_task_runner_ = thread_.TaskRunner();
    io_task_runner_->PostTask([this] { StartOnIOThread(); });
  }

  void set_on_done(fxl::Closure on_done) { on_done_ = std::move(on_done); }

 private:
  // URLLoaderImpl::Coordinator:
  void RequestNetworkSlot(
      std::function<void(fxl::Closure)> slot_request) override {
    // On IO Thread.
    main_task_runner_->PostTask(
        [ weak_this = weak_ptr_, slot_request = std::move(slot_request) ] {
          // On Main Thread.
          if (!weak_this)
            return;

          weak_this->top_coordinator_->RequestNetworkSlot([
            weak_this, slot_request = std::move(slot_request)
          ](fxl::Closure on_inactive) {
            if (!weak_this) {
              on_inactive();
              return;
            }
            weak_this->on_inactive_ = std::move(on_inactive);
            weak_this->io_task_runner_->PostTask([
              weak_this, main_task_runner = weak_this->main_task_runner_,
              slot_request = std::move(slot_request)
            ] {
              // On IO Thread.
              slot_request([weak_this, main_task_runner]() {
                main_task_runner->PostTask([weak_this]() {
                  // On Main Thread.
                  if (!weak_this)
                    return;

                  auto on_inactive = std::move(weak_this->on_inactive_);
                  weak_this->on_inactive_ = nullptr;
                  on_inactive();
                });
              });
            });
          });
        });
  }

  void JoinAndNotify() {
    if (joined_)
      return;
    joined_ = true;
    thread_.Join();
    if (on_inactive_)
      on_inactive_();
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
    url_loader_ = std::make_unique<URLLoaderImpl>(this);
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
  URLLoaderImpl::Coordinator* top_coordinator_;
  fxl::Closure on_inactive_;
  fxl::Closure on_done_;
  mtl::Thread thread_;
  bool stopped_ = true;
  bool joined_ = false;

  // There are thread-safe.
  fxl::RefPtr<fxl::TaskRunner> main_task_runner_;
  fxl::RefPtr<fxl::TaskRunner> io_task_runner_;

  // The binding and the implementation can only be accessed on the io thread.
  std::unique_ptr<fidl::Binding<URLLoader>> binding_;
  std::unique_ptr<URLLoaderImpl> url_loader_;

  // Copyable on any thread, but can only be de-referenced on the main thread.
  fxl::WeakPtr<UrlLoaderContainer> weak_ptr_;

  // The weak ptr factory is only accessed on the main thread.
  fxl::WeakPtrFactory<UrlLoaderContainer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UrlLoaderContainer);
};

NetworkServiceImpl::NetworkServiceImpl() : available_slots_(kMaxSlots) {}

NetworkServiceImpl::~NetworkServiceImpl() = default;

void NetworkServiceImpl::AddBinding(
    fidl::InterfaceRequest<NetworkService> request) {
  bindings_.AddBinding(this, std::move(request));
}

void NetworkServiceImpl::CreateURLLoader(
    fidl::InterfaceRequest<URLLoader> request) {
  loaders_.emplace_back(this, std::move(request));
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
  FXL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateWebSocket(mx::channel socket) {
  FXL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateTCPBoundSocket(
    netstack::NetAddressPtr local_address,
    mx::channel bound_socket,
    const CreateTCPBoundSocketCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateTCPConnectedSocket(
    netstack::NetAddressPtr remote_address,
    mx::socket send_stream,
    mx::socket receive_stream,
    mx::channel client_socket,
    const CreateTCPConnectedSocketCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::CreateUDPSocket(mx::channel request) {
  FXL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHttpServer(
    netstack::NetAddressPtr local_address,
    mx::channel delegate,
    const CreateHttpServerCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(MakeNetworkError(network::NETWORK_ERR_NOT_IMPLEMENTED), nullptr);
}

void NetworkServiceImpl::RegisterURLLoaderInterceptor(mx::channel factory) {
  FXL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateHostResolver(mx::channel host_resolver) {
  FXL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::RequestNetworkSlot(
    std::function<void(fxl::Closure)> slot_request) {
  if (available_slots_ == 0) {
    slot_requests_.push(std::move(slot_request));
    return;
  }
  --available_slots_;
  slot_request([this]() { OnSlotReturned(); });
}

void NetworkServiceImpl::OnSlotReturned() {
  FXL_DCHECK(available_slots_ < kMaxSlots);

  if (slot_requests_.empty()) {
    ++available_slots_;
    return;
  }
  auto request = std::move(slot_requests_.front());
  slot_requests_.pop();
  request([this] { OnSlotReturned(); });
}

}  // namespace network
