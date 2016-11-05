// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/acquirers/gps.h"

#include "apps/maxwell/services/context_engine.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"

using maxwell::acquirers::GpsAcquirer;

constexpr char GpsAcquirer::kLabel[];
constexpr char GpsAcquirer::kSchema[];

namespace {

using namespace maxwell::context_engine;

constexpr ftl::TimeDelta kGpsUpdatePeriod = ftl::TimeDelta::FromSeconds(1);
#define KEEP_ALIVE_TICKS 3
#define HAS_SUBSCRIBERS -1

class GpsAcquirerImpl : public GpsAcquirer, public ContextPublisherController {
 public:
  GpsAcquirerImpl()
      : app_ctx_(modular::ApplicationContext::CreateFromStartupInfo()),
        ctl_(this) {
    srand(time(NULL));

    auto cx = app_ctx_->ConnectToEnvironmentService<ContextAcquirerClient>();

    fidl::InterfaceHandle<ContextPublisherController> ctl_handle;
    ctl_.Bind(GetProxy(&ctl_handle));

    cx->Publish(kLabel, kSchema, std::move(ctl_handle), GetProxy(&out_));
  }

  void OnHasSubscribers() override {
    if (!tick_keep_alive_) {
      FTL_LOG(INFO) << "GPS on";
      tick_keep_alive_ = HAS_SUBSCRIBERS;
      PublishingTick();
    } else {
      tick_keep_alive_ = HAS_SUBSCRIBERS;
    }
  }

  void OnNoSubscribers() override {
    tick_keep_alive_ = KEEP_ALIVE_TICKS;
    FTL_LOG(INFO) << "GPS subscribers lost; keeping GPS on for "
                  << KEEP_ALIVE_TICKS << " seconds";
  }

 private:
  inline void PublishLocation() {
    // For now, this representation must be agreed upon by all parties out of
    // band. In the future, we will want to represent most mathematical typing
    // information in schemas and any remaining semantic information in
    // manifests.
    std::ostringstream json;
    json << "{ \"lat\": " << rand() % 18001 / 100. - 90
         << ", \"lng\": " << rand() % 36001 / 100. - 180 << " }";

    FTL_LOG(INFO) << "Update by acquirers/gps: " << json.str();

    out_->Update(json.str());
  }

  void PublishingTick() {
    if (tick_keep_alive_ > 0) {
      tick_keep_alive_--;
    }

    PublishLocation();

    if (!tick_keep_alive_) {
      FTL_LOG(INFO) << "GPS off";
      out_->Update(NULL);
      return;
    }

    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this] { PublishingTick(); }, kGpsUpdatePeriod);
  }

  std::unique_ptr<modular::ApplicationContext> app_ctx_;

  fidl::Binding<ContextPublisherController> ctl_;
  ContextPublisherLinkPtr out_;
  int tick_keep_alive_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  GpsAcquirerImpl app;
  loop.Run();
  return 0;
}
