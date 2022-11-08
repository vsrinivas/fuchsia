// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONNECTIVITY_MANAGER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONNECTIVITY_MANAGER_H_

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/internal/ServiceTunnelAgent.h>
#pragma GCC diagnostic pop
// clang-format on

#include <lib/fit/defer.h>

#include <gtest/gtest.h>

#include "connectivity_manager_delegate_impl.h"

namespace weave::adaptation::testing {

class TestConnectivityManager final
    : public nl::Weave::DeviceLayer::ConnectivityManagerDelegateImpl {
 public:
  static constexpr char kWiFiInterfaceName[] = "wlan0";

  // Access underlying delegate implementation.
  using Impl = nl::Weave::DeviceLayer::ConnectivityManagerDelegateImpl;
  using WeaveTunnelAgent = nl::Weave::Profiles::WeaveTunnel::WeaveTunnelAgent;

  class TestWeaveTunnelAgent : public WeaveTunnelAgent {
   public:
    // Starts the service tunnel.
    WEAVE_ERROR StartServiceTunnel() override {
      return start_service_tunnel_error_.value_or(WEAVE_NO_ERROR);
    }

    // Stops the service tunnel.
    void StopServiceTunnel(WEAVE_ERROR error) override {
      // It is invalid to get an error that isn't TUNNEL_FORCE_ABORT.
      ASSERT_EQ(error, WEAVE_ERROR_TUNNEL_FORCE_ABORT);
    }

    // Returns whether tunnel routing is restricted.
    bool IsTunnelRoutingRestricted() override {
      return is_tunnel_routing_restricted_.value_or(WEAVE_NO_ERROR);
    }

    // Set StartServiceTunnel error.
    void set_start_service_tunnel_error(std::optional<WEAVE_ERROR> start_service_tunnel_error) {
      start_service_tunnel_error_ = start_service_tunnel_error;
    }

    // Set whether tunnel routing is restricted.
    void set_is_tunnel_routing_restricted(std::optional<bool> is_tunnel_routing_restricted) {
      is_tunnel_routing_restricted_ = is_tunnel_routing_restricted;
    }

    // Mark ownership of this instance.
    void acquire() {
      ASSERT_FALSE(is_acquired_);
      is_acquired_ = true;
      // On acquisition, reset all variables.
      start_service_tunnel_error_ = std::nullopt;
      is_tunnel_routing_restricted_ = std::nullopt;
    }

    // Release ownership of this instance.
    void release() {
      ASSERT_TRUE(is_acquired_);
      is_acquired_ = false;
    }

   private:
    std::optional<WEAVE_ERROR> start_service_tunnel_error_;
    std::optional<bool> is_tunnel_routing_restricted_;
    bool is_acquired_;
  };

  // Returns the WiFi interface name.
  std::optional<std::string> GetWiFiInterfaceName() override { return kWiFiInterfaceName; }

  // Refreshes existing endpoints.
  WEAVE_ERROR RefreshEndpoints() override {
    endpoints_refreshed_ = true;
    return refresh_endpoints_error_.value_or(WEAVE_NO_ERROR);
  }

  // Sets the service tunnel agent.
  WEAVE_ERROR InitServiceTunnelAgent() override {
    WEAVE_ERROR error = init_service_tunnel_error_.value_or(WEAVE_NO_ERROR);
    if (error == WEAVE_NO_ERROR) {
      new (&nl::Weave::DeviceLayer::Internal::ServiceTunnelAgent) ProxyWeaveTunnelAgent();
      // Acquire the static instance and set the deferred callback to ensure it
      // is released once this class is released.
      test_weave_tunnel_agent_.acquire();
      defer_test_weave_tunnel_agent_release_ =
          fit::defer<fit::closure>([] { test_weave_tunnel_agent_.release(); });
    }
    return error;
  }

  fuchsia::net::interfaces::admin::ControlSyncPtr* GetTunInterfaceControlSyncPtr() override {
    return &tun_control_sync_ptr_;
  };

  // Set service tunnel agent initialization error.
  void set_init_service_tunnel_error(std::optional<WEAVE_ERROR> init_service_tunnel_error) {
    init_service_tunnel_error_ = init_service_tunnel_error;
  }

  // Sets the endpoint refresh error.
  void set_refresh_endpoints_error(std::optional<WEAVE_ERROR> refresh_endpoints_error) {
    refresh_endpoints_error_ = refresh_endpoints_error;
  }

  // Sets endpoint refresh state.
  void set_endpoints_refreshed(bool endpoints_refreshed) {
    endpoints_refreshed_ = endpoints_refreshed;
  }

  // Gets endpoint refresh state.
  bool get_endpoints_refreshed() const { return endpoints_refreshed_; }

  // Gets whether the service tunnel has started.
  bool get_service_tunnel_started() const {
    return nl::GetFlag(flags_, kFlag_ServiceTunnelStarted);
  }

  // Gets whether the service tunnel is up.
  bool get_service_tunnel_up() const { return nl::GetFlag(flags_, kFlag_ServiceTunnelUp); }

 private:
  static inline TestWeaveTunnelAgent test_weave_tunnel_agent_;
  fit::deferred_action<fit::closure> defer_test_weave_tunnel_agent_release_;

  // A helper proxy class that has the same byte-alignment as WeaveTunnelAgent,
  // to support tests issuing a placement-new on TestWeaveTunnelAgent. This
  // class should only invoke the static TestWeaveTunnelAgent instance and
  // should not introduce new member variables.
  class ProxyWeaveTunnelAgent : public WeaveTunnelAgent {
    // Starts the service tunnel.
    WEAVE_ERROR StartServiceTunnel() override {
      return test_weave_tunnel_agent_.StartServiceTunnel();
    }

    // Stops the service tunnel.
    void StopServiceTunnel(WEAVE_ERROR error) override {
      return test_weave_tunnel_agent_.StopServiceTunnel(error);
    }

    // Returns whether tunnel routing is restricted.
    bool IsTunnelRoutingRestricted() override {
      return test_weave_tunnel_agent_.IsTunnelRoutingRestricted();
    }
  };

  std::optional<WEAVE_ERROR> init_service_tunnel_error_;
  std::optional<WEAVE_ERROR> refresh_endpoints_error_;
  bool endpoints_refreshed_ = false;
  fuchsia::net::interfaces::admin::ControlSyncPtr tun_control_sync_ptr_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONNECTIVITY_MANAGER_H_
