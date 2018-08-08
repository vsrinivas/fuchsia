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

namespace hcitool {
namespace {

::btlib::hci::CommandChannel::TransactionId SendCommand(
    const CommandData* cmd_data,
    std::unique_ptr<::btlib::hci::CommandPacket> packet,
    ::btlib::hci::CommandChannel::CommandCallback cb,
    fit::closure complete_cb) {
  return cmd_data->cmd_channel()->SendCommand(
      std::move(packet), cmd_data->dispatcher(),
      [complete_cb = std::move(complete_cb), cb = std::move(cb)](
          ::btlib::hci::CommandChannel::TransactionId id,
          const ::btlib::hci::EventPacket& event) {
        if (event.event_code() == ::btlib::hci::kCommandStatusEventCode) {
          auto status = event.ToStatus();
          std::cout << "  Command Status: " << status.ToString()
                    << " (id=" << id << ")" << std::endl;
          if (status != ::btlib::hci::StatusCode::kSuccess)
            complete_cb();
          return;
        }
        cb(id, event);
      });
}

void LogCommandResult(::btlib::hci::StatusCode status,
                      ::btlib::hci::CommandChannel::TransactionId id,
                      const std::string& event_name = "Command Complete") {
  std::cout << fxl::StringPrintf("  %s - status: 0x%02x (id=%lu)\n",
                                 event_name.c_str(), status, id);
}

::btlib::hci::CommandChannel::TransactionId SendCompleteCommand(
    const CommandData* cmd_data,
    std::unique_ptr<::btlib::hci::CommandPacket> packet,
    fit::closure complete_cb) {
  auto cb = [complete_cb = complete_cb.share()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::SimpleReturnParams>();
    LogCommandResult(return_params->status, id);
    complete_cb();
  };
  return SendCommand(cmd_data, std::move(packet), std::move(cb),
                     std::move(complete_cb));
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
                              int8_t rssi, const std::string& name_filter,
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

void DisplayInquiryResult(const ::btlib::hci::InquiryResult& result) {
  std::cout << "  Result: " << result.bd_addr << " (" << result.class_of_device
            << ")" << std::endl;
}

bool HandleVersionInfo(const CommandData* cmd_data,
                       const fxl::CommandLine& cmd_line,
                       fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: version-info" << std::endl;
    return false;
  }

  auto cb = [complete_cb = complete_cb.share()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) {
    auto params =
        event.return_params<::btlib::hci::ReadLocalVersionInfoReturnParams>();
    LogCommandResult(params->status, id);
    if (params->status != ::btlib::hci::StatusCode::kSuccess) {
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
  auto id = SendCommand(cmd_data, std::move(packet), std::move(cb),
                        std::move(complete_cb));

  std::cout << "  Sent HCI_Read_Local_Version_Information (id=" << id << ")"
            << std::endl;
  return true;
}

bool HandleReset(const CommandData* cmd_data, const fxl::CommandLine& cmd_line,
                 fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: reset" << std::endl;
    return false;
  }

  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kReset);
  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_Reset (id=" << id << ")" << std::endl;

  return true;
}

bool HandleReadBDADDR(const CommandData* cmd_data,
                      const fxl::CommandLine& cmd_line,
                      fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-bdaddr" << std::endl;
    return false;
  }

  auto cb = [complete_cb = complete_cb.share()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::ReadBDADDRReturnParams>();
    LogCommandResult(return_params->status, id);
    if (return_params->status != ::btlib::hci::StatusCode::kSuccess) {
      complete_cb();
      return;
    }

    std::cout << "  BD_ADDR: " << return_params->bd_addr.ToString()
              << std::endl;
    complete_cb();
  };

  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kReadBDADDR);
  auto id = SendCommand(cmd_data, std::move(packet), std::move(cb),
                        std::move(complete_cb));

  std::cout << "  Sent HCI_Read_BDADDR (id=" << id << ")" << std::endl;

  return true;
}

bool HandleReadLocalName(const CommandData* cmd_data,
                         const fxl::CommandLine& cmd_line,
                         fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-local-name" << std::endl;
    return false;
  }

  auto cb = [complete_cb = complete_cb.share()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::ReadLocalNameReturnParams>();
    LogCommandResult(return_params->status, id);
    if (return_params->status != ::btlib::hci::StatusCode::kSuccess) {
      complete_cb();
      return;
    }

    std::cout << "  Local Name: " << return_params->local_name << std::endl;

    complete_cb();
  };

  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kReadLocalName);
  auto id = SendCommand(cmd_data, std::move(packet), std::move(cb),
                        std::move(complete_cb));
  std::cout << "  Sent HCI_Read_Local_Name (id=" << id << ")" << std::endl;

  return true;
}

