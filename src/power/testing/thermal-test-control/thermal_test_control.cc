// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/thermal/cpp/fidl.h>
#include <fuchsia/thermal/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <vector>

class ControllerImpl : public fuchsia::thermal::Controller, public fuchsia::thermal::test::Control {
  struct Subscriber {
    fidl::InterfacePtr<fuchsia::thermal::Actor> actor;
    fuchsia::thermal::ActorType type;
    std::vector<fuchsia::thermal::TripPoint> points;
  };

 public:
  void Subscribe(fidl::InterfaceHandle<fuchsia::thermal::Actor> actor,
                 fuchsia::thermal::ActorType actor_type,
                 std::vector<fuchsia::thermal::TripPoint> trip_points,
                 SubscribeCallback callback) override {
    FX_LOGS(TRACE) << "actor_type " << static_cast<uint32_t>(actor_type) << ", trip_points["
                   << trip_points.size() << "]";
    for (auto i = 0u; i < trip_points.size(); ++i) {
      FX_LOGS(TRACE) << "  point[" << i << "]:";
      FX_LOGS(TRACE) << "    deactivate_below:" << trip_points[i].deactivate_below;
      FX_LOGS(TRACE) << "    activate_at:" << trip_points[i].activate_at;
    }

    auto interface = actor.Bind();
    subscribers_.push_back(
        {.actor = std::move(interface), .type = actor_type, .points = std::move(trip_points)});

    callback({fit::ok()});
  }

  void GetSupportedThermalStates(GetSupportedThermalStatesCallback callback) override {
    std::vector<uint32_t> subscribers;

    // For each thermal subscriber, return the number of supported thermal states.
    for (auto i = 0u; i < subscribers_.size(); ++i) {
      // All subscribers support thermal state 0, so 'N' trip_points implies 'N+1' thermal states.
      subscribers.push_back(static_cast<uint32_t>(subscribers_[i].points.size() + 1));
    }

    callback(std::move(subscribers));
  }

  void SetThermalState(uint32_t subscriber_index, uint32_t state,
                       SetThermalStateCallback callback) override {
    FX_CHECK(subscriber_index < subscribers_.size());
    FX_CHECK(state <= subscribers_[subscriber_index].points.size());

    subscribers_[subscriber_index].actor->SetThermalState(state,
                                                          [cbk = std::move(callback)]() { cbk(); });
  }

 private:
  std::vector<Subscriber> subscribers_;
};

int main(int argc, const char** argv) {
  syslog::SetTags({"thermal_test_control"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  ControllerImpl impl;

  fidl::Binding<fuchsia::thermal::Controller> thermal_binding(&impl);
  fidl::InterfaceRequestHandler<fuchsia::thermal::Controller> thermal_handler =
      [&](fidl::InterfaceRequest<fuchsia::thermal::Controller> request) {
        thermal_binding.Bind(std::move(request));
      };
  context->outgoing()->AddPublicService(std::move(thermal_handler));

  fidl::Binding<fuchsia::thermal::test::Control> test_control_binding(&impl);
  fidl::InterfaceRequestHandler<fuchsia::thermal::test::Control> test_control_handler =
      [&](fidl::InterfaceRequest<fuchsia::thermal::test::Control> request) {
        test_control_binding.Bind(std::move(request));
      };
  context->outgoing()->AddPublicService(std::move(test_control_handler));

  FX_LOGS(INFO) << "Thermal test control is running";
  return loop.Run();
}
