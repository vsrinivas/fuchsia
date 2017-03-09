// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include <magenta/compiler.h>

#include "apps/bluetooth/common/device_address.h"
#include "apps/bluetooth/common/uint128.h"
#include "apps/bluetooth/hci/hci_constants.h"

// This file contains general opcode/number and static packet definitions for
// the Bluetooth Host-Controller Interface. Each packet payload structure contains parameter
// descriptions based on their respective documentation in the Bluetooth Core Specification version
// 5.0

namespace bluetooth {
namespace hci {

// HCI opcode as used in command packets.
using OpCode = uint16_t;

// HCI event code as used in event packets.
using EventCode = uint8_t;

// Data Connection Handle used for ACL and SCO logical link connections.
using ConnectionHandle = uint16_t;

// Returns the OGF (OpCode Group Field) which occupies the upper 6-bits of the
// opcode.
inline uint8_t GetOGF(const OpCode opcode) {
  return opcode >> 10;
}

// Returns the OCF (OpCode Command Field) which occupies the lower 10-bits of
// the opcode.
inline uint16_t GetOCF(const OpCode opcode) {
  return opcode & 0x3FF;
}

// Returns the opcode based on the given OGF and OCF fields.
constexpr OpCode DefineOpCode(const uint8_t ogf, const uint16_t ocf) {
  return (static_cast<uint16_t>(ogf & 0x3F) << 10) | (ocf & 0x03FF);
}

// ========================= HCI packet headers ==========================
// NOTE(armansito): The definitions below are incomplete since they get added as
// needed. This list will grow as we support more features.

struct CommandHeader {
  uint16_t opcode;
  uint8_t parameter_total_size;
} __PACKED;

struct EventHeader {
  uint8_t event_code;
  uint8_t parameter_total_size;
} __PACKED;

struct ACLDataHeader {
  // The first 16-bits contain the following fields, in order:
  //   - 12-bits: Connection Handle
  //   - 2-bits: Packet Boundary Flags
  //   - 2-bits: Broadcast Flags
  uint16_t handle_and_flags;

  // Length of data following the header.
  uint16_t data_total_length;
} __PACKED;

// Generic return parameter struct for commands that only return a status. This
// can also be used to check the status of HCI commands with more complex return
// parameters.
struct SimpleReturnParams {
  // See enum Status in hci_constants.h.
  Status status;
} __PACKED;

// ============= HCI Command and Event (op)code and payloads =============

// No-Op
constexpr OpCode kNoOp = 0x0000;

// The following is a list of HCI command and event declarations sorted by OGF
// category. Within each category the commands are sorted by their OCF. Each
// declaration is preceded by the name of the command or event followed by the
// Bluetooth Core Specification version in which it was introduced. Commands
// that apply to a specific Bluetooth sub-technology
// (e.g. BR/EDR, LE, AMP) will also contain that definition.
//
// NOTE(armansito): This list is incomplete. Entries will be added as needed.
// TODO(armansito): Complete the HCI LE commands and events even if we don't
// use them right away..

// ======= Controller & Baseband Commands =======
// Core Spec v5.0 Vol 2, Part E, Section 7.3
constexpr uint8_t kControllerAndBasebandOGF = 0x03;
constexpr OpCode ControllerAndBasebandOpCode(const uint16_t ocf) {
  return DefineOpCode(kControllerAndBasebandOGF, ocf);
}

// ====================
// Reset Command (v1.1)
constexpr OpCode kReset = ControllerAndBasebandOpCode(0x0003);

// ========================================
// Write Local Name Command (v1.1) (BR/EDR)
constexpr OpCode kWriteLocalName = ControllerAndBasebandOpCode(0x0013);

struct WriteLocalNameCommandParams {
  // A UTF-8 encoded User Friendly Descriptive Name for the device. This can
  // contain up to 248 octets. If the name contained in the parameter is shorter
  // than 248 octets, the end of the name is indicated by a NULL octet (0x00),
  // and the following octets (to fill up 248 octets, which is the length of the
  // parameter) do not have valid values.
  uint8_t local_name[0];
} __PACKED;

// =======================================
// Read Local Name Command (v1.1) (BR/EDR)
constexpr OpCode kReadLocalName = ControllerAndBasebandOpCode(0x0014);

struct ReadLocalNameReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // A UTF-8 encoded User Friendly Descriptive Name for the device. This can
  // contain up to 248 octets. If the name contained in the parameter is shorter
  // than 248 octets, the end of the name is indicated by a NULL octet (0x00),
  // and the following octets (to fill up 248 octets, which is the length of the
  // parameter) do not have valid values.
  uint8_t local_name[0];
} __PACKED;

// ============================================
// Read Class of Device Command (v1.1) (BR/EDR)
constexpr OpCode kReadClassOfDevice = ControllerAndBasebandOpCode(0x0023);

struct ReadClassOfDeviceReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  uint8_t class_of_device[3];
} __PACKED;

