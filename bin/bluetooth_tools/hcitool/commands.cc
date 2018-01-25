// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <endian.h>

#include <cstring>
#include <iostream>

#include "garnet/drivers/bluetooth/lib/common/manufacturer_names.h"
#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/hci/advertising_report_parser.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/time_delta.h"

using namespace bluetooth;

using std::placeholders::_1;
using std::placeholders::_2;

namespace hcitool {
namespace {

void StatusCallback(fxl::Closure complete_cb,
                    ::btlib::hci::CommandChannel::TransactionId id,
                    ::btlib::hci::Status status) {
  std::cout << "  Command Status: " << fxl::StringPrintf("0x%02x", status)
            << " (id=" << id << ")" << std::endl;
  if (status != ::btlib::hci::Status::kSuccess)
    complete_cb();
}

::btlib::hci::CommandChannel::TransactionId SendCommand(
    const CommandData* cmd_data,
    std::unique_ptr<::btlib::hci::CommandPacket> packet,
    const ::btlib::hci::CommandChannel::CommandCompleteCallback& cb,
    const fxl::Closure& complete_cb) {
  return cmd_data->cmd_channel()->SendCommand(
      std::move(packet), cmd_data->task_runner(), cb,
      std::bind(&StatusCallback, complete_cb, _1, _2));
}

void LogCommandComplete(::btlib::hci::Status status,
                        ::btlib::hci::CommandChannel::TransactionId id) {
  std::cout << "  Command Complete - status: "
            << fxl::StringPrintf("0x%02x", status) << " (id=" << id << ")"
            << std::endl;
}

::btlib::hci::CommandChannel::TransactionId SendCompleteCommand(
    const CommandData* cmd_data,
    std::unique_ptr<::btlib::hci::CommandPacket> packet,
    const fxl::Closure& complete_cb) {
  auto cb = [complete_cb](::btlib::hci::CommandChannel::TransactionId id,
                          const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::SimpleReturnParams>();
    LogCommandComplete(return_params->status, id);
    complete_cb();
  };
  return SendCommand(cmd_data, std::move(packet), cb, complete_cb);
}

// TODO(armansito): Move this to a library header as it will be useful
// elsewhere.
std::string AdvEventTypeToString(::btlib::hci::LEAdvertisingEventType type) {
  switch (type) {
    case ::btlib::hci::LEAdvertisingEventType::kAdvInd:
      return "ADV_IND";
    case ::btlib::hci::LEAdvertisingEventType::kAdvDirectInd:
      return "ADV_DIRECT_IND";
    case ::btlib::hci::LEAdvertisingEventType::kAdvScanInd:
      return "ADV_SCAN_IND";
    case ::btlib::hci::LEAdvertisingEventType::kAdvNonConnInd:
      return "ADV_NONCONN_IND";
    case ::btlib::hci::LEAdvertisingEventType::kScanRsp:
      return "SCAN_RSP";
    default:
      break;
  }
  return "(unknown)";
}

// TODO(armansito): Move this to a library header as it will be useful
// elsewhere.
std::string BdAddrTypeToString(::btlib::hci::LEAddressType type) {
  switch (type) {
    case ::btlib::hci::LEAddressType::kPublic:
      return "public";
    case ::btlib::hci::LEAddressType::kRandom:
      return "random";
    case ::btlib::hci::LEAddressType::kPublicIdentity:
      return "public-identity (resolved private)";
    case ::btlib::hci::LEAddressType::kRandomIdentity:
      return "random-identity (resolved private)";
    default:
      break;
  }
  return "(unknown)";
}

// TODO(armansito): Move this to a library header as it will be useful
// elsewhere.
std::vector<std::string> AdvFlagsToStrings(uint8_t flags) {
  std::vector<std::string> flags_list;
  if (flags & ::btlib::gap::AdvFlag::kLELimitedDiscoverableMode)
    flags_list.push_back("limited-discoverable");
  if (flags & ::btlib::gap::AdvFlag::kLEGeneralDiscoverableMode)
    flags_list.push_back("general-discoverable");
  if (flags & ::btlib::gap::AdvFlag::kBREDRNotSupported)
    flags_list.push_back("bredr-not-supported");
  if (flags & ::btlib::gap::AdvFlag::kSimultaneousLEAndBREDRController)
    flags_list.push_back("le-and-bredr-controller");
  if (flags & ::btlib::gap::AdvFlag::kSimultaneousLEAndBREDRHost)
    flags_list.push_back("le-and-bredr-host");
  return flags_list;
}

void DisplayAdvertisingReport(const ::btlib::hci::LEAdvertisingReportData& data,
                              int8_t rssi,
                              const std::string& name_filter,
                              const std::string& addr_type_filter) {
  ::btlib::gap::AdvertisingDataReader reader(
      ::btlib::common::BufferView(data.data, data.length_data));

  // The AD fields that we'll parse out.
  uint8_t flags = 0;
  fxl::StringView short_name, complete_name;
  int8_t tx_power_lvl;
  bool tx_power_present = false;

  ::btlib::gap::DataType type;
  ::btlib::common::BufferView adv_data_field;
  while (reader.GetNextField(&type, &adv_data_field)) {
    switch (type) {
      case ::btlib::gap::DataType::kFlags:
        flags = adv_data_field.data()[0];
        break;
      case ::btlib::gap::DataType::kCompleteLocalName:
        complete_name = adv_data_field.AsString();
        break;
      case ::btlib::gap::DataType::kShortenedLocalName:
        short_name = adv_data_field.AsString();
        break;
      case ::btlib::gap::DataType::kTxPowerLevel:
        tx_power_present = true;
        tx_power_lvl = adv_data_field.data()[0];
        break;
      default:
        break;
    }
  }

  // First check if this report should be filtered out by name.
  if (!name_filter.empty() && complete_name.compare(name_filter) != 0 &&
      short_name.compare(name_filter) != 0) {
    return;
  }

  // Apply the address type filter.
  if (!addr_type_filter.empty()) {
    FXL_DCHECK(addr_type_filter == "public" || addr_type_filter == "random");
    if (addr_type_filter == "public" &&
        data.address_type != ::btlib::hci::LEAddressType::kPublic &&
        data.address_type != ::btlib::hci::LEAddressType::kPublicIdentity)
      return;
    if (addr_type_filter == "random" &&
        data.address_type != ::btlib::hci::LEAddressType::kRandom &&
        data.address_type != ::btlib::hci::LEAddressType::kRandomIdentity)
      return;
  }

  std::cout << "  LE Advertising Report:" << std::endl;
  std::cout << "    RSSI: " << fxl::NumberToString(rssi) << std::endl;
  std::cout << "    type: " << AdvEventTypeToString(data.event_type)
            << std::endl;
  std::cout << "    address type: " << BdAddrTypeToString(data.address_type)
            << std::endl;
  std::cout << "    BD_ADDR: " << data.address.ToString() << std::endl;
  std::cout << "    Data Length: " << fxl::NumberToString(data.length_data)
            << " bytes" << std::endl;
  if (flags) {
    std::cout << "    Flags: ["
              << fxl::JoinStrings(AdvFlagsToStrings(flags), ", ") << "]"
              << std::endl;
  }
  if (!short_name.empty())
    std::cout << "    Shortened Local Name: " << short_name << std::endl;
  if (!complete_name.empty())
    std::cout << "    Complete Local Name: " << complete_name << std::endl;
  if (tx_power_present) {
    std::cout << "    Tx Power Level: " << fxl::NumberToString(tx_power_lvl)
              << std::endl;
  }
}

bool HandleVersionInfo(const CommandData* cmd_data,
                       const fxl::CommandLine& cmd_line,
                       const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: version-info" << std::endl;
    return false;
  }

