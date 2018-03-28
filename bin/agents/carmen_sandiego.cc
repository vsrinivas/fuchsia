// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/modular.h>

#include "lib/app/cpp/application_context.h"
#include "lib/context/cpp/context_helper.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/bin/acquirers/gps.h"
#include "third_party/rapidjson/rapidjson/document.h"

constexpr char maxwell::acquirers::GpsAcquirer::kLabel[];

namespace maxwell {
namespace {

class CarmenSandiegoApp : public modular::ContextListener {
 public:
  CarmenSandiegoApp()
      : app_context_(component::ApplicationContext::CreateFromStartupInfo()),
        writer_(app_context_->ConnectToEnvironmentService<modular::ContextWriter>()),
        reader_(app_context_->ConnectToEnvironmentService<modular::ContextReader>()),
        binding_(this) {
    modular::ContextSelector selector;
    selector.type = modular::ContextValueType::ENTITY;
    selector.meta = modular::ContextMetadata::New();
    selector.meta->entity = modular::EntityMetadata::New();
    selector.meta->entity->topic = acquirers::GpsAcquirer::kLabel;
    modular::ContextQuery query;
    AddToContextQuery(&query, "gps", std::move(selector));
    reader_->Subscribe(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnContextUpdate(modular::ContextUpdate update) override {
    auto p = TakeContextValue(&update, "gps");
    if (!p.first)
      return;

    std::string hlloc = "somewhere";

    rapidjson::Document d;
    d.Parse(p.second->at(0).content);

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

  std::unique_ptr<component::ApplicationContext> app_context_;

  modular::ContextWriterPtr writer_;
  modular::ContextReaderPtr reader_;
  fidl::Binding<modular::ContextListener> binding_;
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  maxwell::CarmenSandiegoApp app;
  loop.Run();
  return 0;
}