// =============================================
// Write Class Of Device Command (v1.1) (BR/EDR)
constexpr OpCode kWriteClassOfDevice = ControllerAndBasebandOpCode(0x0024);

struct WriteClassOfDeviceCommandParams {
  uint8_t class_of_device[3];
} __PACKED;

// =========================================================
// Read Flow Control Mode Command (v3.0 + HS) (BR/EDR & AMP)
constexpr OpCode kReadFlowControlMode = ControllerAndBasebandOpCode(0x0066);

struct ReadFlowControlModeReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // See enum class FlowControlMode in hci_constants.h for possible values.
  uint8_t flow_control_mode;
} __PACKED;

// ==========================================================
// Write Flow Control Mode Command (v3.0 + HS) (BR/EDR & AMP)
constexpr OpCode kWriteFlowControlMode = ControllerAndBasebandOpCode(0x0067);

struct WriteFlowControlModeCommandParams {
  // See enum class FlowControlMode in hci_constants.h for possible values.
  uint8_t flow_control_mode;
} __PACKED;

// ======= Informational Parameters =======
// Core Spec v5.0 Vol 2, Part E, Section 7.4
constexpr uint8_t kInformationalParamsOGF = 0x04;
constexpr OpCode InformationalParamsOpCode(const uint16_t ocf) {
  return DefineOpCode(kInformationalParamsOGF, ocf);
}

// =============================================
// Read Local Version Information Command (v1.1)
constexpr OpCode kReadLocalVersionInfo = InformationalParamsOpCode(0x0001);

struct ReadLocalVersionInfoReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // HCI version (see enum class HCIVersion in hci_constants.h)
  HCIVersion hci_version;

  uint16_t hci_revision;
  uint8_t lmp_pal_version;
  uint16_t manufacturer_name;
  uint16_t lmp_pal_subversion;
} __PACKED;

// ============================================
// Read Local Supported Commands Command (v1.2)
constexpr OpCode kReadLocalSupportedCommands = InformationalParamsOpCode(0x0002);

struct ReadLocalSupportedCommandsReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // See enum class SupportedCommand in hci_constants.h for how to interpret this bitfield.
  uint8_t supported_commands[64];
} __PACKED;

// ============================================
// Read Local Supported Features Command (v1.1)
constexpr OpCode kReadLocalSupportedFeatures = InformationalParamsOpCode(0x0003);

struct ReadLocalSupportedFeaturesReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Bit Mask List of LMP features. For details see Core Spec v4.2, Volume 2,
  // Part C, Link Manager Protocol Specification.
  uint64_t lmp_features;
} __PACKED;

// ====================================================
// Read Local Extended Features Command (v1.2) (BR/EDR)
constexpr OpCode kReadLocalExtendedFeatures = InformationalParamsOpCode(0x0004);

struct ReadLocalExtendedFeaturesCommandParams {
  // - 0x00: Requests the normal LMP features as returned by
  //   Read_Local_Supported_Features.
  //
  // - 0x01-0xFF: Return the corresponding page of features.
  uint8_t page_number;
} __PACKED;

struct ReadLocalExtendedFeaturesReturnParams {
  // See enum Status in hci_constants.h.
  Status status;
  uint8_t page_number;
  uint8_t maximum_page_number;
  uint64_t extended_lmp_features;
} __PACKED;

// ===============================
// Read Buffer Size Command (v1.1)
constexpr OpCode kReadBufferSize = InformationalParamsOpCode(0x0005);

struct ReadBufferSizeReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  uint16_t hc_acl_data_packet_length;
  uint8_t hc_synchronous_data_packet_length;
  uint16_t hc_total_num_acl_data_packets;
  uint16_t hc_total_num_synchronous_data_packets;
} __PACKED;

// ========================================
// Read BD_ADDR Command (v1.1) (BR/EDR, LE)
constexpr OpCode kReadBDADDR = InformationalParamsOpCode(0x0009);

struct ReadBDADDRReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  common::DeviceAddress bd_addr;
} __PACKED;

// =======================================================
// Read Data Block Size Command (v3.0 + HS) (BR/EDR & AMP)
constexpr OpCode kReadDataBlockSize = InformationalParamsOpCode(0x000A);

struct ReadDataBlockSizeReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  uint16_t max_acl_data_packet_length;
  uint16_t data_block_length;
  uint16_t total_num_data_blocks;
} __PACKED;

// ======= Events =======
// Core Spec v5.0 Vol 2, Part E, Section 7.7

// =============================
// Command Complete Event (v1.1)
constexpr EventCode kCommandCompleteEventCode = 0x0E;

struct CommandCompleteEventParams {
  // The Number of HCI command packets which are allowed to be sent to the
  // Controller from the Host.
  uint8_t num_hci_command_packets;

  // Opcode of the command which caused this event.
  uint16_t command_opcode;

  // This is the return parameter(s) for the command specified in the
  // |command_opcode| event parameter. Refer to the Bluetooth Core Specification
  // v4.2, Vol 2, Part E for each command’s definition for the list of return
  // parameters associated with that command.
  uint8_t return_parameters[0];
} __PACKED;