  auto cb = [complete_cb](::btlib::hci::CommandChannel::TransactionId id,
                          const ::btlib::hci::EventPacket& event) {
    auto params =
        event.return_params<::btlib::hci::ReadLocalVersionInfoReturnParams>();
    LogCommandComplete(params->status, id);
    if (params->status != ::btlib::hci::Status::kSuccess) {
      complete_cb();
      return;
    }

    std::cout << "  Version Info:" << std::endl;
    std::cout << "    HCI Version: Core Spec "
              << ::btlib::hci::HCIVersionToString(params->hci_version)
              << std::endl;
    std::cout << "    Manufacturer Name: "
              << ::btlib::common::GetManufacturerName(
                     le16toh(params->manufacturer_name))
              << std::endl;

    complete_cb();
  };

  auto packet =
      ::btlib::hci::CommandPacket::New(::btlib::hci::kReadLocalVersionInfo);
  auto id = SendCommand(cmd_data, std::move(packet), cb, complete_cb);

  std::cout << "  Sent HCI_Read_Local_Version_Information (id=" << id << ")"
            << std::endl;
  return true;
}

bool HandleReset(const CommandData* cmd_data,
                 const fxl::CommandLine& cmd_line,
                 const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: reset" << std::endl;
    return false;
  }

  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kReset);
  auto id = SendCompleteCommand(cmd_data, std::move(packet), complete_cb);

  std::cout << "  Sent HCI_Reset (id=" << id << ")" << std::endl;

  return true;
}

