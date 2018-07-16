// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <lib/async-loop/cpp/loop.h>
#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace examples {

namespace http = ::fuchsia::net::oldhttp;

// ResponseConsumer consumes the response silently.
class ResponseConsumer {
 public:
  ResponseConsumer(int id) : id_(id) {}
  ResponseConsumer() = delete;

  void Run(http::URLResponse response) const {
    if (response.error) {
      printf("#%d: Got error: %d (%s)\n", id_, response.error->code,
             response.error->description.get().c_str());
    } else {
      ReadResponseBody(std::move(response.body->stream()));
    }
  }

  void ReadResponseBody(zx::socket body) const {
    for (;;) {
      char buf[512];
      size_t num_bytes = sizeof(buf);
      zx_status_t result = body.read(0u, buf, num_bytes, &num_bytes);

      if (result == ZX_ERR_SHOULD_WAIT) {
        body.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                      zx::time::infinite(), nullptr);
      } else if (result == ZX_ERR_PEER_CLOSED) {
        // not an error
        break;
      } else if (result == ZX_OK) {
        // ignore the data and go to another read.
      } else {
        printf("#%d: Unexpected error reading response %d\n", id_, result);
        break;
      }
    }
  }

 private:
  int id_;
};

class MWGetApp {
 public:
  static constexpr int MAX_LOADERS = 100;

  MWGetApp(async::Loop* loop)
      : context_(component::StartupContext::CreateFromStartupInfo()),
        loop_(loop) {
    http_service_ =
        context_->ConnectToEnvironmentService<http::HttpService>();
    FXL_DCHECK(loop);
    FXL_DCHECK(http_service_);
  }

  bool Start(const std::vector<std::string>& args) {
    if (args.size() != 3) {
      printf("usage: %s url num_loaders\n", args[0].c_str());
      return false;
    }
    std::string url(args[1]);
    num_loaders_ = atoi(args[2].c_str());
    if (num_loaders_ <= 0) {
      printf("num_loaders must be positive\n");
      return false;
    } else if (num_loaders_ > MAX_LOADERS) {
      printf("can't exceed the max number of loaders (%d)\n", MAX_LOADERS);
      return false;
    }
    printf("Loading: %s x %d\n", url.c_str(), num_loaders_);

    num_done_ = 0;
    for (int i = 0; i < num_loaders_; i++) {
      http_service_->CreateURLLoader(url_loader_[i].NewRequest());

      http::URLRequest request;
      request.url = url;
      request.method = "GET";
      request.auto_follow_redirects = true;

      url_loader_[i]->Start(std::move(request),
                            [this, i](http::URLResponse response) {
                              ResponseConsumer consumer(i);
                              consumer.Run(std::move(response));
                              ++num_done_;
                              printf("[%d] #%d done\n", num_done_, i);
                              if (num_done_ == num_loaders_) {
                                printf("All done!\n");
                                loop_->Quit();
                              }
                            });
    }
    return true;
  }

 private:
  std::unique_ptr<component::StartupContext> context_;

  async::Loop* const loop_;
  http::HttpServicePtr http_service_;
  http::URLLoaderPtr url_loader_[MAX_LOADERS];
  int num_loaders_;
  int num_done_;
};

}  // namespace examples

int main(int argc, const char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  examples::MWGetApp app(&loop);
  if (app.Start(args))
    loop.Run();

  return 0;
}