// ===========================
// Command Status Event (v1.1)
constexpr EventCode kCommandStatusEventCode = 0x0F;
constexpr uint8_t kCommandStatusPending = 0x00;

struct CommandStatusEventParams {
  // See enum Status in hci_constants.h.
  Status status;

  // The Number of HCI command packets which are allowed to be sent to the
  // Controller from the Host.
  uint8_t num_hci_command_packets;

  // Opcode of the command which caused this event and is pending completion.
  uint16_t command_opcode;
} __PACKED;

// ===========================
// Hardware Error Event (v1.1)
constexpr EventCode kHardwareErrorEventCode = 0x10;

struct HardwareErrorEventParams {
  // These Hardware_Codes will be implementation-specific, and can be assigned
  // to indicate various hardware problems.
  uint8_t hardware_code;
} __PACKED;

// ========================================
// Number Of Completed Packets Event (v1.1)
constexpr EventCode kNumberOfCompletedPacketsEventCode = 0x13;

struct NumberOfCompletedPacketsEventData {
  uint16_t connection_handle;
  uint16_t hc_num_of_completed_packets;
} __PACKED;

struct NumberOfCompletedPacketsEventParams {
  uint8_t number_of_handles;
  NumberOfCompletedPacketsEventData data[0];
} __PACKED;

// =========================
// LE Meta Event (v4.0) (LE)
constexpr EventCode kLEMetaEventCode = 0x3E;

struct LEMetaEventParams {
  // The event code for the LE subevent.
  EventCode subevent_code;

  // Beginning of parameters that are specific to the LE subevent.
  uint8_t subevent_parameters[0];
} __PACKED;

// LE Advertising Report Event
constexpr EventCode kLEAdvertisingReportSubeventCode = 0x02;

struct LEAdvertisingReportData {
  // The event type.
  LEAdvertisingEventType event_type;

  // Type of |address| for the advertising device.
  LEAddressType address_type;

  // Public Device Address, Random Device Address, Public Identity Address or
  // Random (static) Identity Address of the advertising device.
  common::DeviceAddress address;

  // Length of the advertising data payload.
  uint8_t length_data;

  // The begining of |length_data| octets of advertising or scan response data
  // formatted as defined in Core Spec v5.0, Vol 3, Part C, Section 11.
  uint8_t data[0];

  // Immediately following |data| there is a single octet field containing the
  // received signal strength for this advertising report. Since |data| has a
  // variable length we do not declare it as a field within this struct.
  //
  //   Range: -127 <= N <= +20
  //   Units: dBm
  //   If N == 127: RSSI is not available.
  //
  // int8_t rssi;
} __PACKED;

struct LEAdvertisingReportSubeventParams {
  // Number of LEAdvertisingReportData instances contained in the array
  // |reports|.
  uint8_t num_reports;

  // Beginning of LEAdvertisingReportData array. Since each report data has a
  // variable length, the contents of |reports| this is declared as an array of
  // uint8_t.
  uint8_t reports[0];
} __PACKED;

// ================================================================
// Number Of Completed Data Blocks Event (v3.0 + HS) (BR/EDR & AMP)
constexpr EventCode kNumberOfCompletedDataBlocksEventCode = 0x48;

struct NumberOfCompletedDataBlocksEventData {
  // Handle (Connection Handle for a BR/EDR Controller or a Logical_Link_Handle
  // for an AMP Controller).
  uint16_t handle;
  uint16_t num_of_completed_packets;
  uint16_t num_of_completed_blocks;
} __PACKED;

struct NumberOfCompletedDataBlocksEventParams {
  uint16_t total_num_data_blocks;
  uint8_t number_of_handles;
  NumberOfCompletedDataBlocksEventData data[0];
} __PACKED;

// ======= LE Controller Commands =======
// Core Spec v5.0 Vol 2, Part E, Section 7.8
constexpr uint8_t kLEControllerCommandsOGF = 0x08;
constexpr OpCode LEControllerCommandOpCode(const uint16_t ocf) {
  return DefineOpCode(kLEControllerCommandsOGF, ocf);
}

// =====================================
// LE Set Event Mask Command (v4.0) (LE)
constexpr OpCode kLESetEventMask = LEControllerCommandOpCode(0x0001);

struct LESetEventMaskCommandParams {
  // See enum LEEventMask in hci_constants.h for possible values.
  uint64_t le_event_mask;
} __PACKED;

// =======================================
// LE Read Buffer Size Command (v4.0) (LE)
constexpr OpCode kLEReadBufferSize = LEControllerCommandOpCode(0x0002);

struct LEReadBufferSizeReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  uint16_t hc_le_acl_data_packet_length;
  uint8_t hc_total_num_le_acl_data_packets;
} __PACKED;

// ====================================================
// LE Read Local Supported Features Command (v4.0) (LE)
constexpr OpCode kLEReadLocalSupportedFeatures = LEControllerCommandOpCode(0x0003);

struct LEReadLocalSupportedFeaturesReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Bit Mask List of supported LE features. See enum class LEFeatures in
  // hci_constants.h.
  uint64_t le_features;
} __PACKED;

// =========================================
// LE Set Random Address Command (v4.0) (LE)
constexpr OpCode kLESetRandomAddress = LEControllerCommandOpCode(0x0005);

struct LESetRandomAddressCommandParams {
  common::DeviceAddress random_address;
} __PACKED;

// =================================================
// LE Set Advertising Parameters Command (v4.0) (LE)
constexpr OpCode kLESetAdvertisingParameters = LEControllerCommandOpCode(0x0006);

struct LESetAdvertisingParametersCommandParams {
  // Range: see kLEAdvertisingInterval[Min|Max] in hci_constants.h
  // Default: N = kLEAdvertisingIntervalDefault (see hci_constants.h)
  // Time: N * 0.625 ms
  // Time Range: 20 ms to 10.24 s
  uint16_t adv_interval_min;

  // Range: see kLEAdvertisingInterval[Min|Max] in hci_constants.h
  // Default: N = kLEAdvertisingIntervalDefault (see hci_constants.h)
  // Time: N * 0.625 ms
  // Time Range: 20 ms to 10.24 s
  uint16_t adv_interval_max;

  // Used to determine the packet type that is used for advertising when
  // advertising is enabled (see hci_constants.h)
  LEAdvertisingType adv_type;

  LEOwnAddressType own_address_type;
  LEPeerAddressType peer_address_type;

  // Public Device Address, Random Device Address, Public Identity Address, or
  // Random (static) Identity Address of the device to be connected.
  common::DeviceAddress peer_address;

  // (See the constants kLEAdvertisingChannel* in hci_constants.h for possible values).
  uint8_t adv_channel_map;

  // This parameter shall be ignored when directed advertising is enabled (see
  // hci_constants.h for possible values).
  LEAdvFilterPolicy adv_filter_policy;
} __PACKED;

// ========================================================
// LE Read Advertising Channel Tx Power Command (v4.0) (LE)
constexpr OpCode kLEReadAdvertisingChannelTxPower = LEControllerCommandOpCode(0x0007);

struct LEReadAdvertisingChannelTxPowerReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // The transmit power level used for LE advertising channel packets.
  //
  //   Range: -20 <= N <= +10
  //   Units: dBm
  //   Accuracy: +/- 4 dB
  int8_t tx_power;
} __PACKED;

// ===========================================
// LE Set Advertising Data Command (v4.0) (LE)
constexpr OpCode kLESetAdvertisingData = LEControllerCommandOpCode(0x0008);

struct LESetAdvertisingDataCommandParams {
  // The number of significant octets in |adv_data|.
  uint8_t adv_data_length;

  // 31 octets of advertising data formatted as defined in Core Spec v5.0, Vol
  // 3, Part C, Section 11.
  //
  // Default: All octets zero.
  uint8_t adv_data[kMaxLEAdvertisingDataLength];
} __PACKED;

// =============================================
// LE Set Scan Response Data Command (v4.0) (LE)
constexpr OpCode kLESetScanResponseData = LEControllerCommandOpCode(0x0009);

struct LESetScanResponseDataCommandParams {
  // The number of significant octets in |scan_rsp_data|.
  uint8_t scan_rsp_data_length;

  // 31 octets of Scan Response Data formatted as defined in Core Spec v5.0, Vol 3, Part C, Section
  // 11.
  //
  // Default: All octets zero.
  uint8_t scan_rsp_data[kMaxLEAdvertisingDataLength];
} __PACKED;

// =============================================
// LE Set Advertising Enable Command (v4.0) (LE)
constexpr OpCode kLESetAdvertisingEnable = LEControllerCommandOpCode(0x000A);

struct LESetAdvertisingEnableCommandParams {
  GenericEnableParam advertising_enable;
} __PACKED;

// ==========================================
// LE Set Scan Parameters Command (v4.0) (LE)
constexpr OpCode kLESetScanParameters = LEControllerCommandOpCode(0x000B);

struct LESetScanParametersCommandParams {
  // Controls the type of scan to perform.
  LEScanType scan_type;

  // Range: see kLEScanInterval[Min|Max] in hci_constants.h
  // Default: N = kLEScanIntervalDefault (see hci_constants.h)
  // Time: N * 0.625 ms
  // Time Range: 2.5 ms to 10.24 s
  uint16_t scan_interval;
  uint16_t scan_window;

  LEOwnAddressType own_address_type;
  LEScanFilterPolicy filter_policy;
} __PACKED;

// ======================================
// LE Set Scan Enable Command (v4.0) (LE)
constexpr OpCode kLESetScanEnable = LEControllerCommandOpCode(0x000C);

struct LESetScanEnableCommandParams {
  GenericEnableParam scanning_enabled;

