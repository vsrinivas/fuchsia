// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex>
#include <vector>

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/src/agents/entity_utils/entity_span.h"
#include "apps/maxwell/src/agents/entity_utils/entity_utils.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace maxwell {

const std::string kEmailRegex = "[^\\s]+@[^\\s]+";

// Subscribe to the Context Engine and Publish any entities found back to
// the Context Engine.
class BasicTextListener : ContextListener {
 public:
  BasicTextListener()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        reader_(app_context_->ConnectToEnvironmentService<ContextReader>()),
        publisher_(
            app_context_->ConnectToEnvironmentService<ContextPublisher>()),
        topics_({kRawTextTopic}),
        binding_(this) {
    auto query = ContextQuery::New();
    for (const std::string& s : topics_) {
      query->topics.push_back(s);
    }
    reader_->SubscribeToTopics(std::move(query), binding_.NewBinding());
  }

 private:
  // Return a JSON representation of an array of entities.
  std::string GetEntitiesFromText(const std::string& raw_text) {
    const std::regex entity_regex(kEmailRegex);
    const auto entities_begin =
        std::sregex_iterator(raw_text.begin(), raw_text.end(), entity_regex);
    const auto entities_end = std::sregex_iterator();

    rapidjson::Document d;
    rapidjson::Value entities_json(rapidjson::kArrayType);
    for (std::sregex_iterator i = entities_begin; i != entities_end; ++i) {
      const std::smatch match = *i;
      const std::string content = match.str();
      const int start = match.position();
      const int end = start + match.length();
      const EntitySpan entity(content, kEmailType, start, end);
      // TODO(travismart): It would be more efficient to work directly with
      // JSON values here, so we don't have to make multiple copies of strings
      // and parse them. However, strings allow our interface to be independent
      // of choice of JSON library.
      d.Parse(entity.GetJsonString());
      entities_json.PushBack(d, d.GetAllocator());
    }

    return modular::JsonValueToString(entities_json);
  }

  // |ContextListener|
  void OnUpdate(ContextUpdatePtr result) override {
    if (!KeyInUpdateResult(result, kRawTextTopic)) {
      return;
    }
    rapidjson::Document text_doc;
    text_doc.Parse(result->values[kRawTextTopic]);
    // TODO(travismart): What to do if there are multiple topics, or if
    // topics_[0] has more than one entry?
    if (!text_doc[0].HasMember("text") || !text_doc[0]["text"].IsString()) {
      FTL_LOG(ERROR) << "Invalid " << kRawTextTopic
                     << " entry in Context Engine.";
    }
    const std::string raw_text = text_doc[0]["text"].GetString();

    publisher_->Publish(kFocalEntitiesTopic, GetEntitiesFromText(raw_text));
  }

  std::unique_ptr<app::ApplicationContext> app_context_;
  ContextReaderPtr reader_;
  ContextPublisherPtr publisher_;
  const std::vector<std::string> topics_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::BasicTextListener app;
  loop.Run();
  return 0;
}
