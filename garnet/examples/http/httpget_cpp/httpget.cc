// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/lib/fxl/macros.h"

namespace examples {

namespace http = ::fuchsia::net::http;

class ResponsePrinter {
 public:
  void Run(async::Loop* loop, http::Response response) const {
    if (response.has_error()) {
      printf("Got error: %d\n", response.error());
      exit(1);
    } else {
      PrintResponse(response);
      PrintResponseBody(response.body());
    }

    loop->Quit();  // All done!
  }

  void PrintResponse(const http::Response& response) const {
    printf(">>> Headers <<< \n");
    const auto& status_line = response.status_line();
    printf("  %s\n", std::string(status_line.begin(), status_line.end()).c_str());
    if (response.has_headers()) {
      for (const auto& header : response.headers()) {
        printf("  %s=%s\n", std::string(header.name.begin(), header.name.end()).c_str(),
               std::string(header.value.begin(), header.value.end()).c_str());
      }
    }
  }

  void PrintResponseBody(const zx::socket& body) const {
    // Read response body in blocking fashion.
    printf(">>> Body <<<\n");

    for (;;) {
      char buf[512];
      size_t num_bytes = sizeof(buf);
      zx_status_t result = body.read(0u, buf, num_bytes, &num_bytes);

      if (result == ZX_ERR_SHOULD_WAIT) {
        body.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), nullptr);
      } else if (result == ZX_ERR_PEER_CLOSED) {
        // not an error
        break;
      } else if (result == ZX_OK) {
        if (fwrite(buf, num_bytes, 1, stdout) != 1) {
          printf("\nUnexpected error writing to file\n");
          break;
        }
      } else {
        printf("\nUnexpected error reading response %d\n", result);
        break;
      }
    }

    printf("\n>>> EOF <<<\n");
  }
};

class WGetApp {
 public:
  WGetApp(async::Loop* loop)
      : loop_(loop), context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
    loader_ = context_->svc()->Connect<http::Loader>();
    FX_DCHECK(loader_);
  }

  bool Start(const std::vector<std::string>& args) {
    if (args.size() == 1) {
      printf("usage: %s url\n", args[0].c_str());
      return false;
    }
    std::string url(args[1]);
    if (url.find("://") == std::string::npos) {
      url.insert(0, "http://");
    }
    printf("Loading: %s\n", url.c_str());

    http::Request request;
    request.set_url(url);
    request.set_method("GET");

    loader_->Fetch(std::move(request), [this](http::Response response) {
      ResponsePrinter printer;
      printer.Run(loop_, std::move(response));
    });
    return true;
  }

 private:
  async::Loop* const loop_;
  std::unique_ptr<sys::ComponentContext> context_;

  http::LoaderPtr loader_;
};

}  // namespace examples

int main(int argc, const char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  examples::WGetApp app(&loop);
  if (app.Start(args))
    loop.Run();

  return 0;
}