  // (See Core Spec v5.0, Vol 6, Part B, Section 4.4.3.5)
  GenericEnableParam filter_duplicates;
} __PACKED;

// ========================================
// LE Create Connection Command (v4.0) (LE)
constexpr OpCode kLECreateConnection = LEControllerCommandOpCode(0x000D);

struct LECreateConnectionCommandParams {
  // Range: see kLEScanInterval[Min|Max] in hci_constants.h
  // Time: N * 0.625 ms
  // Time Range: 2.5 ms to 10.24 s
  uint16_t scan_interval;

  // Range: see kLEScanInterval[Min|Max] in hci_constants.h
  // Time: N * 0.625 ms
  // Time Range: 2.5 ms to 10.24 s
  uint16_t scan_window;

  GenericEnableParam initiator_filter_policy;
  LEAddressType peer_address_type;
  common::DeviceAddress peer_address;
  LEOwnAddressType own_address_type;

  // Range: see kLEConnectionInterval[Min|Max] in hci_constants.h
  // Time: N * 1.25 ms
  // Time Range: 7.5 ms to 4 s.
  uint16_t conn_interval_min;
  uint16_t conn_interval_max;

  // Range: 0x0000 to kLEConnectionLatencyMax in hci_constants.h
  uint16_t conn_latency;

  // Range: see kLEConnectionSupervisionTimeout[Min|Max] in hci_constants.h
  // Time: N * 10 ms
  // Time Range: 100 ms to 32 s
  uint16_t supervision_timeout;

  // Range: 0x0000 - 0xFFFF
  // Time: N * 0x625 ms
  uint16_t minimum_ce_length;
  uint16_t maximum_ce_length;
} __PACKED;

// NOTE on ReturnParams: No Command Complete event is sent by the Controller to indicate that this
// command has been completed. Instead, the LE Connection Complete or LE Enhanced Connection
// Complete event indicates that this command has been completed.

// ===============================================
// LE Create Connection Cancel Command (v4.0) (LE)
constexpr OpCode kLECreateConnectionCancel = LEControllerCommandOpCode(0x000E);

// ===========================================
// LE Read White List Size Command (v4.0) (LE)
constexpr OpCode kLEReadWhiteListSize = LEControllerCommandOpCode(0x000F);

struct LEReadWhiteListSizeReturnParams {
  // See enum Status in hci_constants.h.
  Status status;
  uint8_t white_list_size;
} __PACKED;

// =======================================
// LE Clear White List Command (v4.0) (LE)
constexpr OpCode kLEClearWhiteList = LEControllerCommandOpCode(0x0010);

// ===============================================
// LE Add Device To White List Command (v4.0) (LE)
constexpr OpCode kLEAddDeviceToWhiteList = LEControllerCommandOpCode(0x0011);

struct LEAddDeviceToWhiteListCommandParams {
  // The address type of the peer. The |address| parameter will be ignored if |address_type| is set
  // to LEPeerAddressType::kAnonymous.
  LEPeerAddressType address_type;

  // Public Device Address or Random Device Address of the device to be added to the White List.
  common::DeviceAddress address;
} __PACKED;

// ====================================================
// LE Remove Device From White List Command (v4.0) (LE)
constexpr OpCode kLERemoveDeviceFromWhiteList = LEControllerCommandOpCode(0x0012);

struct LERemoveDeviceFromWhiteListCommandParams {
  // The address type of the peer. The |address| parameter will be ignored if |address_type| is set
  // to LEPeerAddressType::kAnonymous.
  LEPeerAddressType address_type;

  // Public Device Address or Random Device Address of the device to be removed from the White List.
  common::DeviceAddress address;
} __PACKED;

// ========================================
// LE Connection Update Command (v4.0) (LE)
constexpr OpCode kLEConnectionUpdate = LEControllerCommandOpCode(0x0013);

struct LEConnectionUpdateCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;

  // Range: see kLEConnectionInterval[Min|Max] in hci_constants.h
  // Time: N * 1.25 ms
  // Time Range: 7.5 ms to 4 s.
  uint16_t conn_interval_min;
  uint16_t conn_interval_max;

  // Range: 0x0000 to kLEConnectionLatencyMax in hci_constants.h
  uint16_t conn_latency;

  // Range: see kLEConnectionSupervisionTimeout[Min|Max] in hci_constants.h
  // Time: N * 10 ms
  // Time Range: 100 ms to 32 s
  uint16_t supervision_timeout;

  // Range: 0x0000 - 0xFFFF
  // Time: N * 0x625 ms
  uint16_t minimum_ce_length;
  uint16_t maximum_ce_length;
} __PACKED;

// NOTE on Return Params: A Command Complete event is not sent by the Controller to indicate that
// this command has been completed. Instead, the LE Connection Update Complete event indicates that
// this command has been completed.

// ======================================================
// LE Set Host Channel Classification Command (v4.0) (LE)
constexpr OpCode kLESetHostChannelClassification = LEControllerCommandOpCode(0x0014);

