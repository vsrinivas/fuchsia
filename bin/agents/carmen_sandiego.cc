// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/src/acquirers/gps.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"

constexpr char maxwell::acquirers::GpsAcquirer::kLabel[];

namespace maxwell {
namespace {

class CarmenSandiegoApp : public ContextListener {
 public:
  CarmenSandiegoApp()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        publisher_(
            app_context_->ConnectToEnvironmentService<ContextPublisher>()),
        reader_(app_context_->ConnectToEnvironmentService<ContextReader>()),
        binding_(this) {
    auto query = ContextQuery::New();
    query->topics.push_back(acquirers::GpsAcquirer::kLabel);
    reader_->SubscribeToTopics(std::move(query), binding_.NewBinding());
  }

 private:
  // |ContextListener|
  void OnUpdate(ContextUpdatePtr update) override {
    std::string hlloc = "somewhere";

    rapidjson::Document d;
    d.Parse(update->values[acquirers::GpsAcquirer::kLabel].data());

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

    publisher_->Publish("/location/region", json.str());
  }

  std::unique_ptr<app::ApplicationContext> app_context_;

  ContextPublisherPtr publisher_;
  ContextReaderPtr reader_;
  fidl::Binding<ContextListener> binding_;
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::CarmenSandiegoApp app;
  loop.Run();
  return 0;
}
