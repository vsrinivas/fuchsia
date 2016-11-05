// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rapidjson/document.h>

#include "apps/maxwell/services/context_engine.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/maxwell/acquirers/gps.h"

using maxwell::acquirers::GpsAcquirer;

constexpr char GpsAcquirer::kLabel[];
constexpr char GpsAcquirer::kSchema[];

namespace {

using namespace maxwell::context_engine;

class CarmenSandiego : public ContextPublisherController,
                       public ContextSubscriberLink {
 public:
  CarmenSandiego()
      : app_ctx_(modular::ApplicationContext::CreateFromStartupInfo()),
        cx_(app_ctx_->ConnectToEnvironmentService<ContextAgentClient>()),
        ctl_(this),
        in_(this) {
    fidl::InterfaceHandle<ContextPublisherController> ctl_handle;
    ctl_.Bind(GetProxy(&ctl_handle));
    // TODO(rosswang): V0 does not support semantic differentiation by source,
    // so the labels have to be explicitly different. In the future, these could
    // all be refinements on "location"
    cx_->Publish("/location/region", "json:string", std::move(ctl_handle),
                 GetProxy(&out_));
  }

  void OnHasSubscribers() override {
    ContextSubscriberLinkPtr in_ptr;
    in_.Bind(GetProxy(&in_ptr));
    cx_->Subscribe(GpsAcquirer::kLabel, GpsAcquirer::kSchema,
                   in_ptr.PassInterfaceHandle());
  }

  void OnNoSubscribers() override {
    in_.Unbind();
    out_->Update(NULL);
  }

  void OnUpdate(ContextUpdatePtr update) override {
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
  std::unique_ptr<modular::ApplicationContext> app_ctx_;

  ContextAgentClientPtr cx_;
  fidl::Binding<ContextPublisherController> ctl_;
  fidl::Binding<ContextSubscriberLink> in_;
  ContextPublisherLinkPtr out_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  CarmenSandiego app;
  loop.Run();
  return 0;
}