struct LESetHostChannelClassificationCommandParams {
  // This parameter contains 37 1-bit fields (only the lower 37-bits of the 5-octet value are
  // meaningful).
  //
  // The nth such field (in the range 0 to 36) contains the value for the link layer channel index
  // n.
  //
  // Channel n is bad = 0. Channel n is unknown = 1.
  //
  // The most significant bits are reserved and shall be set to 0 for future use.
  //
  // At least one channel shall be marked as unknown.
  uint8_t channel_map[5];
} __PACKED;

// =======================================
// LE Read Channel Map Command (v4.0) (LE)
constexpr OpCode kLEReadChannelMap = LEControllerCommandOpCode(0x0015);

struct LEReadChannelMapCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;
} __PACKED;

struct LEReadChannelMapReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;

  // This parameter contains 37 1-bit fields (only the lower 37-bits of the 5-octet value are
  // meaningful).
  //
  // The nth such field (in the range 0 to 36) contains the value for the link layer channel index
  // n.
  //
  // Channel n is bad = 0. Channel n is unknown = 1.
  //
  // The most significant bits are reserved and shall be set to 0 for future use.
  //
  // At least one channel shall be marked as unknown.
  uint8_t channel_map[5];
} __PACKED;

// ===========================================
// LE Read Remote Features Command (v4.0) (LE)
constexpr OpCode kLEReadRemoteFeatures = LEControllerCommandOpCode(0x0016);

struct LEReadRemoteFeaturesCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;
} __PACKED;

// Note on ReturnParams: A Command Complete event is not sent by the Controller to indicate that
// this command has been completed. Instead, the LE Read Remote Features Complete event indicates
// that this command has been completed.

// ==============================
// LE Encrypt Command (v4.0) (LE)
constexpr OpCode kLEEncrypt = LEControllerCommandOpCode(0x0017);

struct LEEncryptCommandParams {
  // 128 bit key for the encryption of the data given in the command.
  common::UInt128 key;

  // 128 bit data block that is requested to be encrypted.
  uint8_t plaintext_data[16];
} __PACKED;

struct LEEncryptReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // 128 bit encrypted data block.
  uint8_t encrypted_data[16];
} __PACKED;

// ===========================
// LE Rand Command (v4.0) (LE)
constexpr OpCode kLERand = LEControllerCommandOpCode(0x0018);

struct LERandReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Random Number
  uint64_t random_number;
} __PACKED;

// =======================================
// LE Start Encryption Command (v4.0) (LE)
constexpr OpCode kLEStartEncryption = LEControllerCommandOpCode(0x0019);

// The parameters below are as defined in Core Spec v5.0, Vol 3, Part H, Section 2.4.4 "Encrypted
// Session Setup".
struct LEStartEncryptionCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;

  // 64-bit random number.
  uint64_t random_number;

  // 16-bit encrypted diversifier.
  uint16_t encrypted_diversifier;

  // 128-bit long-term key (LTK).
  common::UInt128 long_term_key;
} __PACKED;

// NOTE on Return Params: A Command Complete event is not sent by the Controller to indicate that
// this command has been completed. Instead, the Encryption Change or Encryption Key Refresh
// Complete events indicate that this command has been completed.

// ==================================================
// LE Long Term Key Request Reply Command (v4.0) (LE)
constexpr OpCode kLELongTermKeyRequestReply = LEControllerCommandOpCode(0x001A);

struct LELongTermKeyRequestReplyCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;

  // 128-bit long term key for the current connection.
  common::UInt128 long_term_key;
} __PACKED;

struct LELongTermKeyRequestReplyReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;
} __PACKED;

// ===========================================================
// LE Long Term Key Request Negative Reply Command (v4.0) (LE)
constexpr OpCode kLELongTermKeyRequestNegativeReply = LEControllerCommandOpCode(0x001B);

struct LELongTermKeyRequestNegativeReplyCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;
} __PACKED;

struct LELongTermKeyRequestNegativeReplyReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;
} __PACKED;

// ============================================
// LE Read Supported States Command (v4.0) (LE)
constexpr OpCode kLEReadSupportedStates = LEControllerCommandOpCode(0x001C);

struct LEReadSupportedStatesReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Bit-mask of supported state or state combinations. See Core Spec v4.2,
  // Volume 2, Part E, Section 7.8.27 "LE Read Supported States Command".
  uint64_t le_states;
} __PACKED;

// ====================================
// LE Receiver Test Command (v4.0) (LE)
constexpr OpCode kLEReceiverTest = LEControllerCommandOpCode(0x001D);

struct LEReceiverTestCommandParams {
  // N = (F - 2402) / 2
  // Range: 0x00 - 0x27. Frequency Range : 2402 MHz to 2480 MHz.
  uint8_t rx_channel;
} __PACKED;

// ======================================
// LE Transmitter Test Comand (v4.0) (LE)
constexpr OpCode kLETransmitterTest = LEControllerCommandOpCode(0x001E);

