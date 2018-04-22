// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_service_impl.h"

#include <utility>

#include <fdio/limits.h>
#include <lib/async/cpp/task.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/network/net_adapters.h"
#include "garnet/bin/network/net_errors.h"
#include "garnet/bin/network/network_service_impl.h"
#include "garnet/bin/network/url_loader_impl.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace network {

// Number of file descriptors each UrlLoader instance uses. (This
// depends on the implementation of the reactor in third_party/asio:
// currently 2 for pipe, 1 for socket)
constexpr size_t kNumFDPerConnection = 3;
// Number of reserved file descriptors for stdio.
constexpr size_t kNumFDReserved = 3;
// This is some random margin.
constexpr size_t kMargin = 4;
// Maximum number of slots used to run network requests concurrently.
constexpr size_t kMaxSlots = ((FDIO_MAX_FD - kNumFDReserved) / kNumFDPerConnection) - kMargin;

// Container for the url loader implementation. The loader is run on his own
// thread.
class NetworkServiceImpl::UrlLoaderContainer
    : public URLLoaderImpl::Coordinator {
 public:
  UrlLoaderContainer(URLLoaderImpl::Coordinator* top_coordinator,
                     async_t* main_dispatcher,
                     fidl::InterfaceRequest<URLLoader> request)
      : request_(std::move(request)),
        top_coordinator_(top_coordinator),
        main_dispatcher_(main_dispatcher),
        weak_ptr_factory_(this) {
    FXL_DCHECK(main_dispatcher_);
    weak_ptr_ = weak_ptr_factory_.GetWeakPtr();
  }

  ~UrlLoaderContainer() { Stop(); }

  void Start() {
    stopped_ = false;
    io_loop_.StartThread();
    async::PostTask(io_loop_.async(), [this] { StartOnIOThread(); });
  }

  void set_on_done(fxl::Closure on_done) { on_done_ = std::move(on_done); }

 private:
  // URLLoaderImpl::Coordinator:
  void RequestNetworkSlot(
      std::function<void(fxl::Closure)> slot_request) override {
    // On IO Thread.
    async::PostTask(main_dispatcher_,
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
            async::PostTask(weak_this->io_loop_.async(), [
              weak_this, main_dispatcher = weak_this->main_dispatcher_,
              slot_request = std::move(slot_request)
            ] {
              // On IO Thread.
              slot_request([weak_this, main_dispatcher]() {
                async::PostTask(main_dispatcher, [weak_this]() {
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
    io_loop_.JoinThreads();
    if (on_inactive_)
      on_inactive_();
    if (on_done_)
      on_done_();
  }

  void Stop() {
    if (stopped_)
      return;
    stopped_ = true;
    async::PostTask(io_loop_.async(), [this] { StopOnIOThread(); });
  }

  void StartOnIOThread() {
    url_loader_ = std::make_unique<URLLoaderImpl>(this);
    binding_ = std::make_unique<fidl::Binding<URLLoader>>(url_loader_.get(),
                                                          std::move(request_));
    binding_->set_error_handler([this] { StopOnIOThread(); });
  }

  void StopOnIOThread() {
    binding_.reset();
    url_loader_.reset();
    io_loop_.Quit();
    async::PostTask(main_dispatcher_, [this] { JoinAndNotify(); });
  }

  // This is set on the constructor, and then accessed on the io thread.
  fidl::InterfaceRequest<URLLoader> request_;

  // These variables can only be accessed on the main thread.
  URLLoaderImpl::Coordinator* top_coordinator_;
  fxl::Closure on_inactive_;
  fxl::Closure on_done_;
  bool stopped_ = true;
  bool joined_ = false;

  async_t* const main_dispatcher_;
  async::Loop io_loop_;

  // The binding and the implementation can only be accessed on the io thread.
  std::unique_ptr<fidl::Binding<URLLoader>> binding_;
  std::unique_ptr<URLLoaderImpl> url_loader_;

  // Copyable on any thread, but can only be de-referenced on the main thread.
  fxl::WeakPtr<UrlLoaderContainer> weak_ptr_;

  // The weak ptr factory is only accessed on the main thread.
  fxl::WeakPtrFactory<UrlLoaderContainer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UrlLoaderContainer);
};

NetworkServiceImpl::NetworkServiceImpl(async_t* dispatcher)
  : dispatcher_(dispatcher), available_slots_(kMaxSlots) {
  FXL_DCHECK(dispatcher_);
}

NetworkServiceImpl::~NetworkServiceImpl() = default;

void NetworkServiceImpl::AddBinding(
    fidl::InterfaceRequest<NetworkService> request) {
  bindings_.AddBinding(this, std::move(request));
}

void NetworkServiceImpl::CreateURLLoader(
    fidl::InterfaceRequest<URLLoader> request) {
  loaders_.emplace_back(this, dispatcher_, std::move(request));
  UrlLoaderContainer* container = &loaders_.back();
  container->set_on_done([this, container] {
    loaders_.erase(std::find_if(loaders_.begin(), loaders_.end(),
                                [container](const UrlLoaderContainer& value) {
                                  return container == &value;
                                }));
  });
  container->Start();
}

void NetworkServiceImpl::GetCookieStore(zx::channel cookie_store) {
  FXL_NOTIMPLEMENTED();
}

void NetworkServiceImpl::CreateWebSocket(zx::channel socket) {
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
