// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include "weave_inspector.h"

namespace nl::Weave::testing {

// Inspect Node names
const std::string kNode_Reason = "reason";
const std::string kNode_State = "state";
const std::string kNode_Time = "@time";
const std::string kNode_Type = "type";
const std::string kNode_TunnelStatus = "tunnel_status";
const std::string kNode_TunnelStatus_TimeTunnelDown = "last_time_tunnel_down";
const std::string kNode_TunnelStatus_TimeTunnelEstablish = "last_time_tunnel_established";
const std::string kNode_WeaveStatus = "weave_status";
const std::string kNode_WeaveStatus_FailsafeState = "failsafe_state";
const std::string kNode_WeaveStatus_PairingState = "pairing_state";
const std::string kNode_WeaveStatus_TunnelState = "tunnel_state";
const std::string kNode_WeaveStatus_TunnelState_IsRestricted = "is_restricted";
const std::string kNode_WeaveStatus_SetupState = "setup_state";

class WeaveInspectorTest : public ::gtest::RealLoopFixture {
 public:
  // Get the underlying inspect::Inspector for Weave component.
  inspect::Inspector* GetInspector() {
    auto& weave_inspector = GetTestWeaveInspector();
    return weave_inspector.inspector_->inspector();
  }

  // Return WeaveInspector for test.
  WeaveInspector& GetTestWeaveInspector() { return weave_inspector_; }

  // Validate single Weave status entry node.
  void ValidateWeaveStatusEntryNode(
      const inspect::Hierarchy* weave_status_node,
      WeaveInspector::WeaveStatusNode::WeaveCurrentStatus weave_status_info) {
    ASSERT_TRUE(weave_status_node);

    auto* timestamp =
        weave_status_node->node().get_property<inspect::UintPropertyValue>(kNode_Time);
    ASSERT_TRUE(timestamp);

    auto* pairing_state = weave_status_node->node().get_property<inspect::StringPropertyValue>(
        kNode_WeaveStatus_PairingState);
    ASSERT_TRUE(pairing_state);
    EXPECT_EQ(pairing_state->value(), weave_status_info.pairing_state_);

    auto* setup_state = weave_status_node->node().get_property<inspect::StringPropertyValue>(
        kNode_WeaveStatus_SetupState);
    ASSERT_TRUE(setup_state);
    EXPECT_EQ(setup_state->value(), weave_status_info.setup_state_);

    auto* tunnel_state = weave_status_node->GetByPath({kNode_WeaveStatus_TunnelState});
    ASSERT_TRUE(tunnel_state);

    auto* is_restricted = tunnel_state->node().get_property<inspect::BoolPropertyValue>(
        kNode_WeaveStatus_TunnelState_IsRestricted);
    ASSERT_TRUE(setup_state);
    EXPECT_EQ(is_restricted->value(), weave_status_info.tunnel_restricted_);

    auto* tunnel_state_value =
        tunnel_state->node().get_property<inspect::StringPropertyValue>(kNode_State);
    ASSERT_TRUE(setup_state);
    EXPECT_EQ(tunnel_state_value->value(), weave_status_info.tunnel_state_);

    auto* tunnel_state_type =
        tunnel_state->node().get_property<inspect::StringPropertyValue>(kNode_Type);
    ASSERT_TRUE(tunnel_state_type);
    EXPECT_EQ(tunnel_state_type->value(), weave_status_info.tunnel_type_);

    auto* failsafe_state = weave_status_node->GetByPath({kNode_WeaveStatus_FailsafeState});
    ASSERT_TRUE(failsafe_state);

    auto* failsafe_state_value =
        failsafe_state->node().get_property<inspect::StringPropertyValue>(kNode_State);
    ASSERT_TRUE(failsafe_state_value);
    EXPECT_EQ(failsafe_state_value->value(), weave_status_info.failsafe_state_);

    auto* failsafe_state_reason =
        failsafe_state->node().get_property<inspect::StringPropertyValue>(kNode_Reason);
    ASSERT_TRUE(failsafe_state_reason);
    EXPECT_EQ(failsafe_state_reason->value(), weave_status_info.failsafe_state_reason_);
  }