struct LETransmitterTestCommandParams {
  // N = (F - 2402) / 2
  // Range: 0x00 - 0x27. Frequency Range : 2402 MHz to 2480 MHz.
  uint8_t tx_channel;

  // Length in bytes of payload data in each packet
  uint8_t length_of_test_data;

  // The packet payload sequence. See Core Spec 5.0, Vol 2, Part E, Section 7.8.29 for a description
  // of possible values.
  uint8_t packet_payload;
} __PACKED;

// ===============================
// LE Test End Command (v4.0) (LE)
constexpr OpCode kLETestEnd = LEControllerCommandOpCode(0x001F);

struct LETestEndReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Number of packets received
  uint16_t number_of_packets;
} __PACKED;

// ================================================================
// LE Remote Connection Parameter Request Reply Command (v4.1) (LE)
constexpr OpCode kLERemoteConnectionParameterRequestReply = LEControllerCommandOpCode(0x0020);

struct LERemoteConnectionParameterRequestReplyCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;

  // Range: see kLEConnectionInterval[Min|Max] in hci_constants.h
  // Time: N * 1.25 ms
  // Time Range: 7.5 ms to 4 s.
  uint16_t conn_interval_min;
  uint16_t conn_interval_max;

  // Range: 0x0000 to kLEConnectionLatencyMax in hci_constants.h
  uint16_t conn_latency;

  // Range: see kLEConnectionSupervisionTimeout[Min|Max] in hci_constants.h
  // Time: N * 10 ms
  // Time Range: 100 ms to 32 s
  uint16_t supervision_timeout;

  // Range: 0x0000 - 0xFFFF
  // Time: N * 0x625 ms
  uint16_t minimum_ce_length;
  uint16_t maximum_ce_length;
} __PACKED;

struct LERemoteConnectionParameterRequestReplyReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;
} __PACKED;

// =========================================================================
// LE Remote Connection Parameter Request Negative Reply Command (v4.1) (LE)
constexpr OpCode kLERemoteConnectionParameterRequestNegativeReply =
    LEControllerCommandOpCode(0x0021);

struct LERemoteConnectionParamReqNegativeReplyCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;

  // Reason that the connection parameter request was rejected.
  Status reason;
} __PACKED;

struct LERemoteConnectionParamReqNegativeReplyReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;
} __PACKED;

// ======================================
// LE Set Data Length Command (v4.2) (LE)
constexpr OpCode kLESetDataLength = LEControllerCommandOpCode(0x0022);

struct LESetDataLengthCommandParams {
  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;

  // Range: see kLEMaxTxOctets[Min|Max] in hci_constants.h
  uint16_t tx_octets;

  // Range: see kLEMaxTxTime[Min|Max] in hci_constants.h
  uint16_t tx_time;
} __PACKED;

struct LESetDataLengthReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Connection Handle (only the lower 12-bits are meaningful).
  //
  //   Range: 0x0000 to kConnectioHandleMax in hci_constants.h
  ConnectionHandle connection_handle;
} __PACKED;

// =========================================================
// LE Read Suggested Default Data Length Command (v4.2) (LE)
constexpr OpCode kLEReadSuggestedDefaultDataLength = LEControllerCommandOpCode(0x0023);

struct LEReadSuggestedDefaultDataLengthReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Range: see kLEMaxTxOctets[Min|Max] in hci_constants.h
  uint16_t suggested_max_tx_octets;

  // Range: see kLEMaxTxTime[Min|Max] in hci_constants.h
  uint16_t suggested_max_tx_time;
} __PACKED;

// ==========================================================
// LE Write Suggested Default Data Length Command (v4.2) (LE)
constexpr OpCode kLEWriteSuggestedDefaultDataLength = LEControllerCommandOpCode(0x0024);

struct LEWriteSuggestedDefaultDataLengthCommandParams {
  // Range: see kLEMaxTxOctets[Min|Max] in hci_constants.h
  uint16_t suggested_max_tx_octets;

  // Range: see kLEMaxTxTime[Min|Max] in hci_constants.h
  uint16_t suggested_max_tx_time;
} __PACKED;

// ==================================================
// LE Read Local P-256 Public Key Command (v4.2) (LE)
constexpr OpCode kLEReadLocalP256PublicKey = LEControllerCommandOpCode(0x0025);

// NOTE on ReturnParams: When the Controller receives the LE_Read_Local_P-256_Public_Key command,
// the Controller shall send the Command Status event to the Host. When the local P-256 public key
// generation finishes, an LE Read Local P-256 Public Key Complete event shall be generated.
//
// No Command Complete event is sent by the Controller to indicate that this command has been
// completed.

// ======================================
// LE Generate DH Key Command (v4.2) (LE)
constexpr OpCode kLEGenerateDHKey = LEControllerCommandOpCode(0x0026);

