// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "apps/maxwell/services/context/context_writer.fidl.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/src/acquirers/gps.h"
#include "lib/fsl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"

constexpr char maxwell::acquirers::GpsAcquirer::kLabel[];

namespace maxwell {
namespace {

class CarmenSandiegoApp : public ContextListener {
 public:
  CarmenSandiegoApp()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        writer_(
            app_context_->ConnectToEnvironmentService<ContextWriter>()),
        reader_(app_context_->ConnectToEnvironmentService<ContextReader>()),
        binding_(this) {
    auto selector = ContextSelector::New();
    selector->type = ContextValueType::ENTITY;
    selector->meta = ContextMetadata::New();
    selector->meta->entity = EntityMetadata::New();
    selector->meta->entity->topic = acquirers::GpsAcquirer::kLabel;
    auto query = ContextQuery::New();
    query->selector["gps"] = std::move(selector);
    reader_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnContextUpdate(ContextUpdatePtr update) override {
    if (update->values["gps"].empty()) return;

    std::string hlloc = "somewhere";

    rapidjson::Document d;
    d.Parse(update->values["gps"][0]->content);

    if (d.IsObject()) {
      const float latitude = d["lat"].GetFloat(),
                  longitude = d["lng"].GetFloat();

      if (latitude > 66) {
        hlloc = "The Arctic";
      } else if (latitude < -66) {
        hlloc = "Antarctica";
      } else if (latitude < 49 && latitude > 25 && longitude > -125 &&
                 longitude < -67) {
        hlloc = "America";
      }
    }

    std::ostringstream json;
    json << "\"" << hlloc << "\"";

    writer_->WriteEntityTopic("/location/region", json.str());
  }

  std::unique_ptr<app::ApplicationContext> app_context_;

  ContextWriterPtr writer_;
  ContextReaderPtr reader_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  maxwell::CarmenSandiegoApp app;
  loop.Run();
  return 0;
}
