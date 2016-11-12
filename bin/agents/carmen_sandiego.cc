// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rapidjson/document.h>

#include "apps/maxwell/services/context/client.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/maxwell/src/acquirers/gps.h"

using maxwell::acquirers::GpsAcquirer;

constexpr char GpsAcquirer::kLabel[];
constexpr char GpsAcquirer::kSchema[];

namespace {

class CarmenSandiegoApp : public maxwell::context::PublisherController,
                          public maxwell::context::SubscriberLink {
 public:
  CarmenSandiegoApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()),
        maxwell_context_(app_context_->ConnectToEnvironmentService<
                         maxwell::context::ContextAgentClient>()),
        ctl_(this),
        in_(this) {
    fidl::InterfaceHandle<maxwell::context::PublisherController> ctl_handle;
    ctl_.Bind(&ctl_handle);
    // TODO(rosswang): V0 does not support semantic differentiation by source,
    // so the labels have to be explicitly different. In the future, these could
    // all be refinements on "location"
    maxwell_context_->Publish("/location/region", "json:string",
                              std::move(ctl_handle), GetProxy(&out_));
  }

  void OnHasSubscribers() override {
    fidl::InterfaceHandle<maxwell::context::SubscriberLink> in_handle;
    in_.Bind(&in_handle);
    maxwell_context_->Subscribe(GpsAcquirer::kLabel, GpsAcquirer::kSchema,
                                std::move(in_handle));
  }

  void OnNoSubscribers() override {
    in_.Unbind();
    out_->Update(NULL);
  }

  void OnUpdate(maxwell::context::UpdatePtr update) override {
    FTL_LOG(INFO) << "OnUpdate from " << update->source << ": "
                  << update->json_value;

    std::string hlloc = "somewhere";

    rapidjson::Document d;
    d.Parse(update->json_value.data());

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

    out_->Update(json.str());
  }

 private:
  std::unique_ptr<modular::ApplicationContext> app_context_;

  maxwell::context::ContextAgentClientPtr maxwell_context_;
  fidl::Binding<maxwell::context::PublisherController> ctl_;
  fidl::Binding<maxwell::context::SubscriberLink> in_;
  maxwell::context::PublisherLinkPtr out_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  CarmenSandiegoApp app;
  loop.Run();
  return 0;
}
