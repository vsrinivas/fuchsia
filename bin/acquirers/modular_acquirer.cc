// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/acquirers/modular_acquirer.h"

#include "apps/maxwell/services/context/client.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"

using maxwell::acquirers::ModularAcquirer;

constexpr char ModularAcquirer::kLabel[];
constexpr char ModularAcquirer::kSchema[];

namespace {

constexpr ftl::TimeDelta kModularAcquirerUpdatePeriod =
    ftl::TimeDelta::FromSeconds(10);
#define KEEP_ALIVE_TICKS 3
#define HAS_SUBSCRIBERS -1

class ModularAcquirerApp : public ModularAcquirer,
                           public maxwell::context::PublisherController {
 public:
  ModularAcquirerApp()
      : app_ctx_(modular::ApplicationContext::CreateFromStartupInfo()),
        ctl_(this) {
    srand(time(NULL));

    auto cx = app_ctx_->ConnectToEnvironmentService<
        maxwell::context::ContextAcquirerClient>();

    fidl::InterfaceHandle<maxwell::context::PublisherController> ctl_handle;
    ctl_.Bind(&ctl_handle);

    cx->Publish(kLabel, kSchema, std::move(ctl_handle), GetProxy(&out_));
  }

  void OnHasSubscribers() override {
    if (!tick_keep_alive_) {
      FTL_LOG(INFO) << "Modular acquirer has subscribers";
      tick_keep_alive_ = HAS_SUBSCRIBERS;
      PublishingTick();
    } else {
      tick_keep_alive_ = HAS_SUBSCRIBERS;
    }
  }

  void OnNoSubscribers() override {
    tick_keep_alive_ = KEEP_ALIVE_TICKS;
    FTL_LOG(INFO) << "Modular acquirer subscribers lost; continuing to track "
                     "state for "
                  << KEEP_ALIVE_TICKS << " seconds";
  }

 private:
  inline void PublishModularState() {
    std::ostringstream json;
    json << "{ \"modular_state\": " << modular_state << " }";
    FTL_LOG(INFO) << ": " << json.str();

    out_->Update(json.str());
  }

  void PublishingTick() {
    if (tick_keep_alive_ > 0) {
      tick_keep_alive_--;
    }

    PublishModularState();

    if (!tick_keep_alive_) {
      FTL_LOG(INFO) << "Modular acquirer off";
      out_->Update(NULL);
      return;
    }

    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this] { PublishingTick(); }, kModularAcquirerUpdatePeriod);
  }

  std::unique_ptr<modular::ApplicationContext> app_ctx_;

  fidl::Binding<maxwell::context::PublisherController> ctl_;
  maxwell::context::PublisherLinkPtr out_;
  int tick_keep_alive_;

  // TODO(afergan): Once we figure out all of the possible states of
  // user_runner or  SysUI (on the timeline, running a story, etc.), turn this
  // into an enum.
  int modular_state = 0;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ModularAcquirerApp app;
  loop.Run();
  return 0;
}
