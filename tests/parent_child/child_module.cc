// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/story/module.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

class ChildApp : public modular::SingleServiceApp<modular::Module> {
 public:
  ChildApp() { modular::testing::Init(application_context(), __FILE__); }

  ~ChildApp() override { mtl::MessageLoop::GetCurrent()->PostQuitTask(); }

 private:
  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::Story> story,
      fidl::InterfaceHandle<modular::Link> link,
      fidl::InterfaceHandle<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override {
    story_.Bind(std::move(story));
    link_.Bind(std::move(link));
    initialized_.Pass();
    modular::testing::GetStore()->Put("child_module_init", "", [] {});
  }

  // |Module|
  void Stop(const StopCallback& done) override {
    stopped_.Pass();
    modular::testing::GetStore()->Put("child_module_stop", "", [] {});
    modular::testing::Done();
    done();
    delete this;
  }

  modular::StoryPtr story_;
  modular::LinkPtr link_;

  TestPoint initialized_{"Child module initialized"};
  TestPoint stopped_{"Child module stopped"};
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  new ChildApp();
  loop.Run();
  return 0;
}
