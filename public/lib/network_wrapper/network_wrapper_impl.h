// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_
#define LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_

#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include "lib/backoff/backoff.h"
#include "lib/callback/auto_cleanable.h"
#include "lib/callback/scoped_task_runner.h"
#include "lib/network_wrapper/network_wrapper.h"

namespace network_wrapper {

class NetworkWrapperImpl : public NetworkWrapper {
 public:
  NetworkWrapperImpl(async_dispatcher_t* dispatcher,
                     std::unique_ptr<backoff::Backoff> backoff,
                     fit::function<::fuchsia::net::oldhttp::HttpServicePtr()>
                         http_service_factory);
  ~NetworkWrapperImpl() override;

  fxl::RefPtr<callback::Cancellable> Request(
      fit::function<::fuchsia::net::oldhttp::URLRequest()> request_factory,
      fit::function<void(::fuchsia::net::oldhttp::URLResponse)> callback)
      override;

 private:
  class RunningRequest;

  ::fuchsia::net::oldhttp::HttpService* GetHttpService();

  void RetryGetHttpService();

  std::unique_ptr<backoff::Backoff> backoff_;
  bool in_backoff_ = false;
  fit::function<::fuchsia::net::oldhttp::HttpServicePtr()>
      http_service_factory_;
  ::fuchsia::net::oldhttp::HttpServicePtr http_service_;
  callback::AutoCleanableSet<RunningRequest> running_requests_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace network_wrapper

#endif  // LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_
