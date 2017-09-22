// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "apps/maxwell/services/context/context_writer.fidl.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/src/agents/entity_utils/entity_span.h"
#include "apps/maxwell/src/agents/entity_utils/entity_utils.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "lib/fsl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

// Subscribe to entities and selection in the Context Engine, and Publish any
// selected entities back to the Context Engine.
class SelectedEntityFinder : ContextListener {
 public:
  SelectedEntityFinder()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        reader_(app_context_->ConnectToEnvironmentService<ContextReader>()),
        writer_(
            app_context_->ConnectToEnvironmentService<ContextWriter>()),
        binding_(this) {
    auto query = ContextQuery::New();
    for (const std::string& topic : {kFocalEntitiesTopic, kRawTextSelectionTopic}) {
      auto selector = ContextSelector::New();
      selector->type = ContextValueType::ENTITY;
      selector->meta = ContextMetadata::New();
      selector->meta->entity = EntityMetadata::New();
      selector->meta->entity->topic = topic;
      query->selector[topic] = std::move(selector);
    }
    reader_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // Parse a JSON representation of selection.
  std::pair<int, int> GetSelectionFromJson(const std::string& json_string) {
    // Validate and parse the string.
    if (json_string.empty()) {
      FXL_LOG(INFO) << "No current selection.";
      return std::make_pair(-1, -1);
    }
    rapidjson::Document selection_doc;
    selection_doc.Parse(json_string);
    if (selection_doc.HasParseError() || selection_doc.Empty() ||
        !selection_doc.IsArray()) {
      FXL_LOG(ERROR) << "Invalid " << kRawTextSelectionTopic
                     << " entry in Context.";
      return std::make_pair(-1, -1);
    }
    const rapidjson::Value& selection = selection_doc[0];
    if (!(selection.HasMember("start") && selection["start"].IsInt() &&
          selection.HasMember("end") && selection["end"].IsInt())) {
      FXL_LOG(ERROR) << "Invalid " << kRawTextSelectionTopic
                     << " entry in Context. "
                     << "Missing \"start\" or \"end\" keys.";
      return std::make_pair(-1, -1);
    }

    const int start = selection["start"].GetInt();
    const int end = selection["end"].GetInt();
    return std::make_pair(start, end);
  }

  // Return a JSON representation of an array of entities that fall within
  // start and end.
  std::string GetSelectedEntities(const std::vector<EntitySpan>& entities,
                                  const int selection_start,
                                  const int selection_end) {
    rapidjson::Document d;
    rapidjson::Value entities_json(rapidjson::kArrayType);
    for (const EntitySpan& e : entities) {
      if (e.GetStart() <= selection_start && e.GetEnd() >= selection_end) {
        d.Parse(e.GetJsonString());
        entities_json.PushBack(d, d.GetAllocator());
      }
    }
    return modular::JsonValueToString(entities_json);
  }

  // |ContextListener|
  void OnContextUpdate(ContextUpdatePtr result) override {
    if (result->values[kFocalEntitiesTopic].empty() ||
        result->values[kRawTextSelectionTopic].empty()) {
      return;
    }
    const std::vector<EntitySpan> entities =
        EntitySpan::FromContextValues(result->values[kFocalEntitiesTopic]);
    const std::pair<int, int> start_and_end =
        GetSelectionFromJson(result->values[kRawTextSelectionTopic][0]->content);
    writer_->WriteEntityTopic(kSelectedEntitiesTopic,
                        GetSelectedEntities(entities, start_and_end.first,
                                            start_and_end.second));
  }

  std::unique_ptr<app::ApplicationContext> app_context_;
  ContextReaderPtr reader_;
  ContextWriterPtr writer_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  maxwell::SelectedEntityFinder app;
  loop.Run();
  return 0;
}