struct LEGenerateDHKeyCommandParams {
  // The remote P-256 public key:
  //   X, Y format
  //   Octets 31-0: X co-ordinate
  //   Octets 63-32: Y co-ordinate Little Endian Format
  uint8_t remote_p256_public_key[64];
} __PACKED;

// NOTE on ReturnParams: When the Controller receives the LE_Generate_DHKey command, the Controller
// shall send the Command Status event to the Host. When the DHKey generation finishes, an LE DHKey
// Generation Complete event shall be generated.
//
// No Command Complete event is sent by the Controller to indicate that this command has been
// completed.

// ===================================================
// LE Add Device To Resolving List Command (v4.2) (LE)
constexpr OpCode kLEAddDeviceToResolvingList = LEControllerCommandOpCode(0x0027);

struct LEAddDeviceToResolvingListCommandParams {
  // The peer device's identity address type.
  LEPeerAddressType peer_identity_address_type;

  // Public or Random (static) Identity address of the peer device
  common::DeviceAddress peer_identity_address;

  // IRK (Identity Resolving Key) of the peer device
  common::UInt128 peer_irk;

  // IRK (Identity Resolving Key) of the local device
  common::UInt128 local_irk;
} __PACKED;

// ========================================================
// LE Remove Device From Resolving List Command (v4.2) (LE)
constexpr OpCode kLERemoveDeviceFromResolvingList = LEControllerCommandOpCode(0x0028);

struct LERemoveDeviceFromResolvingListCommandParams {
  // The peer device's identity address type.
  LEPeerAddressType peer_identity_address_type;

  // Public or Random (static) Identity address of the peer device
  common::DeviceAddress peer_identity_address;
} __PACKED;

// ===========================================
// LE Clear Resolving List Command (v4.2) (LE)
constexpr OpCode kLEClearResolvingList = LEControllerCommandOpCode(0x0029);

// ===============================================
// LE Read Resolving List Size Command (v4.2) (LE)
constexpr OpCode kLEReadResolvingListSize = LEControllerCommandOpCode(0x002A);

struct LEReadResolvingListReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Number of address translation entries in the resolving list.
  uint8_t resolving_list_size;
} __PACKED;

// ===================================================
// LE Read Peer Resolvable Address Command (v4.2) (LE)
constexpr OpCode kLEReadPeerResolvableAddress = LEControllerCommandOpCode(0x002B);

struct LEReadPeerResolvableAddressCommandParams {
  // The peer device's identity address type.
  LEPeerAddressType peer_identity_address_type;

  // Public or Random (static) Identity address of the peer device.
  common::DeviceAddress peer_identity_address;
} __PACKED;

struct LEReadPeerResolvableAddressReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Resolvable Private Address being used by the peer device.
  common::DeviceAddress peer_resolvable_address;
} __PACKED;

// ====================================================
// LE Read Local Resolvable Address Command (v4.2) (LE)
constexpr OpCode kLEReadLocalResolvableAddress = LEControllerCommandOpCode(0x002C);

struct LEReadLocalResolvableAddressCommandParams {
  // The peer device's identity address type.
  LEPeerAddressType peer_identity_address_type;

  // Public or Random (static) Identity address of the peer device
  common::DeviceAddress peer_identity_address;
} __PACKED;

struct LEReadLocalResolvableAddressReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Resolvable Private Address being used by the local device.
  common::DeviceAddress local_resolvable_address;
} __PACKED;

// ====================================================
// LE Set Address Resolution Enable Command (v4.2) (LE)
constexpr OpCode kLESetAddressResolutionEnable = LEControllerCommandOpCode(0x002D);

struct LESetAddressResolutionEnableCommandParams {
  GenericEnableParam address_resolution_enable;
} __PACKED;

// =============================================================
// LE Set Resolvable Private Address Timeout Command (v4.2) (LE)
constexpr OpCode kLESetResolvablePrivateAddressTimeout = LEControllerCommandOpCode(0x002E);

struct LESetResolvablePrivateAddressTimeoutCommandParams {
  // Range: See kLERPATimeout[Min|Max] in hci_constants.h
  // Default: See kLERPATimeoutDefault in hci_constants.h
  uint16_t rpa_timeout;
} __PACKED;

// ===============================================
// LE Read Maximum Data Length Command (v4.2) (LE)
constexpr OpCode kLEReadMaximumDataLength = LEControllerCommandOpCode(0x002F);

struct LEReadMaximumDataLengthReturnParams {
  // See enum Status in hci_constants.h.
  Status status;

  // Range: see kLEMaxTxOctets[Min|Max] in hci_constants.h
  uint16_t supported_max_tx_octets;

  // Range: see kLEMaxTxTime[Min|Max] in hci_constants.h
  uint16_t supported_max_tx_time;

  // Range: see kLEMaxTxOctets[Min|Max] in hci_constants.h
  uint16_t supported_max_rx_octets;

  // Range: see kLEMaxTxTime[Min|Max] in hci_constants.h
  uint16_t supported_max_rx_time;
} __PACKED;

}  // namespace hci
}  // namespace bluetooth