  // Returns Weave status info filled with init values.
  WeaveInspector::WeaveStatusNode::WeaveCurrentStatus GetInitializedWeaveStatusInfo() {
    WeaveInspector::WeaveStatusNode::WeaveCurrentStatus weave_status_info;
    weave_status_info.tunnel_restricted_ = false;
    weave_status_info.tunnel_state_ = WeaveInspector::kTunnelState_NoTunnel;
    weave_status_info.tunnel_type_ = WeaveInspector::kTunnelType_None;
    weave_status_info.failsafe_state_ = WeaveInspector::kFailSafeState_Disarmed;
    weave_status_info.failsafe_state_reason_ = WeaveInspector::kFailSafeReason_Init;
    weave_status_info.pairing_state_ = WeaveInspector::kPairingState_Initialized;
    weave_status_info.setup_state_ = WeaveInspector::kSetupState_Initialized;
    return weave_status_info;
  }

 private:
  WeaveInspector weave_inspector_;
};

TEST_F(WeaveInspectorTest, NotifyInit) {
  auto& weave_inspector = GetTestWeaveInspector();
  weave_inspector.NotifyInit();

  fpromise::result<inspect::Hierarchy> hierarchy =
      RunPromise(inspect::ReadFromInspector(*GetInspector()));
  ASSERT_TRUE(hierarchy.is_ok());

  auto* tunnel_status = hierarchy.value().GetByPath({kNode_TunnelStatus});
  ASSERT_TRUE(tunnel_status);

  auto* last_time_tunnel_down = tunnel_status->node().get_property<inspect::UintPropertyValue>(
      kNode_TunnelStatus_TimeTunnelDown);
  ASSERT_TRUE(last_time_tunnel_down);
  EXPECT_EQ(last_time_tunnel_down->value(), 0u);

  auto* last_time_tunnel_up = tunnel_status->node().get_property<inspect::UintPropertyValue>(
      kNode_TunnelStatus_TimeTunnelEstablish);
  ASSERT_TRUE(last_time_tunnel_up);
  EXPECT_EQ(last_time_tunnel_up->value(), 0u);

  auto* weave_status = hierarchy.value().GetByPath({kNode_WeaveStatus});
  ASSERT_TRUE(weave_status);

  auto* weave_status_node = weave_status->GetByPath({"0x0"});

  ValidateWeaveStatusEntryNode(weave_status_node, GetInitializedWeaveStatusInfo());
}

TEST_F(WeaveInspectorTest, NotifyWeaveStatusChanges) {
  auto status = GetInitializedWeaveStatusInfo();
  auto& weave_inspector = GetTestWeaveInspector();

  weave_inspector.NotifySetupStateChange(WeaveInspector::kSetupState_PASESessionEstablished);
  weave_inspector.NotifyFailSafeStateChange(WeaveInspector::kFailSafeState_Armed,
                                            WeaveInspector::kFailSafeReason_Nominal);
  weave_inspector.NotifyPairingStateChange(WeaveInspector::kPairingState_FabricCreatedOrJoined);
  weave_inspector.NotifyPairingStateChange(
      WeaveInspector::kPairingState_ThreadNetworkCreatedOrJoined);
  weave_inspector.NotifyTunnelStateChange(WeaveInspector::kTunnelState_PrimaryTunMode,
                                          WeaveInspector::kTunnelType_Primary, true);
  weave_inspector.NotifyTunnelStateChange(WeaveInspector::kTunnelState_PrimaryTunMode,
                                          WeaveInspector::kTunnelType_Primary, false);
  weave_inspector.NotifyFailSafeStateChange(WeaveInspector::kFailSafeState_Armed,
                                            WeaveInspector::kFailSafeReason_FailsafeDisarmFailed);

  fpromise::result<inspect::Hierarchy> hierarchy =
      RunPromise(inspect::ReadFromInspector(*GetInspector()));
  ASSERT_TRUE(hierarchy.is_ok());

  auto* weave_status = hierarchy.value().GetByPath({kNode_WeaveStatus});
  ASSERT_TRUE(weave_status);

  auto* weave_status_node_0 = weave_status->GetByPath({"0x0"});
  status.setup_state_ = WeaveInspector::kSetupState_PASESessionEstablished;
  ValidateWeaveStatusEntryNode(weave_status_node_0, status);

  auto* weave_status_node_1 = weave_status->GetByPath({"0x1"});
  status.failsafe_state_ = WeaveInspector::kFailSafeState_Armed;
  status.failsafe_state_reason_ = WeaveInspector::kFailSafeReason_Nominal;
  ValidateWeaveStatusEntryNode(weave_status_node_1, status);

  auto* weave_status_node_2 = weave_status->GetByPath({"0x2"});
  status.pairing_state_ = WeaveInspector::kPairingState_FabricCreatedOrJoined;
  ValidateWeaveStatusEntryNode(weave_status_node_2, status);

  auto* weave_status_node_3 = weave_status->GetByPath({"0x3"});
  status.pairing_state_ = WeaveInspector::kPairingState_ThreadNetworkCreatedOrJoined;
  ValidateWeaveStatusEntryNode(weave_status_node_3, status);

  auto* weave_status_node_4 = weave_status->GetByPath({"0x4"});
  status.tunnel_state_ = WeaveInspector::kTunnelState_PrimaryTunMode;
  status.tunnel_type_ = WeaveInspector::kTunnelType_Primary;
  status.tunnel_restricted_ = true;
  ValidateWeaveStatusEntryNode(weave_status_node_4, status);

  auto* weave_status_node_5 = weave_status->GetByPath({"0x5"});
  status.tunnel_state_ = WeaveInspector::kTunnelState_PrimaryTunMode;
  status.tunnel_type_ = WeaveInspector::kTunnelType_Primary;
  status.tunnel_restricted_ = false;
  ValidateWeaveStatusEntryNode(weave_status_node_5, status);

  auto* weave_status_node_6 = weave_status->GetByPath({"0x6"});
  status.failsafe_state_ = WeaveInspector::kFailSafeState_Armed;
  status.failsafe_state_reason_ = WeaveInspector::kFailSafeReason_FailsafeDisarmFailed;
  ValidateWeaveStatusEntryNode(weave_status_node_6, status);
}

TEST_F(WeaveInspectorTest, NotifyTunnelStatusChanges) {
  auto status = GetInitializedWeaveStatusInfo();
  auto& weave_inspector = GetTestWeaveInspector();
  uint64_t tunnel_down_time_value = 0;
  uint64_t tunnel_up_time_value = 0;

  {
    weave_inspector.NotifyTunnelStateChange(WeaveInspector::kTunnelState_PrimaryTunMode,
                                            WeaveInspector::kTunnelType_Primary, true);
    fpromise::result<inspect::Hierarchy> hierarchy =
        RunPromise(inspect::ReadFromInspector(*GetInspector()));
    ASSERT_TRUE(hierarchy.is_ok());
    auto* tunnel_status = hierarchy.value().GetByPath({kNode_TunnelStatus});
    ASSERT_TRUE(tunnel_status);
    auto* last_time_tunnel_up = tunnel_status->node().get_property<inspect::UintPropertyValue>(
        kNode_TunnelStatus_TimeTunnelEstablish);
    ASSERT_TRUE(last_time_tunnel_up);
    tunnel_up_time_value = last_time_tunnel_up->value();
    EXPECT_NE(tunnel_up_time_value, 0u);
  }

  {
    weave_inspector.NotifyTunnelStateChange(WeaveInspector::kTunnelState_PrimaryTunMode,
                                            WeaveInspector::kTunnelType_Primary, false);
    fpromise::result<inspect::Hierarchy> hierarchy =
        RunPromise(inspect::ReadFromInspector(*GetInspector()));
    ASSERT_TRUE(hierarchy.is_ok());
    auto* tunnel_status = hierarchy.value().GetByPath({kNode_TunnelStatus});
    ASSERT_TRUE(tunnel_status);
    auto* last_time_tunnel_up = tunnel_status->node().get_property<inspect::UintPropertyValue>(
        kNode_TunnelStatus_TimeTunnelEstablish);
    ASSERT_TRUE(last_time_tunnel_up);
    auto new_tunnel_up_time_value = last_time_tunnel_up->value();
    EXPECT_EQ(tunnel_up_time_value, new_tunnel_up_time_value);
  }
  {
    weave_inspector.NotifyTunnelStateChange(WeaveInspector::kTunnelState_NoTunnel,
                                            WeaveInspector::kTunnelType_None, false);

    fpromise::result<inspect::Hierarchy> hierarchy =
        RunPromise(inspect::ReadFromInspector(*GetInspector()));
    ASSERT_TRUE(hierarchy.is_ok());
    auto* tunnel_status = hierarchy.value().GetByPath({kNode_TunnelStatus});
    ASSERT_TRUE(tunnel_status);
    auto* last_time_tunnel_down = tunnel_status->node().get_property<inspect::UintPropertyValue>(
        kNode_TunnelStatus_TimeTunnelEstablish);
    ASSERT_TRUE(last_time_tunnel_down);
    tunnel_down_time_value = last_time_tunnel_down->value();
    EXPECT_NE(tunnel_down_time_value, 0u);
  }
}

}  // namespace nl::Weave::testing
