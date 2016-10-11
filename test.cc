// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/bindings/formatting.h"
#include "mojo/public/cpp/utility/run_loop.h"

namespace maxwell {
namespace suggestion_engine {

std::ostream& operator<<(std::ostream& os,
                         const SuggestionDisplayProperties& o) {
  return os << "{ icon: " << o.icon << ", headline: " << o.headline
            << ", subtext: " << o.subtext << ", details: " << o.details << "}";
}

std::ostream& operator<<(std::ostream& os, const Suggestion& o) {
  return os << "{ uuid: " << o.uuid << ", rank: " << o.rank
            << ", display_properties: " << o.display_properties << "}";
}

}  // suggestion_engine
}  // maxwell

namespace {

using namespace maxwell::suggestion_engine;

using mojo::ApplicationImplBase;
using mojo::Binding;
using mojo::InterfaceHandle;
using mojo::ServiceProvider;
using mojo::RunLoop;

#define ONE_MOJO_SECOND 1000000

class MaxwellTestApp : public ApplicationImplBase, public SuggestionListener {
 public:
  MaxwellTestApp() : listener_(this) {}

  void OnInitialize() override {
    srand(time(NULL));

    shell()->ConnectToApplication("mojo:acquirers/gps", GetProxy(&gps_));
    shell()->ConnectToApplication("mojo:agents/carmen_sandiego",
                                  GetProxy(&carmen_sandiego_));
    shell()->ConnectToApplication("mojo:agents/ideas", GetProxy(&ideas_));

    ConnectToService(shell(), "mojo:suggestion_engine", GetProxy(&s_));
    SuggestionListenerPtr lp;
    listener_.Bind(GetProxy(&lp));
    s_->SubscribeToNext(lp.PassInterfaceHandle(), GetProxy(&ctl_));
    ctl_->SetResultCount(3);
  }

  void OnAdd(mojo::Array<SuggestionPtr> suggestions) override {
    MOJO_LOG(INFO) << "OnAdd(" << suggestions << ")";
  }

  void OnRemove(const mojo::String& uuid) override {
    MOJO_LOG(INFO) << "OnRemove(\"" << uuid << "\")";
  }

  void OnRemoveAll() override { MOJO_LOG(INFO) << "OnRemoveAll"; }

 private:
  InterfaceHandle<ServiceProvider> gps_, carmen_sandiego_, ideas_;
  SuggestionManagerPtr s_;
  Binding<SuggestionListener> listener_;
  NextControllerPtr ctl_;
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  MaxwellTestApp test;
  return mojo::RunApplication(request, &test);
}
