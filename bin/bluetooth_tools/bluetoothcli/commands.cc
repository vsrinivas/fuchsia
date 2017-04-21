// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <iostream>

#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

#include "app.h"
#include "logging.h"

namespace bluetoothcli {
namespace commands {
namespace {

// TODO(armansito): IWBN if FIDL could generate this for us.
std::string AppearanceToString(bluetooth::control::Appearance appearance) {
  switch (appearance) {
    case bluetooth::control::Appearance::GENERIC_PHONE:
      return "GENERIC_PHONE";
    case bluetooth::control::Appearance::GENERIC_COMPUTER:
      return "GENERIC_COMPUTER";
    case bluetooth::control::Appearance::GENERIC_WATCH:
      return "GENERIC_WATCH";
    case bluetooth::control::Appearance::GENERIC_CLOCK:
      return "GENERIC_CLOCK";
    case bluetooth::control::Appearance::GENERIC_DISPLAY:
      return "GENERIC_DISPLAY";
    case bluetooth::control::Appearance::GENERIC_REMOTE_CONTROL:
      return "GENERIC_REMOTE_CONTROL";
    case bluetooth::control::Appearance::GENERIC_EYE_GLASSES:
      return "GENERIC_EYE_GLASSES";
    case bluetooth::control::Appearance::GENERIC_TAG:
      return "GENERIC_TAG";
    case bluetooth::control::Appearance::GENERIC_KEYRING:
      return "GENERIC_KEYRING";
    case bluetooth::control::Appearance::GENERIC_MEDIA_PLAYER:
      return "GENERIC_MEDIA_PLAYER";
    case bluetooth::control::Appearance::GENERIC_BARCODE_SCANNER:
      return "GENERIC_BARCODE_SCANNER";
    case bluetooth::control::Appearance::GENERIC_THERMOMETER:
      return "GENERIC_THERMOMETER";
    case bluetooth::control::Appearance::GENERIC_HEART_RATE_SENSOR:
      return "GENERIC_HEART_RATE_SENSOR";
    case bluetooth::control::Appearance::GENERIC_BLOOD_PRESSURE:
      return "GENERIC_BLOOD_PRESSURE";
    case bluetooth::control::Appearance::GENERIC_GLUCOSE_METER:
      return "GENERIC_GLUCOSE_METER";
    case bluetooth::control::Appearance::GENERIC_RUNNING_WALKING_SENSOR:
      return "GENERIC_RUNNING_WALKING_SENSOR";
    case bluetooth::control::Appearance::GENERIC_CYCLING:
      return "GENERIC_CYCLING";
    case bluetooth::control::Appearance::GENERIC_PULSE_OXIMETER:
      return "GENERIC_PULSE_OXIMETER";
    case bluetooth::control::Appearance::GENERIC_WEIGHT_SCALE:
      return "GENERIC_WEIGHT_SCALE";
    case bluetooth::control::Appearance::GENERIC_PERSONAL_MOBILITY_DEVICE:
      return "GENERIC_PERSONAL_MOBILITY_DEVICE";
    case bluetooth::control::Appearance::GENERIC_CONTINUOUS_GLUCOSE_MONITOR:
      return "GENERIC_CONTINUOUS_GLUCOSE_MONITOR";
    case bluetooth::control::Appearance::GENERIC_OUTDOOR_SPORTS_ACTIVITY:
      return "GENERIC_OUTDOOR_SPORTS_ACTIVITY";
    case bluetooth::control::Appearance::HID_KEYBOARD:
      return "HID_KEYBOARD";
    case bluetooth::control::Appearance::HID_MOUSE:
      return "HID_MOUSE";
    case bluetooth::control::Appearance::HID_JOYSTICK:
      return "HID_JOYSTICK";
    case bluetooth::control::Appearance::HID_GAMEPAD:
      return "HID_GAMEPAD";
    case bluetooth::control::Appearance::HID_DIGITIZER_TABLET:
      return "HID_DIGITIZER_TABLET";
    case bluetooth::control::Appearance::HID_CARD_READER:
      return "HID_CARD_READER";
    case bluetooth::control::Appearance::HID_DIGITAL_PEN:
      return "HID_DIGITAL_PEN";
    case bluetooth::control::Appearance::HID_BARCODE_SCANNER:
      return "HID_BARCODE_SCANNER";
    case bluetooth::control::Appearance::UNKNOWN:
    default:
      break;
  }
  return "UNKNOWN";
}

std::string BoolToString(bool val) {
  return val ? "yes" : "no";
}

void PrintAdapterInfo(const bluetooth::control::AdapterInfoPtr& adapter_info, size_t indent = 0u) {
  CLI_LOG_INDENT(indent) << "id: " << adapter_info->identifier;
  CLI_LOG_INDENT(indent) << "address: " << adapter_info->address;
  CLI_LOG_INDENT(indent) << "appearance: " << AppearanceToString(adapter_info->appearance);
  CLI_LOG_INDENT(indent) << "powered: " << BoolToString(adapter_info->state->powered->value);
}

bool HandleAvailable(const App* app, const ftl::CommandLine& cmd_line,
                     const ftl::Closure& complete_cb) {
  app->adapter_manager()->IsBluetoothAvailable([complete_cb](bool available) {
    CLI_LOG() << ftl::StringPrintf("Bluetooth is %savailable", available ? "" : "not ");
    complete_cb();
  });
  return true;
}

bool HandleListAdapters(const App* app, const ftl::CommandLine& cmd_line,
                        const ftl::Closure& complete_cb) {
  app->adapter_manager()->GetAdapters(
      [complete_cb](fidl::Array<bluetooth::control::AdapterInfoPtr> adapters) {
        auto ac = ftl::MakeAutoCall(complete_cb);

        if (!adapters || adapters.size() == 0) {
          CLI_LOG() << "No adapters";
          return;
        }

        size_t i = 0;
        for (auto& adapter : adapters) {
          CLI_LOG() << "Adapter " << i++ << ":";
          PrintAdapterInfo(adapter, 1);
        }
      });
  return true;
}

bool HandleActiveAdapter(const App* app, const ftl::CommandLine& cmd_line,
                         const ftl::Closure& complete_cb) {
  if (!app->active_adapter()) {
    CLI_LOG() << "No active adapter";
    return false;
  }

  app->active_adapter()->GetInfo([complete_cb](bluetooth::control::AdapterInfoPtr adapter_info) {
    PrintAdapterInfo(adapter_info);
    complete_cb();
  });

  return true;
}

bool HandleExit(const App* app, const ftl::CommandLine& cmd_line, const ftl::Closure& complete_cb) {
  mtl::MessageLoop::GetCurrent()->QuitNow();
  return true;
}

}  // namespace

void RegisterCommands(App* app, bluetooth::tools::CommandDispatcher* dispatcher) {
  FTL_DCHECK(dispatcher);

#define BIND(handler) std::bind(&handler, app, std::placeholders::_1, std::placeholders::_2)

  dispatcher->RegisterHandler("exit", "Exit the tool", BIND(HandleExit));
  dispatcher->RegisterHandler("available", "Check if Bluetooth is available on this platform",
                              BIND(HandleAvailable));
  dispatcher->RegisterHandler("list-adapters",
                              "Print information about available Bluetooth adapters",
                              BIND(HandleListAdapters));
  dispatcher->RegisterHandler("active-adapter",
                              "Print information about the current active adapter",
                              BIND(HandleActiveAdapter));

#undef BIND_HANDLER
}

}  // namespace commands
}  // namespace bluetoothcli
