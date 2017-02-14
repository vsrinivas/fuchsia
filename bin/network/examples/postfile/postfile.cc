// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <fcntl.h>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/network/services/network_service.fidl.h"
#include "apps/network/services/url_loader.fidl.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/mtl/socket/files.h"
#include "lib/mtl/tasks/message_loop.h"

namespace examples {

class ResponsePrinter {
 public:
  void Run(network::URLResponsePtr response) const {
    if (response->error) {
      printf("Got error: %d (%s)\n", response->error->code,
             response->error->description.get().c_str());
    } else {
      PrintResponse(response);
      PrintResponseBody(std::move(response->body->get_stream()));
    }

    mtl::MessageLoop::GetCurrent()->QuitNow();  // All done!
  }

  void PrintResponse(const network::URLResponsePtr& response) const {
    printf(">>> Headers <<< \n");
    printf("  %s\n", response->status_line.get().c_str());
    if (response->headers) {
      for (size_t i = 0; i < response->headers.size(); ++i)
        printf("  %s=%s\n",
               response->headers[i]->name.To<std::string>().c_str(),
               response->headers[i]->value.To<std::string>().c_str());
    }
  }

  void PrintResponseBody(mx::socket body) const {
    // Read response body in blocking fashion.
    printf(">>> Body <<<\n");

    for (;;) {
      char buf[512];
      size_t num_bytes = sizeof(buf);
      mx_status_t result = body.read(0u, buf, num_bytes, &num_bytes);

      if (result == ERR_SHOULD_WAIT) {
        body.wait_one(MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED,
                      MX_TIME_INFINITE, nullptr);
      } else if (result == NO_ERROR) {
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
  PostFileApp(const std::vector<std::string>& args)
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    network_service_ =
        context_->ConnectToEnvironmentService<network::NetworkService>();

    Start(args);
  }

 private:
  void Start(const std::vector<std::string>& args) {
    if (args.size() < 3) {
      printf("usage: %s url upload_file\n", args[0].c_str());
      return;
    }
    std::string url(args[1]);
    std::string upload_file(args[2]);
    printf("Posting %s to %s\n", upload_file.c_str(), url.c_str());

    std::string boundary = "XXXX";  // TODO: make an option to change this

    ftl::UniqueFD fd(open(upload_file.c_str(), O_RDONLY));
    if (!fd.is_valid()) {
      printf("cannot open %s\n", upload_file.c_str());
      return;
    }

    network::URLRequestPtr request(network::URLRequest::New());
    request->url = url;
    request->method = "POST";
    request->auto_follow_redirects = true;

    auto header = network::HttpHeader::New();
    header->name = "Content-Type";
    header->value = "multipart/form-data; boundary=" + boundary;
    request->headers.push_back(std::move(header));

    mx::socket consumer;
    mx::socket producer;
    mx_status_t status = mx::socket::create(0u, &producer, &consumer);
    if (status != NO_ERROR) {
      printf("cannot create socket\n");
      return;
    }

    request->body = network::URLBody::New();
    request->body->set_stream(std::move(consumer));

    auto task_runner = mtl::MessageLoop::GetCurrent()->task_runner();
    mtl::CopyFromFileDescriptor(std::move(fd), std::move(producer), task_runner,
                                [](bool result, ftl::UniqueFD fd) {
                                  if (!result) {
                                    printf("file read error\n");
                                    mtl::MessageLoop::GetCurrent()->QuitNow();
                                  }
                                });

    network_service_->CreateURLLoader(GetProxy(&url_loader_));

    url_loader_->Start(std::move(request),
                       [this](network::URLResponsePtr response) {
                         ResponsePrinter printer;
                         printer.Run(std::move(response));
                       });
  }

  std::unique_ptr<app::ApplicationContext> context_;
  network::NetworkServicePtr network_service_;
  network::URLLoaderPtr url_loader_;
};

}  // namespace examples

int main(int argc, const char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  mtl::MessageLoop loop;

  examples::PostFileApp postfile_app(args);

  loop.Run();
  return 0;
}
