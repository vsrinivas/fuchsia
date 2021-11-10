// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_INSPECTOR_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_INSPECTOR_H_

#include <lib/sys/inspect/cpp/component.h>

#pragma GCC diagnostic push
#include <Weave/DeviceLayer/PlatformManager.h>
#pragma GCC diagnostic pop

#include "utils.h"

namespace nl::Weave {

namespace testing {
class WeaveInspectorTest;
}  // namespace testing

// This class defines the structure of the Inspect hierarchy for Weave. Components
// of WeaveStack can invoke the WeaveInspector to log events pertaining to status
// and tunnel state changes. For example:
//
// auto inspector = nl::Weave::WeaveInspector::GetWeaveInspector();
// inspector.NotifyPairingStateChange(
//     nl::Weave::WeaveInspector::WeaveStatusNode::kPairingState_FabricCreatedOrJoined);
//
// The WeaveInspector holds types of WeaveStatusNode and TunnelStatusNode as sub-nodes
// of the Inspect hierarchy. WeaveStatusNode will contain multiple WeaveStatusEntry
// sub-nodes. These sub-nodes are added whenever a Weave status change is notified by
// WeaveStack and will contain complete information of current weave status. To obtain
// history of Weave status changes WeaveStatusNode will hold last |kMaxWeaveStatusEntries|.
// TunnelStatusNode will contain information on tunnel status changes. TunnelStatusNode will
// be updated whenever Tunnel status change is notified by WeaveStack.
//
// Sample Inspect hierarchy:
//   "root": {
//       "weave_status": {
//         "0x13": {
//           "setup_state": "CASESessionEstablished",
//           "pairing_state": "RegisterServiceCompleted",
//           "@time": 479097104375,
//           "tunnel_state": {
//             "state": "PrimaryTunMode",
//             "type": "Primary",
//             "is_restricted": false
//           },
//           "failsafe_state": {
//             "state": "Disarmed",
//             "reason": "Nominal"
//           }
//         },
//         "0x12": {
//           ..
//           ..
//         },
//         ..
//         ..
//       },
//       "tunnel_status": {
//         "last_time_tunnel_down": 0,
//         "last_time_tunnel_established": 478702584208
//       }
//     }
//   }

class WeaveInspector final {
 public:
  // Failsafe state & Reasons
  using WeaveStatus_FailSafeReason = std::string;
  static const WeaveStatus_FailSafeReason kFailSafeReason_Init;
  static const WeaveStatus_FailSafeReason kFailSafeReason_Nominal;
  static const WeaveStatus_FailSafeReason kFailSafeReason_FailsafeArmFailed;
  static const WeaveStatus_FailSafeReason kFailSafeReason_FailsafeDisarmFailed;

  using WeaveStatus_FailSafeState = std::string;
  static const WeaveStatus_FailSafeState kFailSafeState_Armed;
  static const WeaveStatus_FailSafeState kFailSafeState_Disarmed;

  // Paring State
  using WeaveStatus_PairingState = std::string;
  static const WeaveStatus_PairingState kPairingState_Initialized;
  static const WeaveStatus_PairingState kPairingState_FabricCreatedOrJoined;
  static const WeaveStatus_PairingState kPairingState_ThreadNetworkCreatedOrJoined;
  static const WeaveStatus_PairingState kPairingState_RegisterServicePending;
  static const WeaveStatus_PairingState kPairingState_RegisterServiceCompleted;
  static const WeaveStatus_PairingState kPairingState_ServiceConfigUpdated;
  static const WeaveStatus_PairingState kPairingState_LeftFabric;

  // Setup State
  using WeaveStatus_SetupState = std::string;
  static const WeaveStatus_SetupState kSetupState_Initialized;
  static const WeaveStatus_SetupState kSetupState_BLEConnected;
  static const WeaveStatus_SetupState kSetupState_PASESessionEstablished;
  static const WeaveStatus_SetupState kSetupState_CASESessionEstablished;

  // Tunnel state
  using WeaveStatus_TunnelState = std::string;
  static const WeaveStatus_TunnelState kTunnelState_NoTunnel;
  static const WeaveStatus_TunnelState kTunnelState_PrimaryTunMode;
  static const WeaveStatus_TunnelState kTunnelState_BkupOnlyTunMode;
  static const WeaveStatus_TunnelState kTunnelState_PrimaryAndBkupTunMode;

  // Tunnel type
  using WeaveStatus_TunnelType = std::string;
  static const WeaveStatus_TunnelType kTunnelType_None;
  static const WeaveStatus_TunnelType kTunnelType_Primary;
  static const WeaveStatus_TunnelType kTunnelType_Backup;
  static const WeaveStatus_TunnelType kTunnelType_Shortcut;

