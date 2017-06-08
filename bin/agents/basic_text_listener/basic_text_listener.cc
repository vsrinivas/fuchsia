// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_provider.fidl.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace maxwell {

// Subscribe to ApplicationContext and Publish any entities found back to
// ApplicationContext.
class BasicTextListener : ContextListener {
 public:
  BasicTextListener()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        provider_(app_context_->ConnectToEnvironmentService<ContextProvider>()),
        binding_(this) {
    FTL_LOG(INFO) << "Initializing";
    auto query = ContextQuery::New();
    query->topics.push_back("raw/text");
    provider_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnUpdate(ContextUpdatePtr result) override {
    rapidjson::Document text_doc;
    text_doc.Parse(result->values["raw/text"]);
    if (!text_doc.HasMember("text") || !text_doc["text"].IsString()) {
      FTL_LOG(ERROR) << "Invalid raw/text entry in ApplicationContext.";
    }
    const std::string raw_text = text_doc["text"].GetString();
    FTL_LOG(INFO) << "raw/text:" << raw_text;

    // TODO(travismart): Find entities and publish them back to Context, under
    // "raw/entity".
  }

  std::unique_ptr<app::ApplicationContext> app_context_;
  ContextProviderPtr provider_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::BasicTextListener app;
  loop.Run();
  return 0;
}
