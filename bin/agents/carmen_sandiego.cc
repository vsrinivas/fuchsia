// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/client.fidl.h"
#include "apps/maxwell/src/acquirers/gps.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/rapidjson/rapidjson/document.h"

constexpr char maxwell::acquirers::GpsAcquirer::kLabel[];

namespace {

class CarmenSandiegoApp : public maxwell::ContextPublisherController,
                          public maxwell::ContextSubscriberLink {
 public:
  CarmenSandiegoApp()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        publisher_(
            app_context_
                ->ConnectToEnvironmentService<maxwell::ContextPublisher>()),
        subscriber_(
            app_context_
                ->ConnectToEnvironmentService<maxwell::ContextSubscriber>()),
        ctl_(this),
        in_(this) {
    fidl::InterfaceHandle<maxwell::ContextPublisherController> ctl_handle;
    ctl_.Bind(&ctl_handle);
    publisher_->Publish("/location/region", std::move(ctl_handle),
                        out_.NewRequest());
  }

  void OnHasSubscribers() override {
    fidl::InterfaceHandle<maxwell::ContextSubscriberLink> in_handle;
    in_.Bind(&in_handle);
    subscriber_->Subscribe(maxwell::acquirers::GpsAcquirer::kLabel,
                           std::move(in_handle));
  }

  void OnNoSubscribers() override {
    in_.Unbind();
    out_->Update(NULL);
  }

  void OnUpdate(maxwell::ContextUpdatePtr update) override {
    FTL_VLOG(1) << "OnUpdate from " << update->source << ": "
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
  std::unique_ptr<app::ApplicationContext> app_context_;

  maxwell::ContextPublisherPtr publisher_;
  maxwell::ContextSubscriberPtr subscriber_;
  fidl::Binding<maxwell::ContextPublisherController> ctl_;
  fidl::Binding<maxwell::ContextSubscriberLink> in_;
  maxwell::ContextPublisherLinkPtr out_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  CarmenSandiegoApp app;
  loop.Run();
  return 0;
}
