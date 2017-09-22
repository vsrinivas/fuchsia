// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/component/fidl/component.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

class App {
 public:
  App() : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    component_index_ = context_->ConnectToEnvironmentService<ComponentIndex>();
  }

  void Query(int argc, const char** argv) {
    if (argc == 0) {
      std::cerr << "query requires a filter expression\n";
      exit(1);
    }

    fidl::Map<fidl::String, fidl::String> query;
    for (; argc > 0; --argc, ++argv) {
      std::string facet_type(argv[0]);
      if (argc > 1) {
        // Check JSON validity.
        rapidjson::Document doc;
        if (doc.Parse(argv[1]).HasParseError()) {
          FXL_LOG(FATAL) << "Failed to parse JSON facet data: " << argv[1];
        }

        query[fidl::String(facet_type)] = argv[1];
        --argc;
        ++argv;
      } else {
        query[fidl::String(facet_type)] = "";
      }
    }

    component_index_->FindComponentManifests(
        std::move(query), [this](fidl::Array<ComponentManifestPtr> results) {
          FXL_LOG(INFO) << "Got " << results.size() << " results...";
          for (auto& result : results) {
            std::cout << "=== " << result->component->url << "\n";
            std::cout << result->raw << "\n";
          }

          exit(0);
        });
  }

 private:
  std::function<void()> quit_callback_;
  std::unique_ptr<app::ApplicationContext> context_;
  app::ApplicationControllerPtr component_index_controller_;

  fidl::InterfacePtr<ComponentIndex> component_index_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace component

void Usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " <command>\n\n"
      << "commands:\n"
      << "  query [facet1 [info1], ...]]\n"
      << "    Queries for existence of all 'facetN' and optionally matches\n"
      << "    'infoN' against that facet's info. 'infoN' will match if it\n"
      << "    is a subset of 'facetN's info. 'infoN' should be provided\n"
      << "    as JSON.\n";

  exit(1);
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    Usage(argv[0]);
  }

  fsl::MessageLoop loop;
  component::App app;

  if (strcmp(argv[1], "query") == 0) {
    app.Query(argc - 2, argv + 2);
  } else {
    Usage(argv[0]);
  }

  loop.Run();
  return 0;
}
