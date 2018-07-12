// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <cstdio>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include "lib/app/cpp/connect.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fsl/socket/files.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/unique_fd.h"

namespace examples {

namespace http = ::fuchsia::net::oldhttp;

class ResponsePrinter {
 public:
  void Run(async::Loop* loop, http::URLResponse response) const {
    if (response.error) {
      printf("Got error: %d (%s)\n", response.error->code,
             response.error->description->c_str());
    } else {
      PrintResponse(response);
      PrintResponseBody(std::move(response.body->stream()));
    }

    loop->Quit();  // All done!
  }

  void PrintResponse(const http::URLResponse& response) const {
    printf(">>> Headers <<< \n");
    printf("  %s\n", response.status_line.get().c_str());
    if (response.headers) {
      for (size_t i = 0; i < response.headers->size(); ++i)
        printf("  %s=%s\n", response.headers->at(i).name->data(),
               response.headers->at(i).value->data());
    }
  }

  void PrintResponseBody(zx::socket body) const {
    // Read response body in blocking fashion.
    printf(">>> Body <<<\n");

    for (;;) {
      char buf[512];
      size_t num_bytes = sizeof(buf);
      zx_status_t result = body.read(0u, buf, num_bytes, &num_bytes);

      if (result == ZX_ERR_SHOULD_WAIT) {
        body.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                      zx::time::infinite(), nullptr);
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
      : loop_(loop),
        context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()) {
    http_service_ =
        context_->ConnectToEnvironmentService<http::HttpService>();
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

    fxl::UniqueFD fd(open(upload_file.c_str(), O_RDONLY));
    if (!fd.is_valid()) {
      printf("cannot open %s\n", upload_file.c_str());
      return false;
    }

    http::URLRequest request;
    request.url = url;
    request.method = "POST";
    request.auto_follow_redirects = true;

    http::HttpHeader header;
    header.name = "Content-Type";
    header.value = "multipart/form-data; boundary=" + boundary;
    request.headers.push_back(std::move(header));

    zx::socket consumer;
    zx::socket producer;
    zx_status_t status = zx::socket::create(0u, &producer, &consumer);
    if (status != ZX_OK) {
      printf("cannot create socket\n");
      return false;
    }

    request.body = http::URLBody::New();
    request.body->set_stream(std::move(consumer));

    async_dispatcher_t* dispatcher = async_get_default_dispatcher();
    fsl::CopyFromFileDescriptor(std::move(fd), std::move(producer), dispatcher,
                                [this](bool result, fxl::UniqueFD fd) {
                                  if (!result) {
                                    printf("file read error\n");
                                    loop_->Quit();
                                  }
                                });

    http_service_->CreateURLLoader(url_loader_.NewRequest());

    url_loader_->Start(std::move(request),
                       [this](http::URLResponse response) {
                         ResponsePrinter printer;
                         printer.Run(loop_, std::move(response));
                       });
    return true;
  }

 private:
  async::Loop* const loop_;
  std::unique_ptr<fuchsia::sys::StartupContext> context_;
  http::HttpServicePtr http_service_;
  http::URLLoaderPtr url_loader_;
};

}  // namespace examples

int main(int argc, const char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  examples::PostFileApp postfile_app(&loop);
  if (postfile_app.Start(args))
    loop.Run();

  return 0;
}
