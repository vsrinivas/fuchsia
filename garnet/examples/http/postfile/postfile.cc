// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>

#include <cstdio>

#include <fbl/unique_fd.h>

#include "src/lib/files/file.h"
#include "src/lib/files/file_descriptor.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/socket/files.h"

namespace examples {

namespace http = ::fuchsia::net::http;

class ResponsePrinter {
 public:
  void Run(async::Loop* loop, http::Response response) const {
    if (response.has_error()) {
      printf("Got error: %d\n", response.error());
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
      } else if (result == ZX_OK) {
        if (fwrite(buf, num_bytes, 1, stdout) != 1) {
          printf("\nUnexpected error writing to file\n");
          break;
        }
      } else {
        break;
      }
    }

    printf("\n>>> EOF <<<\n");
  }
};

class PostFileApp {
 public:
  PostFileApp(async::Loop* loop)
      : loop_(loop), context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
    loader_ = context_->svc()->Connect<http::Loader>();
  }

  bool Start(const std::vector<std::string>& args) {
    if (args.size() < 3) {
      printf("usage: %s url upload_file\n", args[0].c_str());
      return false;
    }
    std::string url(args[1]);
    std::string upload_file(args[2]);
    printf("Posting %s to %s\n", upload_file.c_str(), url.c_str());

    std::string boundary = "XXXX";  // TODO: make an option to change this

    fbl::unique_fd fd(open(upload_file.c_str(), O_RDONLY));
    if (!fd.is_valid()) {
      printf("cannot open %s\n", upload_file.c_str());
      return false;
    }

    http::Request request;
    request.set_url(url);
    request.set_method("POST");

    const std::string name = "Content-Type";
    const std::string value = "multipart/form-data; boundary=" + boundary;

    request.set_headers({{
        .name = std::vector<uint8_t>(name.begin(), name.end()),
        .value = std::vector<uint8_t>(value.begin(), value.end()),
    }});

    zx::socket consumer;
    zx::socket producer;
    zx_status_t status = zx::socket::create(0u, &producer, &consumer);
    if (status != ZX_OK) {
      printf("cannot create socket\n");
      return false;
    }

    request.set_body(http::Body::WithStream(std::move(consumer)));

    async_dispatcher_t* dispatcher = async_get_default_dispatcher();
    fsl::CopyFromFileDescriptor(std::move(fd), std::move(producer), dispatcher,
                                [this](bool result, fbl::unique_fd fd) {
                                  if (!result) {
                                    printf("file read error\n");
                                    loop_->Quit();
                                  }
                                });

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

  examples::PostFileApp postfile_app(&loop);
  if (postfile_app.Start(args))
    loop.Run();

  return 0;
}