bool HandleReadBDADDR(const CommandData* cmd_data,
                      const fxl::CommandLine& cmd_line,
                      const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-bdaddr" << std::endl;
    return false;
  }

  auto cb = [complete_cb](::btlib::hci::CommandChannel::TransactionId id,
                          const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::ReadBDADDRReturnParams>();
    LogCommandComplete(return_params->status, id);
    if (return_params->status != ::btlib::hci::Status::kSuccess) {
      complete_cb();
      return;
    }

    std::cout << "  BD_ADDR: " << return_params->bd_addr.ToString()
              << std::endl;
    complete_cb();
  };

  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kReadBDADDR);
  auto id = SendCommand(cmd_data, std::move(packet), cb, complete_cb);

  std::cout << "  Sent HCI_Read_BDADDR (id=" << id << ")" << std::endl;

  return true;
}

bool HandleReadLocalName(const CommandData* cmd_data,
                         const fxl::CommandLine& cmd_line,
                         const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-local-name" << std::endl;
    return false;
  }

  auto cb = [complete_cb](::btlib::hci::CommandChannel::TransactionId id,
                          const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::ReadLocalNameReturnParams>();
    LogCommandComplete(return_params->status, id);
    if (return_params->status != ::btlib::hci::Status::kSuccess) {
      complete_cb();
      return;
    }

    std::cout << "  Local Name: " << return_params->local_name << std::endl;

    complete_cb();
  };

  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kReadLocalName);
  auto id = SendCommand(cmd_data, std::move(packet), cb, complete_cb);
  std::cout << "  Sent HCI_Read_Local_Name (id=" << id << ")" << std::endl;

  return true;
}

bool HandleWriteLocalName(const CommandData* cmd_data,
                          const fxl::CommandLine& cmd_line,
                          const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() != 1 || cmd_line.options().size()) {
    std::cout << "  Usage: write-local-name <name>" << std::endl;
    return false;
  }

  const std::string& name = cmd_line.positional_args()[0];
  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kWriteLocalName,
                                                 name.length() + 1);
  std::strcpy((char*)packet->mutable_view()
                  ->mutable_payload<::btlib::hci::WriteLocalNameCommandParams>()
                  ->local_name,
              name.c_str());

  auto id = SendCompleteCommand(cmd_data, std::move(packet), complete_cb);
  std::cout << "  Sent HCI_Write_Local_Name (id=" << id << ")" << std::endl;

  return true;
}

