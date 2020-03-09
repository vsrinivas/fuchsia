// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTING_FAKE_HTTP_LOADER_H_
#define SRC_COBALT_BIN_TESTING_FAKE_HTTP_LOADER_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>

#include "src/lib/callback/scoped_task_runner.h"

namespace cobalt {

class FakeHTTPLoader : public fuchsia::net::http::Loader {
 public:
  FakeHTTPLoader(async_dispatcher_t *dispatcher)
      : dispatcher_(dispatcher), task_runner_(dispatcher_) {}

  fidl::InterfaceRequestHandler<fuchsia::net::http::Loader> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void SetResponse(fuchsia::net::http::Response response) {
    has_response_ = true;
    next_response_ = std::move(response);
  }

  void SetResponseDelay(zx::duration response_delay) { response_delay_ = response_delay; }

  void Fetch(fuchsia::net::http::Request request,
             fuchsia::net::http::Loader::FetchCallback callback) override {
    if (has_response_) {
      has_response_ = false;
      if (response_delay_ > zx::sec(0)) {
        task_runner_.PostDelayedTask(
            [response = std::move(next_response_), callback = std::move(callback)]() mutable {
              callback(std::move(response));
            },
            response_delay_);
      } else {
        callback(std::move(next_response_));
      }
    }
  }

  void Start(fuchsia::net::http::Request request,
             fidl::InterfaceHandle<class fuchsia::net::http::LoaderClient> client) override {}

  void Unbind() { bindings_.CloseAll(); }

 private:
  bool has_response_ = false;
  zx::duration response_delay_ = zx::sec(0);
  async_dispatcher_t *dispatcher_;
  callback::ScopedTaskRunner task_runner_;
  fuchsia::net::http::Response next_response_;
  fidl::BindingSet<fuchsia::net::http::Loader> bindings_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_TESTING_FAKE_HTTP_LOADER_H_
