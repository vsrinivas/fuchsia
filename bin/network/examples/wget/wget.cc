// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/network/services/network_service.fidl.h"
#include "apps/network/services/url_loader.fidl.h"
#include "lib/ftl/macros.h"
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

  void PrintResponseBody(mx::datapipe_consumer body) const {
    // Read response body in blocking fashion.
    printf(">>> Body <<<\n");

    for (;;) {
      char buf[512];
      mx_size_t num_bytes = sizeof(buf);
      mx_status_t result = body.read(0u, buf, num_bytes, &num_bytes);

      if (result == ERR_SHOULD_WAIT) {
        body.wait_one(MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
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

class WGetApp {
 public:
  WGetApp(const std::vector<std::string>& args)
    : context_(modular::ApplicationContext::CreateFromStartupInfo()) {
#if USE_ENVIRONMENT_SERVICE
    network_service_ =
      context_->ConnectToEnvironmentService<network::NetworkService>();
#else
    auto launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = "file:///system/apps/network";
    launch_info->services = fidl::GetProxy(&network_service_provider_);
    context_->launcher()->CreateApplication(std::move(launch_info),
                                            fidl::GetProxy(&app_controller_));

    modular::ConnectToService(network_service_provider_.get(),
                              fidl::GetProxy(&network_service_));
#endif
    FTL_DCHECK(network_service_);

    Start(args);
  }

 private:
  void Start(const std::vector<std::string>& args) {
    if (args.size() == 1) {
      printf("needs an url argument\n");
      return;
    }
    std::string url(args[1]);
    printf("Loading: %s\n", url.c_str());

    network_service_->CreateURLLoader(GetProxy(&url_loader_));

    network::URLRequestPtr request(network::URLRequest::New());
    request->url = url;
    request->method = "GET";
    request->auto_follow_redirects = true;

    url_loader_->Start(std::move(request),
                       [this](network::URLResponsePtr response) {
                         ResponsePrinter printer;
                         printer.Run(std::move(response));
                       });
  }

  std::unique_ptr<modular::ApplicationContext> context_;
#if !USE_ENVIRONMENT_SERVICE
  modular::ApplicationControllerPtr app_controller_;
  modular::ServiceProviderPtr network_service_provider_;
#endif

  network::NetworkServicePtr network_service_;
  network::URLLoaderPtr url_loader_;
};

}  // namespace examples

int main(int argc, const char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  mtl::MessageLoop loop;

  examples::WGetApp app(args);

  loop.Run();
  return 0;
}
