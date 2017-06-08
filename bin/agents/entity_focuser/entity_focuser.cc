// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_provider.fidl.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace maxwell {

// Subscribe to entities and selection in ApplicationContext, and Publish any
// focused entities back to ApplicationContext.
class FocusedEntityFinder : ContextListener {
 public:
  FocusedEntityFinder()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        provider_(app_context_->ConnectToEnvironmentService<ContextProvider>()),
        binding_(this) {
    FTL_LOG(INFO) << "Initializing";
    auto query = ContextQuery::New();
    query->topics.push_back("raw/entity");
    query->topics.push_back("raw/text_selection");
    provider_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnUpdate(ContextUpdatePtr result) override {
    // TODO(travismart): Update entity storage from a raw text entry.
    rapidjson::Document entity_doc;
    entity_doc.Parse(result->values["raw/entity"]);
    if (!entity_doc.HasMember("text") || !entity_doc["text"].IsString()) {
      FTL_LOG(ERROR) << "Invalid raw/entity entry in ApplicationContext.";
    }
    const std::string raw_text = entity_doc["text"].GetString();
    FTL_LOG(INFO) << "raw/entity:" << raw_text;

    rapidjson::Document selection_doc;
    entity_doc.Parse(result->values["raw/text_selection"]);
    if (!selection_doc.HasMember("start") || !selection_doc.HasMember("end")) {
      FTL_LOG(ERROR) << "Invalid raw/selection entry in ApplicationContext.";
    }
    const int start = selection_doc["start"].GetInt();
    const int end = selection_doc["end"].GetInt();
    FTL_LOG(INFO) << "raw/selection:" << start << ", " << end;

    // TODO(travismart): Properly process entity and selection and publish a
    // "focused_entity" back to ApplicationContext.
  }

  std::unique_ptr<app::ApplicationContext> app_context_;
  ContextProviderPtr provider_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::FocusedEntityFinder app;
  loop.Run();
  return 0;
}
