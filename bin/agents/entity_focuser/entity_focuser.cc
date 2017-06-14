// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_provider.fidl.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/src/agents/entity_utils/entity_span.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

const std::string kRawEntitiesTopic = "/inferred/focal_entities";
const std::string kRawTextSelectionTopic =
    "/story/focused/explicit/raw/text_selection";
const std::string kFocusedEntityTopic = "/inferred/focused_entities";

// Subscribe to entities and selection in the Context Engine, and Publish any
// focused entities back to the Context Engine.
class FocusedEntityFinder : ContextListener {
 public:
  FocusedEntityFinder()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        provider_(app_context_->ConnectToEnvironmentService<ContextProvider>()),
        publisher_(
            app_context_->ConnectToEnvironmentService<ContextPublisher>()),
        topics_({kRawEntitiesTopic, kRawTextSelectionTopic}),
        binding_(this) {
    auto query = ContextQuery::New();
    for (const std::string& s : topics_) {
      query->topics.push_back(s);
    }
    provider_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // Parse a JSON representation of an array of entities.
  std::vector<EntitySpan> GetEntitiesFromJson(const std::string& json_string) {
    // Validate and parse the string.
    if (json_string.empty()) {
      FTL_LOG(INFO) << "No current entities.";
      return std::vector<EntitySpan>();
    }
    rapidjson::Document entities_doc;
    entities_doc.Parse(json_string);
    if (entities_doc.HasParseError()) {
      FTL_LOG(ERROR) << "Invalid Entities JSON, error #: "
                     << entities_doc.GetParseError();
      return std::vector<EntitySpan>();
    }
    if (!entities_doc.IsArray()) {
      FTL_LOG(ERROR) << "Invalid " << kRawEntitiesTopic << " entry in Context.";
      return std::vector<EntitySpan>();
    }

    std::vector<EntitySpan> entities;
    for (const rapidjson::Value& e : entities_doc.GetArray()) {
      entities.push_back(EntitySpan::FromJson(modular::JsonValueToString(e)));
    }
    return entities;
  }

  // Parse a JSON representation of selection.
  std::pair<int, int> GetSelectionFromJson(const std::string& json_string) {
    // Validate and parse the string.
    if (json_string.empty()) {
      FTL_LOG(INFO) << "No current selection.";
      return std::make_pair(-1, -1);
    }
    rapidjson::Document selection_doc;
    selection_doc.Parse(json_string);
    if (selection_doc.HasParseError() || selection_doc.Empty() ||
        !selection_doc.IsArray()) {
      FTL_LOG(ERROR) << "Invalid " << kRawTextSelectionTopic
                     << " entry in Context.";
      return std::make_pair(-1, -1);
    }
    const rapidjson::Value& selection = selection_doc[0];
    if (!(selection.HasMember("start") && selection["start"].IsInt() &&
          selection.HasMember("end") && selection["end"].IsInt())) {
      FTL_LOG(ERROR) << "Invalid " << kRawTextSelectionTopic
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
  std::string GetFocusedEntities(const std::vector<EntitySpan>& entities,
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
  void OnUpdate(ContextUpdatePtr result) override {
    const std::vector<EntitySpan> entities =
        GetEntitiesFromJson(result->values[kRawEntitiesTopic]);
    const std::pair<int, int> start_and_end =
        GetSelectionFromJson(result->values[kRawTextSelectionTopic]);
    publisher_->Publish(kFocusedEntityTopic,
                        GetFocusedEntities(entities, start_and_end.first,
                                           start_and_end.second));
  }

  std::unique_ptr<app::ApplicationContext> app_context_;
  ContextProviderPtr provider_;
  ContextPublisherPtr publisher_;
  const std::vector<std::string> topics_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::FocusedEntityFinder app;
  loop.Run();
  return 0;
}