bool HandleWriteLocalName(const CommandData* cmd_data,
                          const fxl::CommandLine& cmd_line,
                          fit::closure complete_cb) {
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

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));
  std::cout << "  Sent HCI_Write_Local_Name (id=" << id << ")" << std::endl;

  return true;
}

bool HandleSetEventMask(const CommandData* cmd_data,
                        const fxl::CommandLine& cmd_line,
                        fit::closure complete_cb) {
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

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_Set_Event_Mask("
            << fxl::NumberToString(mask, fxl::Base::k16) << ") (id=" << id
            << ")" << std::endl;
  return true;
}

bool HandleLESetAdvEnable(const CommandData* cmd_data,
                          const fxl::CommandLine& cmd_line,
                          fit::closure complete_cb) {
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

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_LE_Set_Advertising_Enable (id=" << id << ")"
            << std::endl;
  return true;
}

bool HandleLESetAdvParams(const CommandData* cmd_data,
                          const fxl::CommandLine& cmd_line,
                          fit::closure complete_cb) {
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

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_LE_Set_Advertising_Parameters (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleLESetAdvData(const CommandData* cmd_data,
                        const fxl::CommandLine& cmd_line,
                        fit::closure complete_cb) {
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

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_LE_Set_Advertising_Data (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleLESetScanParams(const CommandData* cmd_data,
                           const fxl::CommandLine& cmd_line,
                           fit::closure complete_cb) {
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

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_LE_Set_Scan_Parameters (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleLEScan(const CommandData* cmd_data, const fxl::CommandLine& cmd_line,
                  fit::closure complete_cb) {
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
      cmd_data->dispatcher());

  fit::closure cleanup_cb = [complete_cb = complete_cb.share(),
                             event_handler_id,
                             cmd_channel = cmd_data->cmd_channel()] {
    cmd_channel->RemoveEventHandler(event_handler_id);
    complete_cb();
  };

  // The callback invoked after scanning is stopped.
  auto final_cb = [cleanup_cb = cleanup_cb.share()](
                      ::btlib::hci::CommandChannel::TransactionId id,
                      const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::SimpleReturnParams>();
    LogCommandResult(return_params->status, id);
    cleanup_cb();
  };

  // Delayed task that stops scanning.
  auto scan_disable_cb = [cleanup_cb = cleanup_cb.share(),
                          final_cb = std::move(final_cb), cmd_data]() mutable {
    auto packet = ::btlib::hci::CommandPacket::New(
        ::btlib::hci::kLESetScanEnable, kPayloadSize);
    auto params =
        packet->mutable_view()
            ->mutable_payload<::btlib::hci::LESetScanEnableCommandParams>();
    params->scanning_enabled = ::btlib::hci::GenericEnableParam::kDisable;
    params->filter_duplicates = ::btlib::hci::GenericEnableParam::kDisable;

    auto id = SendCommand(cmd_data, std::move(packet), std::move(final_cb),
                          std::move(cleanup_cb));

    std::cout << "  Sent HCI_LE_Set_Scan_Enable (disabled) (id=" << id << ")"
              << std::endl;
  };

  auto cb = [scan_disable_cb = std::move(scan_disable_cb),
             cleanup_cb = cleanup_cb.share(), timeout,
             dispatcher = cmd_data->dispatcher()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) mutable {
    auto return_params =
        event.return_params<::btlib::hci::SimpleReturnParams>();
    LogCommandResult(return_params->status, id);
    if (return_params->status != ::btlib::hci::StatusCode::kSuccess) {
      cleanup_cb();
      return;
    }
    async::PostDelayedTask(dispatcher, std::move(scan_disable_cb),
                           zx::duration(timeout.ToNanoseconds()));
  };

  auto id = SendCommand(cmd_data, std::move(packet), std::move(cb),
                        std::move(complete_cb));

  std::cout << "  Sent HCI_LE_Set_Scan_Enable (enabled) (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleBRScan(const CommandData* cmd_data, const fxl::CommandLine& cmd_line,
                  fit::closure complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout << "  Usage: scan "
                 "[--help|--timeout=<t>|--filter=<prefix>|--max-responses=<n>]"
              << std::endl;
    return false;
  }

  if (cmd_line.HasOption("help")) {
    std::cout
        << "  Options: \n"
           "    --help - Display this help message\n"
           "    --timeout=<t> - Maximum duration (in seconds) of the scan\n"
           "                    (default is 30 seconds)\n"
           "    --filter=<prefix> - Filter devices reported by name or\n"
           "                        BR_ADDR prefix.\n"
           "    --max-responses=<n> - End scan after n responses are\n"
           "                          received.\n";
    std::cout << std::endl;
    return false;
  }

  auto timeout = zx::sec(30);  // Default 30 seconds.
  std::string timeout_str;
  if (cmd_line.GetOptionValue("timeout", &timeout_str)) {
    uint32_t time_seconds;
    if (!fxl::StringToNumberWithError(timeout_str, &time_seconds)) {
      std::cout << "  Malformed timeout value: " << timeout_str << std::endl;
      return false;
    }
    // TODO(jamuraa): support longer than 61 second scans by repeating the
    // Inquiry
    if (time_seconds > 61) {
      std::cout << "  Maximum inquiry length is 61 seconds." << std::endl;
      return false;
    }

    timeout = zx::sec(time_seconds);
  }

  std::string filter;
  cmd_line.GetOptionValue("filter", &filter);

  uint8_t max_responses = 0;
  std::string max_responses_str;
  if (cmd_line.GetOptionValue("max-responses", &max_responses_str)) {
    uint32_t responses;
    if (!fxl::StringToNumberWithError(max_responses_str, &responses)) {
      std::cout << "  Malformed maximum responses value: " << max_responses_str
                << std::endl;
      return false;
    }
    if (responses > 255) {
      std::cout << "  Maximum responses must be less than 255." << std::endl;
      return false;
    }
    max_responses = uint8_t(responses);
  }

  constexpr size_t kPayloadSize = sizeof(::btlib::hci::InquiryCommandParams);
  auto packet =
      ::btlib::hci::CommandPacket::New(::btlib::hci::kInquiry, kPayloadSize);
  auto params = packet->mutable_view()
                    ->mutable_payload<::btlib::hci::InquiryCommandParams>();

  params->lap = ::btlib::hci::kGIAC;
  // Always use the maximum inquiry length, we will time it more accurately.
  params->inquiry_length = ::btlib::hci::kInquiryLengthMax;
  params->num_responses = max_responses;

  auto event_handler_ids = std::make_shared<
      std::vector<btlib::hci::CommandChannel::EventHandlerId>>();
  fit::closure cleanup_cb = [complete_cb = std::move(complete_cb),
                             event_handler_ids,
                             cmd_channel = cmd_data->cmd_channel()] {
    for (const auto& handler_id : *event_handler_ids) {
      cmd_channel->RemoveEventHandler(handler_id);
    }
    complete_cb();
  };

  // Event handler to log when we receive advertising reports
  auto inquiry_result_cb = [filter](const ::btlib::hci::EventPacket& event) {
    FXL_DCHECK(event.event_code() == ::btlib::hci::kInquiryResultEventCode);

    const auto& result =
        event.view().payload<::btlib::hci::InquiryResultEventParams>();

    for (int i = 0; i < result.num_responses; i++) {
      if (!filter.empty() &&
          !filter.compare(0, filter.length(),
                          result.responses[i].bd_addr.ToString())) {
        continue;
      }
      DisplayInquiryResult(result.responses[i]);
    }
  };

  event_handler_ids->push_back(cmd_data->cmd_channel()->AddEventHandler(
      ::btlib::hci::kInquiryResultEventCode, std::move(inquiry_result_cb),
      cmd_data->dispatcher()));

  // The callback invoked for an Inquiry Complete response.
  auto inquiry_complete_cb =
      [cleanup_cb =
           cleanup_cb.share()](const ::btlib::hci::EventPacket& event) mutable {
        auto params =
            event.view().payload<::btlib::hci::InquiryCompleteEventParams>();
        std::cout << fxl::StringPrintf("  Inquiry Complete - status: 0x%02x\n",
                                       params.status);
        cleanup_cb();
      };

  event_handler_ids->push_back(cmd_data->cmd_channel()->AddEventHandler(
      ::btlib::hci::kInquiryCompleteEventCode, std::move(inquiry_complete_cb),
      cmd_data->dispatcher()));

  // Delayed task that stops scanning.
  auto inquiry_cancel_cb = [cleanup_cb = cleanup_cb.share(),
                            cmd_data]() mutable {
    auto packet =
        ::btlib::hci::CommandPacket::New(::btlib::hci::kInquiryCancel, 0);
    auto id =
        SendCompleteCommand(cmd_data, std::move(packet), std::move(cleanup_cb));
    std::cout << "  Sent HCI_Inquiry_Cancel (id=" << id << ")" << std::endl;
  };

  auto cb = [inquiry_cancel_cb = std::move(inquiry_cancel_cb),
             cleanup_cb = cleanup_cb.share(), timeout,
             dispatcher = cmd_data->dispatcher()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) mutable {
    auto return_params =
        event.view().payload<::btlib::hci::CommandStatusEventParams>();
    LogCommandResult(return_params.status, id, "Command Status");
    if (return_params.status != ::btlib::hci::StatusCode::kSuccess) {
      cleanup_cb();
      return;
    }
    async::PostDelayedTask(dispatcher, std::move(inquiry_cancel_cb), timeout);
  };

  // Inquiry sends a Command Status, and then we wait for the Inquiry Complete,
  // or the timer to run out, for a long time. Count this as "complete" when the
  // Status comes in.
  auto id = cmd_data->cmd_channel()->SendCommand(
      std::move(packet), cmd_data->dispatcher(), std::move(cb),
      ::btlib::hci::kCommandStatusEventCode);
  std::cout << "  Sent HCI_Inquiry (id=" << id << ")" << std::endl;

  return true;
}

bool HandleWritePageScanActivity(const CommandData* cmd_data,
                                 const fxl::CommandLine& cmd_line,
                                 fit::closure complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout << "  Usage: write-page-scan-activity [--help\n"
                 "                                   |--interval=<interval>\n"
                 "                                   |--window=<window>]"
              << std::endl;
    return false;
  }

  if (cmd_line.HasOption("help")) {
    std::cout << "  Options:\n"
                 "    --help - Display this help message\n"
                 "    --mode=R0|R1|R2 - Use a specific scanning mode\n"
                 "    --interval=<interval> - Set page scan interval (in hex)\n"
                 "    --window=<window> - Set page scan window (in hex)\n";
    std::cout << std::endl;
    return false;
  }

  uint16_t page_scan_interval = ::btlib::hci::kPageScanR1Interval;
  uint16_t page_scan_window = ::btlib::hci::kPageScanR1Window;

  std::string mode_str;
  if (cmd_line.GetOptionValue("mode", &mode_str)) {
    if (mode_str == "R0") {
      page_scan_interval = ::btlib::hci::kPageScanR0Interval;
      page_scan_window = ::btlib::hci::kPageScanR0Window;
    } else if (mode_str == "R1") {
      page_scan_interval = ::btlib::hci::kPageScanR1Interval;
      page_scan_window = ::btlib::hci::kPageScanR1Window;
    } else if (mode_str == "R2") {
      page_scan_interval = ::btlib::hci::kPageScanR2Interval;
      page_scan_window = ::btlib::hci::kPageScanR2Window;
    } else {
      std::cout << "  Unrecognized mode value: " << mode_str << std::endl;
      return false;
    }
  }

  // Check for manual settings.
  std::string interval_str;
  if (cmd_line.GetOptionValue("interval", &interval_str)) {
    uint16_t parsed_interval;
    if (!fxl::StringToNumberWithError(interval_str, &parsed_interval,
                                      fxl::Base::k16)) {
      std::cout << "  Malformed interval value: " << interval_str << std::endl;
      return false;
    }
    if (parsed_interval < ::btlib::hci::kPageScanIntervalMin ||
        parsed_interval > ::btlib::hci::kPageScanIntervalMax) {
      std::cout << "  Interval value is out of the allowed range." << std::endl;
      return false;
    }
    if (parsed_interval % 2 != 0) {
      std::cout << "  Interval value must be even." << std::endl;
      return false;
    }

    page_scan_interval = parsed_interval;
  }

  std::string window_str;
  if (cmd_line.GetOptionValue("window", &window_str)) {
    uint16_t parsed_window;
    if (!fxl::StringToNumberWithError(window_str, &parsed_window,
                                      fxl::Base::k16)) {
      std::cout << "  Malformed window value: " << window_str << std::endl;
      return false;
    }
    if (parsed_window < ::btlib::hci::kPageScanWindowMin ||
        parsed_window > ::btlib::hci::kPageScanWindowMax) {
      std::cout << "  Window value is out of the allowed range." << std::endl;
      return false;
    }
    if (parsed_window > page_scan_interval) {
      std::cout
          << "  Window value must be less than or equal to interval value."
          << std::endl;
      return false;
    }

    page_scan_window = parsed_window;
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::WritePageScanActivityCommandParams);
  auto packet = ::btlib::hci::CommandPacket::New(
      ::btlib::hci::kWritePageScanActivity, kPayloadSize);
  auto params =
      packet->mutable_view()
          ->mutable_payload<::btlib::hci::WritePageScanActivityCommandParams>();
  params->page_scan_interval = page_scan_interval;
  params->page_scan_window = page_scan_window;

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_Write_Page_Scan_Activity (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleReadPageScanActivity(const CommandData* cmd_data,
                                const fxl::CommandLine& cmd_line,
                                fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-page-scan-activity" << std::endl;
    return false;
  }

  auto cb = [complete_cb = complete_cb.share()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::ReadPageScanActivityReturnParams>();
    LogCommandResult(return_params->status, id);
    if (return_params->status != ::btlib::hci::StatusCode::kSuccess) {
      complete_cb();
      return;
    }

    std::cout << "  Interval: " << return_params->page_scan_interval
              << std::endl;
    std::cout << "  Window: " << return_params->page_scan_window << std::endl;

    complete_cb();
  };

  auto packet =
      ::btlib::hci::CommandPacket::New(::btlib::hci::kReadPageScanActivity);
  auto id = SendCommand(cmd_data, std::move(packet), std::move(cb),
                        std::move(complete_cb));
  std::cout << "  Sent HCI_Read_Page_Scan_Activity (id=" << id << ")"
            << std::endl;

  return true;
}

bool HandleWritePageScanType(const CommandData* cmd_data,
                             const fxl::CommandLine& cmd_line,
                             fit::closure complete_cb) {
  if (cmd_line.positional_args().size()) {
    std::cout
        << "  Usage: write-page-scan-type [--help|--standard|--interlaced]"
        << std::endl;
    return false;
  }

  if (cmd_line.HasOption("help")) {
    std::cout << "  Options:\n"
                 "    --help - Display this help message\n"
                 "    --type=standard|interlaced - Choose scanning type";
    std::cout << std::endl;
    return false;
  }

  ::btlib::hci::PageScanType page_scan_type =
      ::btlib::hci::PageScanType::kStandardScan;
  std::string type_str;
  if (cmd_line.GetOptionValue("type", &type_str)) {
    if (type_str == "standard") {
      page_scan_type = ::btlib::hci::PageScanType::kStandardScan;
    } else if (type_str == "interlaced") {
      page_scan_type = ::btlib::hci::PageScanType::kInterlacedScan;
    } else {
      std::cout << "  Unrecognized type: " << type_str << std::endl;
    }
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::WritePageScanTypeCommandParams);
  auto packet = ::btlib::hci::CommandPacket::New(
      ::btlib::hci::kWritePageScanType, kPayloadSize);

  packet->mutable_view()
      ->mutable_payload<::btlib::hci::WritePageScanTypeCommandParams>()
      ->page_scan_type = page_scan_type;

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_Write_Page_Scan_Type (id=" << id << ")" << std::endl;

  return true;
}

bool HandleReadPageScanType(const CommandData* cmd_data,
                            const fxl::CommandLine& cmd_line,
                            fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-page-scan-type" << std::endl;
    return false;
  }

  auto cb = [complete_cb = complete_cb.share()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::ReadPageScanTypeReturnParams>();
    LogCommandResult(return_params->status, id);
    if (return_params->status != ::btlib::hci::StatusCode::kSuccess) {
      complete_cb();
      return;
    }

    if (return_params->page_scan_type ==
        ::btlib::hci::PageScanType::kStandardScan) {
      std::cout << "  Type: standard" << std::endl;
    } else if (return_params->page_scan_type ==
               ::btlib::hci::PageScanType::kInterlacedScan) {
      std::cout << "  Type: interlaced" << std::endl;
    } else {
      std::cout << "  Type: unknown" << std::endl;
    }

    complete_cb();
  };

  auto packet =
      ::btlib::hci::CommandPacket::New(::btlib::hci::kReadPageScanType);
  auto id = SendCommand(cmd_data, std::move(packet), std::move(cb),
                        std::move(complete_cb));
  std::cout << "  Sent HCI_Read_Page_Scan_Type (id=" << id << ")" << std::endl;

  return true;
}

bool HandleWriteScanEnable(const CommandData* cmd_data,
                           const fxl::CommandLine& cmd_line,
                           fit::closure complete_cb) {
  if (cmd_line.positional_args().size() > 2) {
    std::cout << "  Usage: write-scan-enable [--help] [page] [inquiry]"
              << std::endl;
    return false;
  }

  if (cmd_line.HasOption("help")) {
    std::cout << "  Arguments:\n"
                 "    include \"page\" to enable page scan\n"
                 "    include \"inquiry\" to enable inquiry scan\n"
                 "  Options:\n"
                 "    --help - Display this help message";
    std::cout << std::endl;
    return false;
  }

  ::btlib::hci::ScanEnableType scan_enable = 0x00;
  for (std::string positional_arg : cmd_line.positional_args()) {
    if (positional_arg == "inquiry") {
      scan_enable |=
          (::btlib::hci::ScanEnableType)::btlib::hci::ScanEnableBit::kInquiry;
    } else if (positional_arg == "page") {
      scan_enable |=
          (::btlib::hci::ScanEnableType)::btlib::hci::ScanEnableBit::kPage;
    } else {
      std::cout << "  Unrecognized positional argument: " << positional_arg
                << std::endl;
      return false;
    }
  }

  constexpr size_t kPayloadSize =
      sizeof(::btlib::hci::WriteScanEnableCommandParams);
  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kWriteScanEnable,
                                                 kPayloadSize);

  packet->mutable_view()
      ->mutable_payload<::btlib::hci::WriteScanEnableCommandParams>()
      ->scan_enable = scan_enable;

  auto id =
      SendCompleteCommand(cmd_data, std::move(packet), std::move(complete_cb));

  std::cout << "  Sent HCI_Write_Scan_Enable (id=" << id << ")" << std::endl;

  return true;
}

bool HandleReadScanEnable(const CommandData* cmd_data,
                          const fxl::CommandLine& cmd_line,
                          fit::closure complete_cb) {
  if (cmd_line.positional_args().size() || cmd_line.options().size()) {
    std::cout << "  Usage: read-scan-enable" << std::endl;
    return false;
  }

  auto cb = [complete_cb = complete_cb.share()](
                ::btlib::hci::CommandChannel::TransactionId id,
                const ::btlib::hci::EventPacket& event) {
    auto return_params =
        event.return_params<::btlib::hci::ReadScanEnableReturnParams>();
    LogCommandResult(return_params->status, id);
    if (return_params->status != ::btlib::hci::StatusCode::kSuccess) {
      complete_cb();
      return;
    }

    if (return_params->scan_enable &
        (::btlib::hci::ScanEnableType)::btlib::hci::ScanEnableBit::kInquiry) {
      std::cout << "  Inquiry scan: enabled" << std::endl;
    } else {
      std::cout << "  Inquiry scan: disabled" << std::endl;
    }

    if (return_params->scan_enable &
        (::btlib::hci::ScanEnableType)::btlib::hci::ScanEnableBit::kPage) {
      std::cout << "  Page scan: enabled" << std::endl;
    } else {
      std::cout << "  Page scan: disabled" << std::endl;
    }

    complete_cb();
  };

  auto packet = ::btlib::hci::CommandPacket::New(::btlib::hci::kReadScanEnable);
  auto id = SendCommand(cmd_data, std::move(packet), std::move(cb),
                        std::move(complete_cb));
  std::cout << "  Sent HCI_Read_Scan_Enable (id=" << id << ")" << std::endl;

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
  dispatcher->RegisterHandler("scan",
                              "Perform a device scan for a limited duration",
                              BIND(HandleBRScan));
  dispatcher->RegisterHandler("write-page-scan-activity",
                              "Send HCI_Write_Page_Scan_Activity",
                              BIND(HandleWritePageScanActivity));
  dispatcher->RegisterHandler("read-page-scan-activity",
                              "Send HCI_Read_Page_Scan_Activity",
                              BIND(HandleReadPageScanActivity));
  dispatcher->RegisterHandler("write-page-scan-type",
                              "Send HCI_Write_Page_Scan_Type",
                              BIND(HandleWritePageScanType));
  dispatcher->RegisterHandler("read-page-scan-type",
                              "Send HCI_Read_Page_Scan_Type",
                              BIND(HandleReadPageScanType));
  dispatcher->RegisterHandler("write-scan-enable", "Send HCI_Write_Scan_Enable",
                              BIND(HandleWriteScanEnable));
  dispatcher->RegisterHandler("read-scan-enable", "Send HCI_Read_Scan_Enable",
                              BIND(HandleReadScanEnable));
#undef BIND
}

}  // namespace hcitool