bool HandleSetEventMask(const CommandData* cmd_data,
                        const fxl::CommandLine& cmd_line,
                        const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() != 1 || cmd_line.options().size()) {
    std::cout << "  Usage: set-event-mask [hex]" << std::endl;
    return false;
  }

  std::string hex = cmd_line.positional_args()[0];
  if (hex.size() >= 2 && hex[0] == '0' && hex[1] == 'x')
    hex = hex.substr(2);

  uint64_t mask;
  if (!fxl::StringToNumberWithError<uint64_t>(hex, &mask, fxl::Base::k16)) {
    std::cout << "  Unrecognized hex number: " << cmd_line.positional_args()[0]
              << std::endl;
    std::cout << "  Usage: set-event-mask [hex]" << std::endl;
    return false;
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::SetEventMaskCommandParams);
  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kSetEventMask,
                                                 kPayloadSize);
  packet->mutable_view()
      ->mutable_payload<::btlib::hci::SetEventMaskCommandParams>()
      ->event_mask = htole64(mask);

  auto id = SendCompleteCommand(cmd_data, std::move(packet), complete_cb);

  std::cout << "  Sent HCI_Set_Event_Mask("
            << fxl::NumberToString(mask, fxl::Base::k16) << ") (id=" << id
            << ")" << std::endl;
  return true;
}

bool HandleLESetAdvEnable(const CommandData* cmd_data,
                          const fxl::CommandLine& cmd_line,
                          const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size() != 1 || cmd_line.options().size()) {
    std::cout << "  Usage: set-adv-enable [enable|disable]" << std::endl;
    return false;
  }

  ::btlib::hci::GenericEnableParam value;
  std::string cmd_arg = cmd_line.positional_args()[0];
  if (cmd_arg == "enable") {
    value = ::btlib::hci::GenericEnableParam::kEnable;
  } else if (cmd_arg == "disable") {
    value = ::btlib::hci::GenericEnableParam::kDisable;
  } else {
    std::cout << "  Unrecognized parameter: " << cmd_arg << std::endl;
    std::cout << "  Usage: set-adv-enable [enable|disable]" << std::endl;
    return false;
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::LESetAdvertisingEnableCommandParams);

  auto packet = ::btlib::hci::CommandPacket::New(
      ::btlib::hci::kLESetAdvertisingEnable, kPayloadSize);
  packet->mutable_view()
      ->mutable_payload<::btlib::hci::LESetAdvertisingEnableCommandParams>()
      ->advertising_enable = value;

  auto id = SendCompleteCommand(cmd_data, std::move(packet), complete_cb);

  std::cout << "  Sent HCI_LE_Set_Advertising_Enable (id=" << id << ")"
            << std::endl;
  return true;
}

