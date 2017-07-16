// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This application is intenteded to be used for manual testing of
// the Cobalt encoder client on Fuchsia by Cobalt engineers.

#include <memory>
#include <string>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/cobalt_client/services/cobalt.fidl.h"
#include "apps/cobalt_client/services/customers.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

const uint32_t kTestAppProjectId = 2;
const uint32_t kRareEventMetricId = 1;
const uint32_t kRareEventEncodingId = 1;
const std::string kRareEventObservation1 = "Rare-event-1";

std::string StatusToString(cobalt::Status status) {
  switch (status) {
    case cobalt::Status::OK:
      return "OK";
    case cobalt::Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case cobalt::Status::SEND_FAILED:
      return "SEND_FAILED";
    case cobalt::Status::FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case cobalt::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
};

class CobaltTestApp {
 public:
  CobaltTestApp()
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {}

  void RunTests() {
    // Start and connect to the cobalt fidl service.
    app::ServiceProviderPtr services;
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "file://system/apps/cobalt";
    launch_info->services = services.NewRequest();
    context_->launcher()->CreateApplication(std::move(launch_info),
                                            GetProxy(&app_controller_));

    auto encoder_factory =
        app::ConnectToService<cobalt::CobaltEncoderFactory>(services.get());
    cobalt::CobaltEncoderPtr encoder;
    // TODO(azani): Switch to a test project.
    encoder_factory->GetEncoder(kTestAppProjectId, GetProxy(&encoder));

    // Add 7 observations of rare event 1 to the envelope.
    for (int i = 0; i < 7; i++) {
      encoder->AddStringObservation(
          kRareEventMetricId, kRareEventEncodingId, kRareEventObservation1,
          [i](cobalt::Status status) {
            FTL_LOG(INFO) << "Add(Rare-event-1) => " << StatusToString(status);
          });
    }

    // Send the observations.
    encoder->SendObservations([](cobalt::Status status) {
      // TODO(azani, rudominer) Why is this callback not getting invoked?
      FTL_LOG(INFO) << "SendObservations => " << StatusToString(status);
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
    });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  app::ApplicationControllerPtr app_controller_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CobaltTestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  FTL_LOG(INFO) << "CobaltTestApp";
  mtl::MessageLoop loop;
  CobaltTestApp app;
  app.RunTests();
  // TODO(azani, rudominer) Posting the quit task here should be unnecessary
  // because we do so in the callback to SendObservations(). But that callback
  // appears to never get invoked for some reason and so we need to post it
  // here or else the message loop never terminates.
  loop.PostQuitTask();
  FTL_LOG(INFO) << "Start";
  loop.Run();
  FTL_LOG(INFO) << "Done";
  return 0;
}