  // Creates/Returns the pointer to WeaveInspector singleton object.
  static WeaveInspector& GetWeaveInspector();
  WeaveInspector(WeaveInspector const&) = delete;
  void operator=(WeaveInspector const&) = delete;

  // Function to be called on WeaveStack Init.
  // This will record initialization status to inspect data.
  void NotifyInit();

  // Function to be called when pairing state change.
  // This will add a new entry with all current status to weave_status node.
  void NotifyPairingStateChange(const WeaveStatus_PairingState& new_state);

  // Function to be called when failsafe state change.
  // This will add a new entry with all current status to weave_status node.
  void NotifyFailSafeStateChange(const WeaveStatus_FailSafeState& new_state,
                                 const WeaveStatus_FailSafeReason& reason);

  // Function to be called when setup state change.
  // This will add a new entry with all current status to weave_status node.
  void NotifySetupStateChange(const WeaveStatus_SetupState& new_state);

  // Function to be called when tunnel state change.
  // This will add a new entry with all current status to weave_status node.
  void NotifyTunnelStateChange(const WeaveStatus_TunnelState& new_state,
                               const WeaveStatus_TunnelType& tunnel_type, bool is_restricted);

  friend class testing::WeaveInspectorTest;

 private:
  WeaveInspector();

  class WeaveStatusNode {
   public:
    explicit WeaveStatusNode(inspect::Node& parent_node);

    // This structure holds current Weave status at any point of time.
    struct WeaveCurrentStatus {
      WeaveCurrentStatus();
      bool tunnel_restricted_;
      WeaveStatus_TunnelState tunnel_state_;
      WeaveStatus_TunnelType tunnel_type_;
      WeaveStatus_FailSafeState failsafe_state_;
      WeaveStatus_FailSafeReason failsafe_state_reason_;
      WeaveStatus_PairingState pairing_state_;
      WeaveStatus_SetupState setup_state_;
    };

    // An entry in WeaveStatusNode. Each entry is timestamped.
    struct WeaveStatusEntry {
      WeaveStatusEntry(inspect::Node& parent_node, const WeaveCurrentStatus& current_status);

      inspect::Node weave_status_entry_node_;
      inspect::StringProperty setup_state_property_;
      inspect::StringProperty pairing_state_property_;

      struct {
        inspect::Node state_node_;
        inspect::StringProperty state_property_;
        inspect::StringProperty reason_property_;
      } failsafe_state_;

      struct {
        inspect::Node state_node_;
        inspect::StringProperty state_property_;
        inspect::StringProperty type_property_;
        inspect::BoolProperty is_restricted_property_;
      } tunnel_state_;

      inspect::UintProperty timestamp_property_;
    };

    // Add a new entry in weave_status with current status values.
    void LogCurrentStatus();

    // Return current weave status.
    WeaveCurrentStatus GetCurrentWeaveStatus() { return current_status_; }

    // Update current pairing state and add a new entry for weave status.
    void RecordPairingStateChange(const WeaveStatus_PairingState& new_state);

    // Update current failsafe state and add a new entry for weave status.
    void RecordFailSafeStateChange(const WeaveStatus_FailSafeState& new_state,
                                   const WeaveStatus_FailSafeReason& reason);

    // Update current setup state and add a new entry for weave status.
    void RecordSetupStateChange(const WeaveStatus_SetupState& new_state);

    // Update current tunnel state and add a new entry for weave status.
    void RecordTunnelStateChange(const WeaveStatus_TunnelState& new_state,
                                 const WeaveStatus_TunnelType& tunnel_type, bool is_restricted);

   private:
    inspect::Node node_;
    BoundedQueue<WeaveStatusEntry> weave_status_entries_;
    WeaveCurrentStatus current_status_;
  };

  // A Node which contains tunnel_status information.
  class TunnelStatusNode {
   public:
    explicit TunnelStatusNode(inspect::Node& parent_node);

    // Update last time when Tunnel established.
    void RecordTunnelEstablishedTime(uint64_t time_value);

    // Update last time when Tunnel went down.
    void RecordTunnelDownTime(uint64_t time_value);

   private:
    inspect::Node node_;
    inspect::UintProperty tunnel_established_time_property_;
    inspect::UintProperty tunnel_down_time_property_;
  };

  std::unique_ptr<sys::ComponentInspector> inspector_;
  TunnelStatusNode tunnel_status_;
  WeaveStatusNode weave_status_;
};

}  // namespace nl::Weave
#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_INSPECTOR_H_