bool HandleLESetAdvParams(const CommandData* cmd_data,
                          const fxl::CommandLine& cmd_line,
                          const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout << "  Usage: set-adv-params [--help|--type]" << std::endl;
    return false;
  }

  if (cmd_line.HasOption("help")) {
    std::cout
        << "  Options: \n"
           "    --help - Display this help message\n"
           "    --type=<type> - The advertising type. Possible values are:\n"
           "          - nonconn: non-connectable undirected (default)\n"
           "          - adv-ind: connectable and scannable undirected\n"
           "          - direct-low: connectable directed low-duty\n"
           "          - direct-high: connectable directed high-duty\n"
           "          - scan: scannable undirected";
    std::cout << std::endl;
    return false;
  }

  ::btlib::hci::LEAdvertisingType adv_type =
      ::btlib::hci::LEAdvertisingType::kAdvNonConnInd;
  std::string type;
  if (cmd_line.GetOptionValue("type", &type)) {
    if (type == "adv-ind") {
      adv_type = ::btlib::hci::LEAdvertisingType::kAdvInd;
    } else if (type == "direct-low") {
      adv_type = ::btlib::hci::LEAdvertisingType::kAdvDirectIndLowDutyCycle;
    } else if (type == "direct-high") {
      adv_type = ::btlib::hci::LEAdvertisingType::kAdvDirectIndHighDutyCycle;
    } else if (type == "scan") {
      adv_type = ::btlib::hci::LEAdvertisingType::kAdvScanInd;
    } else if (type == "nonconn") {
      adv_type = ::btlib::hci::LEAdvertisingType::kAdvNonConnInd;
    } else {
      std::cout << "  Unrecognized advertising type: " << type << std::endl;
      return false;
    }
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::LESetAdvertisingParametersCommandParams);
  auto packet = ::btlib::hci::CommandPacket::New(
      ::btlib::hci::kLESetAdvertisingParameters, kPayloadSize);
  auto params =
      packet->mutable_view()
          ->mutable_payload<
              ::btlib::hci::LESetAdvertisingParametersCommandParams>();
  params->adv_interval_min =
      htole16(::btlib::hci::kLEAdvertisingIntervalDefault);
  params->adv_interval_max =
      htole16(::btlib::hci::kLEAdvertisingIntervalDefault);
  params->adv_type = adv_type;
  params->own_address_type = ::btlib::hci::LEOwnAddressType::kPublic;
  params->peer_address_type = ::btlib::hci::LEPeerAddressType::kPublic;
  params->peer_address.SetToZero();
  params->adv_channel_map = ::btlib::hci::kLEAdvertisingChannelAll;
  params->adv_filter_policy = ::btlib::hci::LEAdvFilterPolicy::kAllowAll;

  auto id = SendCompleteCommand(cmd_data, std::move(packet), complete_cb);

  std::cout << "  Sent HCI_LE_Set_Advertising_Parameters (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleLESetAdvData(const CommandData* cmd_data,
                        const fxl::CommandLine& cmd_line,
                        const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout << "  Usage: set-adv-data [--help|--name]" << std::endl;
    return false;
  }

  if (cmd_line.HasOption("help")) {
    std::cout
        << "  Options: \n"
           "    --help - Display this help message\n"
           "    --name=<local-name> - Set the \"Complete Local Name\" field";
    std::cout << std::endl;
    return false;
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::LESetAdvertisingDataCommandParams);
  auto packet = ::btlib::hci::CommandPacket::New(
      ::btlib::hci::kLESetAdvertisingData, kPayloadSize);
  packet->mutable_view()->mutable_payload_data().SetToZeros();

  std::string name;
  if (cmd_line.GetOptionValue("name", &name)) {
    // Each advertising data structure consists of a 1 octet length field, 1
    // octet type field.
    size_t adv_data_len = 2 + name.length();
    if (adv_data_len > ::btlib::hci::kMaxLEAdvertisingDataLength) {
      std::cout << "  Given name is too long" << std::endl;
      return false;
    }

    auto params = packet->mutable_view()
                      ->mutable_payload<
                          ::btlib::hci::LESetAdvertisingDataCommandParams>();
    params->adv_data_length = adv_data_len;
    params->adv_data[0] = adv_data_len - 1;
    params->adv_data[1] = 0x09;  // Complete Local Name
    std::strncpy((char*)params->adv_data + 2, name.c_str(), name.length());
  } else {
    packet->mutable_view()
        ->mutable_payload<::btlib::hci::LESetAdvertisingDataCommandParams>()
        ->adv_data_length = 0;
  }

  auto id = SendCompleteCommand(cmd_data, std::move(packet), complete_cb);

  std::cout << "  Sent HCI_LE_Set_Advertising_Data (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleLESetScanParams(const CommandData* cmd_data,
                           const fxl::CommandLine& cmd_line,
                           const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout << "  Usage: set-scan-params [--help|--type]" << std::endl;
    return false;
  }

  if (cmd_line.HasOption("help")) {
    std::cout << "  Options: \n"
                 "    --help - Display this help message\n"
                 "    --type=<type> - The scan type. Possible values are:\n"
                 "          - passive: passive scanning (default)\n"
                 "          - active: active scanning; sends scan requests";
    std::cout << std::endl;
    return false;
  }

  ::btlib::hci::LEScanType scan_type = ::btlib::hci::LEScanType::kPassive;
  std::string type;
  if (cmd_line.GetOptionValue("type", &type)) {
    if (type == "passive") {
      scan_type = ::btlib::hci::LEScanType::kPassive;
    } else if (type == "active") {
      scan_type = ::btlib::hci::LEScanType::kActive;
    } else {
      std::cout << "  Unrecognized scan type: " << type << std::endl;
      return false;
    }
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::LESetScanParametersCommandParams);
  auto packet = ::btlib::hci::CommandPacket::New(
      ::btlib::hci::kLESetScanParameters, kPayloadSize);

  auto params =
      packet->mutable_view()
          ->mutable_payload<::btlib::hci::LESetScanParametersCommandParams>();
  params->scan_type = scan_type;
  params->scan_interval = htole16(::btlib::hci::kLEScanIntervalDefault);
  params->scan_window = htole16(::btlib::hci::kLEScanIntervalDefault);
  params->own_address_type = ::btlib::hci::LEOwnAddressType::kPublic;
  params->filter_policy = ::btlib::hci::LEScanFilterPolicy::kNoWhiteList;

  auto id = SendCompleteCommand(cmd_data, std::move(packet), complete_cb);

  std::cout << "  Sent HCI_LE_Set_Scan_Parameters (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleLEScan(const CommandData* cmd_data,
                  const fxl::CommandLine& cmd_line,
                  const fxl::Closure& complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout << "  Usage: set-scan-params "
                 "[--help|--timeout=<t>|--no-dedup|--name-filter]"
              << std::endl;
    return false;
  }

  if (cmd_line.HasOption("help")) {
    std::cout
        << "  Options: \n"
           "    --help - Display this help message\n"
           "    --timeout=<t> - Duration (in seconds) during which to scan\n"
           "                    (default is 10 seconds)\n"
           "    --no-dedup - Tell the controller not to filter duplicate\n"
           "                 reports\n"
           "    --name-filter=<prefix> - Filter advertising reports by local\n"
           "                             name, if present.\n"
           "    --addr-type-filter=[public|random]";
    std::cout << std::endl;
    return false;
  }

  auto timeout = fxl::TimeDelta::FromSeconds(10);  // Default to 10 seconds.
  std::string timeout_str;
  if (cmd_line.GetOptionValue("timeout", &timeout_str)) {
    uint32_t time_seconds;
    if (!fxl::StringToNumberWithError(timeout_str, &time_seconds)) {
      std::cout << "  Malformed timeout value: " << timeout_str << std::endl;
      return false;
    }

    timeout = fxl::TimeDelta::FromSeconds(time_seconds);
  }

  std::string name_filter;
  cmd_line.GetOptionValue("name-filter", &name_filter);

  std::string addr_type_filter;
  cmd_line.GetOptionValue("addr-type-filter", &addr_type_filter);
  if (!addr_type_filter.empty() && addr_type_filter != "public" &&
      addr_type_filter != "random") {
    std::cout << "  Unknown address type filter: " << addr_type_filter
              << std::endl;
    return false;
  }

  ::btlib::hci::GenericEnableParam filter_duplicates =
      ::btlib::hci::GenericEnableParam::kEnable;
  if (cmd_line.HasOption("no-dedup")) {
    filter_duplicates = ::btlib::hci::GenericEnableParam::kDisable;
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::LESetScanEnableCommandParams);
  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kLESetScanEnable,
                                                 kPayloadSize);

  auto params =
      packet->mutable_view()
          ->mutable_payload<::btlib::hci::LESetScanEnableCommandParams>();
  params->scanning_enabled = ::btlib::hci::GenericEnableParam::kEnable;
  params->filter_duplicates = filter_duplicates;

  // Event handler to log when we receive advertising reports
  auto le_adv_report_cb = [name_filter, addr_type_filter](
                              const ::btlib::hci::EventPacket& event) {
    FXL_DCHECK(event.event_code() == ::btlib::hci::kLEMetaEventCode);
    FXL_DCHECK(
        event.view().payload<::btlib::hci::LEMetaEventParams>().subevent_code ==
        ::btlib::hci::kLEAdvertisingReportSubeventCode);

    ::btlib::hci::AdvertisingReportParser parser(event);
    const ::btlib::hci::LEAdvertisingReportData* data;
    int8_t rssi;
    while (parser.GetNextReport(&data, &rssi)) {
      DisplayAdvertisingReport(*data, rssi, name_filter, addr_type_filter);
    }
  };
  auto event_handler_id = cmd_data->cmd_channel()->AddLEMetaEventHandler(
      ::btlib::hci::kLEAdvertisingReportSubeventCode, le_adv_report_cb,
      cmd_data->task_runner());

  auto cleanup_cb =
      [ complete_cb, event_handler_id, cmd_channel = cmd_data->cmd_channel() ] {
    cmd_channel->RemoveEventHandler(event_handler_id);
    complete_cb();
  };

  // The callback invoked after scanning is stopped.
  auto final_cb = [cleanup_cb](::btlib::hci::CommandChannel::TransactionId id,
                               const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::SimpleReturnParams>();
    LogCommandComplete(return_params->status, id);
    cleanup_cb();
  };

  // Delayed task that stops scanning.
  auto scan_disable_cb = [cleanup_cb, final_cb, cmd_data] {
    auto packet = ::btlib::hci::CommandPacket::New(
        ::btlib::hci::kLESetScanEnable, kPayloadSize);
    auto params =
        packet->mutable_view()
            ->mutable_payload<::btlib::hci::LESetScanEnableCommandParams>();
    params->scanning_enabled = ::btlib::hci::GenericEnableParam::kDisable;
    params->filter_duplicates = ::btlib::hci::GenericEnableParam::kDisable;

    auto id = SendCommand(cmd_data, std::move(packet), final_cb, cleanup_cb);

    std::cout << "  Sent HCI_LE_Set_Scan_Enable (disabled) (id=" << id << ")"
              << std::endl;
  };

  auto cb = [scan_disable_cb, cleanup_cb, timeout,
             task_runner = cmd_data->task_runner()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::SimpleReturnParams>();
    LogCommandComplete(return_params->status, id);
    if (return_params->status != ::btlib::hci::Status::kSuccess) {
      cleanup_cb();
      return;
    }
    task_runner->PostDelayedTask(scan_disable_cb, timeout);
  };

  auto id = SendCommand(cmd_data, std::move(packet), cb, complete_cb);

  std::cout << "  Sent HCI_LE_Set_Scan_Enable (enabled) (id=" << id << ")"
            << std::endl;

  return true;
}

}  // namespace

