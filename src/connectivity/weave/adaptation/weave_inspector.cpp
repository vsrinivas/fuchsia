// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "weave_inspector.h"

#include <lib/syslog/cpp/macros.h>

#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>

using namespace nl::Weave::Profiles;

namespace nl::Weave {

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

constexpr int kMaxWeaveStatusEntries = 25;

// Inspect Node values
const WeaveInspector::WeaveStatus_FailSafeReason WeaveInspector::kFailSafeReason_Init = "Init";
const WeaveInspector::WeaveStatus_FailSafeReason WeaveInspector::kFailSafeReason_Nominal =
    "Nominal";
const WeaveInspector::WeaveStatus_FailSafeReason WeaveInspector::kFailSafeReason_FailsafeArmFailed =
    "FailsafeArmFailed";
const WeaveInspector::WeaveStatus_FailSafeReason
    WeaveInspector::kFailSafeReason_FailsafeDisarmFailed = "FailsafeDisarmFailed";

const WeaveInspector::WeaveStatus_FailSafeState WeaveInspector::kFailSafeState_Armed = "Armed";
const WeaveInspector::WeaveStatus_FailSafeState WeaveInspector::kFailSafeState_Disarmed =
    "Disarmed";

const WeaveInspector::WeaveStatus_PairingState WeaveInspector::kPairingState_Initialized =
    "Initialized";
const WeaveInspector::WeaveStatus_PairingState WeaveInspector::kPairingState_FabricCreatedOrJoined =
    "FabricCreatedOrJoined";
const WeaveInspector::WeaveStatus_PairingState
    WeaveInspector::kPairingState_ThreadNetworkCreatedOrJoined = "ThreadNetworkCreatedOrJoined";
const WeaveInspector::WeaveStatus_PairingState
    WeaveInspector::kPairingState_RegisterServicePending = "RegisterServicePending";
const WeaveInspector::WeaveStatus_PairingState
    WeaveInspector::kPairingState_RegisterServiceCompleted = "RegisterServiceCompleted";
const WeaveInspector::WeaveStatus_PairingState WeaveInspector::kPairingState_ServiceConfigUpdated =
    "ServiceConfigUpdated";
const WeaveInspector::WeaveStatus_PairingState WeaveInspector::kPairingState_LeftFabric =
    "LeftFabric";

const WeaveInspector::WeaveStatus_SetupState WeaveInspector::kSetupState_Initialized =
    "Initialized";
const WeaveInspector::WeaveStatus_SetupState WeaveInspector::kSetupState_BLEConnected =
    "BLEConnected";
const WeaveInspector::WeaveStatus_SetupState WeaveInspector::kSetupState_PASESessionEstablished =
    "PASESessionEstablished";
const WeaveInspector::WeaveStatus_SetupState WeaveInspector::kSetupState_CASESessionEstablished =
    "CASESessionEstablished";

const WeaveInspector::WeaveStatus_TunnelState WeaveInspector::kTunnelState_NoTunnel = "NoTunnel";
const WeaveInspector::WeaveStatus_TunnelState WeaveInspector::kTunnelState_PrimaryTunMode =
    "PrimaryTunMode";
const WeaveInspector::WeaveStatus_TunnelState WeaveInspector::kTunnelState_BkupOnlyTunMode =
    "BkupOnlyTunMode";
const WeaveInspector::WeaveStatus_TunnelState WeaveInspector::kTunnelState_PrimaryAndBkupTunMode =
    "PrimaryAndBkupTunMode";

const WeaveInspector::WeaveStatus_TunnelType WeaveInspector::kTunnelType_None = "None";
const WeaveInspector::WeaveStatus_TunnelType WeaveInspector::kTunnelType_Primary = "Primary";
const WeaveInspector::WeaveStatus_TunnelType WeaveInspector::kTunnelType_Backup = "Backup";
const WeaveInspector::WeaveStatus_TunnelType WeaveInspector::kTunnelType_Shortcut = "Shortcut";

WeaveInspector& WeaveInspector::GetWeaveInspector() {
  static WeaveInspector weave_inspector;
  return weave_inspector;
}

WeaveInspector::WeaveInspector()
    : inspector_(std::make_unique<sys::ComponentInspector>(
          DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess())),
      tunnel_status_(inspector_->root()),
      weave_status_(inspector_->root()) {}

void WeaveInspector::NotifyInit() {
  weave_status_.LogCurrentStatus();
  tunnel_status_.RecordTunnelEstablishedTime(0);
  tunnel_status_.RecordTunnelDownTime(0);
}

void WeaveInspector::NotifyPairingStateChange(const WeaveStatus_PairingState& new_state) {
  weave_status_.RecordPairingStateChange(new_state);
}

void WeaveInspector::NotifyFailSafeStateChange(const WeaveStatus_FailSafeState& new_state,
                                               const WeaveStatus_FailSafeReason& reason) {
  weave_status_.RecordFailSafeStateChange(new_state, reason);
}

void WeaveInspector::NotifySetupStateChange(const WeaveStatus_SetupState& new_state) {
  weave_status_.RecordSetupStateChange(new_state);
}

void WeaveInspector::NotifyTunnelStateChange(const WeaveStatus_TunnelState& new_state,
                                             const WeaveStatus_TunnelType& tunnel_type,
                                             const bool is_restricted) {
  WeaveStatus_TunnelState old_tunnel_state = weave_status_.GetCurrentWeaveStatus().tunnel_state_;
  weave_status_.RecordTunnelStateChange(new_state, tunnel_type, is_restricted);
  if (old_tunnel_state == new_state) {
    return;
  }
  if (new_state == kTunnelState_NoTunnel) {
    tunnel_status_.RecordTunnelDownTime(zx::clock::get_monotonic().get());
    return;
  }
  tunnel_status_.RecordTunnelEstablishedTime(zx::clock::get_monotonic().get());
}

WeaveInspector::WeaveStatusNode::WeaveStatusEntry::WeaveStatusEntry(
    inspect::Node& parent_node, const WeaveCurrentStatus& current_status)
    : weave_status_entry_node_(parent_node.CreateChild(parent_node.UniqueName(""))) {
  setup_state_property_ = weave_status_entry_node_.CreateString(kNode_WeaveStatus_SetupState,
                                                                current_status.setup_state_);
  pairing_state_property_ = weave_status_entry_node_.CreateString(kNode_WeaveStatus_PairingState,
                                                                  current_status.pairing_state_);

  failsafe_state_.state_node_ =
      weave_status_entry_node_.CreateChild(kNode_WeaveStatus_FailsafeState);
  failsafe_state_.state_property_ =
      failsafe_state_.state_node_.CreateString(kNode_State, current_status.failsafe_state_);
  failsafe_state_.reason_property_ =
      failsafe_state_.state_node_.CreateString(kNode_Reason, current_status.failsafe_state_reason_);

  tunnel_state_.state_node_ = weave_status_entry_node_.CreateChild(kNode_WeaveStatus_TunnelState);
  tunnel_state_.state_property_ =
      tunnel_state_.state_node_.CreateString(kNode_State, current_status.tunnel_state_);
  tunnel_state_.type_property_ =
      tunnel_state_.state_node_.CreateString(kNode_Type, current_status.tunnel_type_);
  tunnel_state_.is_restricted_property_ = tunnel_state_.state_node_.CreateBool(
      kNode_WeaveStatus_TunnelState_IsRestricted, current_status.tunnel_restricted_);

  timestamp_property_ =
      weave_status_entry_node_.CreateUint(kNode_Time, zx::clock::get_monotonic().get());
}

WeaveInspector::WeaveStatusNode::WeaveCurrentStatus::WeaveCurrentStatus()
    : tunnel_restricted_(false),
      tunnel_state_(WeaveInspector::kTunnelState_NoTunnel),
      tunnel_type_(WeaveInspector::kTunnelType_None),
      failsafe_state_(WeaveInspector::kFailSafeState_Disarmed),
      failsafe_state_reason_(WeaveInspector::kFailSafeReason_Init),
      pairing_state_(WeaveInspector::kPairingState_Initialized),
      setup_state_(WeaveInspector::kSetupState_Initialized) {}

WeaveInspector::WeaveStatusNode::WeaveStatusNode(inspect::Node& parent_node)
    : node_(parent_node.CreateChild(kNode_WeaveStatus)),
      weave_status_entries_(kMaxWeaveStatusEntries) {}

void WeaveInspector::WeaveStatusNode::LogCurrentStatus() {
  weave_status_entries_.AddEntry(node_, current_status_);
}

void WeaveInspector::WeaveStatusNode::RecordPairingStateChange(
    const WeaveInspector::WeaveStatus_PairingState& new_state) {
  if (current_status_.pairing_state_ == new_state) {
    return;
  }
  current_status_.pairing_state_ = new_state;
  LogCurrentStatus();
}

void WeaveInspector::WeaveStatusNode::RecordFailSafeStateChange(
    const WeaveInspector::WeaveStatus_FailSafeState& new_state,
    const WeaveInspector::WeaveStatus_FailSafeReason& reason) {
  if (current_status_.failsafe_state_ == new_state &&
      current_status_.failsafe_state_reason_ == reason) {
    return;
  }
  current_status_.failsafe_state_ = new_state;
  current_status_.failsafe_state_reason_ = reason;
  LogCurrentStatus();
}
void WeaveInspector::WeaveStatusNode::RecordSetupStateChange(
    const WeaveInspector::WeaveStatus_SetupState& new_state) {
  if (current_status_.setup_state_ == new_state) {
    return;
  }
  current_status_.setup_state_ = new_state;
  LogCurrentStatus();
}
void WeaveInspector::WeaveStatusNode::RecordTunnelStateChange(
    const WeaveInspector::WeaveStatus_TunnelState& new_state,
    const WeaveInspector::WeaveStatus_TunnelType& tunnel_type, const bool is_restricted) {
  if (current_status_.tunnel_state_ == new_state && current_status_.tunnel_type_ == tunnel_type &&
      current_status_.tunnel_restricted_ == is_restricted) {
    return;
  }
  current_status_.tunnel_state_ = new_state;
  current_status_.tunnel_type_ = tunnel_type;
  current_status_.tunnel_restricted_ = is_restricted;
  LogCurrentStatus();
}

WeaveInspector::TunnelStatusNode::TunnelStatusNode(inspect::Node& parent_node)
    : node_(parent_node.CreateChild(kNode_TunnelStatus)) {}

void WeaveInspector::TunnelStatusNode::RecordTunnelEstablishedTime(uint64_t time_value) {
  tunnel_established_time_property_ =
      node_.CreateUint(kNode_TunnelStatus_TimeTunnelEstablish, time_value);
}
void WeaveInspector::TunnelStatusNode::RecordTunnelDownTime(uint64_t time_value) {
  tunnel_down_time_property_ = node_.CreateUint(kNode_TunnelStatus_TimeTunnelDown, time_value);
}

}  // namespace nl::Weave
