// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/story/module.fidl.h"
#include "apps/modular/services/component/component_context.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

constexpr int kStopMilliseconds = 250;

class ParentApp
  : public modular::SingleServiceApp<modular::Module> {
 public:
  ParentApp() {
    modular::testing::Init(application_context());
  }

  ~ParentApp() override = default;

 private:
  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::Story> story,
      fidl::InterfaceHandle<modular::Link> link,
      fidl::InterfaceHandle<modular::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<modular::ServiceProvider> outgoing_services)
      override {
    story_.Bind(std::move(story));
    link_.Bind(std::move(link));
    initialized_.Pass();

    // Exercise ComponentContext
    modular::ComponentContextPtr ctx;
    story_->GetComponentContext(ctx.NewRequest());
    ctx->DeleteMessageQueue("");

    // Start a timer to call Story.Done.
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this] { story_->Done(); },
        ftl::TimeDelta::FromMilliseconds(kStopMilliseconds));
  }

  // |Module|
  void Stop(const StopCallback& done) override {
    stopped_.Pass();
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    modular::testing::Teardown();
    done();
  }

  modular::StoryPtr story_;
  modular::LinkPtr link_;

  TestPoint initialized_{"Root module initialized"};
  TestPoint stopped_{"Root module stopped"};
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ParentApp app;
  loop.Run();
  TEST_PASS("Root module exited normally");
  return 0;
}