void RegisterCommands(const CommandData* cmd_data,
                      ::bluetooth_tools::CommandDispatcher* dispatcher) {
  FXL_DCHECK(dispatcher);

#define BIND(handler) \
  std::bind(&handler, cmd_data, std::placeholders::_1, std::placeholders::_2)

  dispatcher->RegisterHandler("version-info",
                              "Send HCI_Read_Local_Version_Information",
                              BIND(HandleVersionInfo));
  dispatcher->RegisterHandler("reset", "Send HCI_Reset", BIND(HandleReset));
  dispatcher->RegisterHandler("read-bdaddr", "Send HCI_Read_BDADDR",
                              BIND(HandleReadBDADDR));
  dispatcher->RegisterHandler("read-local-name", "Send HCI_Read_Local_Name",
                              BIND(HandleReadLocalName));
  dispatcher->RegisterHandler("write-local-name", "Send HCI_Write_Local_Name",
                              BIND(HandleWriteLocalName));
  dispatcher->RegisterHandler("set-event-mask", "Send HCI_Set_Event_Mask",
                              BIND(HandleSetEventMask));
  dispatcher->RegisterHandler("le-set-adv-enable",
                              "Send HCI_LE_Set_Advertising_Enable",
                              BIND(HandleLESetAdvEnable));
  dispatcher->RegisterHandler("le-set-adv-params",
                              "Send HCI_LE_Set_Advertising_Parameters",
                              BIND(HandleLESetAdvParams));
  dispatcher->RegisterHandler("le-set-adv-data",
                              "Send HCI_LE_Set_Advertising_Data",
                              BIND(HandleLESetAdvData));
  dispatcher->RegisterHandler("le-set-scan-params",
                              "Send HCI_LE_Set_Scan_Parameters",
                              BIND(HandleLESetScanParams));
  dispatcher->RegisterHandler("le-scan",
                              "Perform a LE device scan for a limited duration",
                              BIND(HandleLEScan));

#undef BIND
}

}  // namespace hcitool
