// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>

#include "src/camera/bin/factory/factory_server.h"

enum FactoryServerCommand {
  START_STREAM,
  STOP_STREAM,
  CAPTURE_FRAMES,
  DISPLAY_TO_SCREEN,
  GET_OTP_DATA,
  GET_SENSOR_TEMPERATURE,
  SET_AWB_MODE,
  SET_AE_MODE,
  SET_EXPOSURE,
  SET_SENSOR_MODE,
  SET_TEST_PATTERN_MODE,
};

constexpr std::string_view kCommand0 = "StartStream";
constexpr std::string_view kCommand1 = "StopStream";
constexpr std::string_view kCommand2 = "CaptureFrames";
constexpr std::string_view kCommand3 = "DisplayToScreen";
constexpr std::string_view kCommand4 = "GetOtpData";
constexpr std::string_view kCommand5 = "GetSensorTemperature";
constexpr std::string_view kCommand6 = "SetAWBMode";
constexpr std::string_view kCommand7 = "SetAEMode";
constexpr std::string_view kCommand8 = "SetExposure";
constexpr std::string_view kCommand9 = "SetSensorMode";
constexpr std::string_view kCommand10 = "SetTestPatternMode";
constexpr std::array<std::string_view, 11> kCommandArr{{
    kCommand0,
    kCommand1,
    kCommand2,
    kCommand3,
    kCommand4,
    kCommand5,
    kCommand6,
    kCommand7,
    kCommand8,
    kCommand9,
    kCommand10,
}};

int main(int argc, char* argv[]) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line, {"camera-factory"})) {
    FX_LOGS(ERROR) << "Malformed input.";
    return EXIT_FAILURE;
  }

  auto const option = command_line.options()[0];
  auto const command = option.name;
  auto const value = option.value;
  auto itr = std::find(kCommandArr.begin(), kCommandArr.end(), command);
  if (itr == kCommandArr.end()) {
    FX_LOGS(ERROR) << "No valid command entered.";
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Connect to required services.
  fuchsia::sysmem::AllocatorHandle allocator;
  zx_status_t status = context->svc()->Connect(allocator.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Allocator service.";
    return EXIT_FAILURE;
  }

  fuchsia::camera3::DeviceWatcherHandle watcher;
  status = context->svc()->Connect(watcher.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request DeviceWatcher service.";
    return EXIT_FAILURE;
  }

  // Create the streamer and pass it to the FactoryServer.
  auto streamer_result =
      camera::Streamer::Create(std::move(allocator), std::move(watcher), [&] { loop.Quit(); });
  if (streamer_result.is_error()) {
    FX_PLOGS(ERROR, streamer_result.error()) << "Failed to create Streamer.";
    return EXIT_FAILURE;
  }
  auto streamer = streamer_result.take_value();

  // Create the factory server.
  auto factory_server_result =
      camera::FactoryServer::Create(std::move(streamer), [&] { loop.Quit(); });
  if (factory_server_result.is_error()) {
    FX_PLOGS(ERROR, factory_server_result.error()) << "Failed to create FactoryServer.";
    return EXIT_FAILURE;
  }
  auto factory_server = factory_server_result.take_value();

  switch (std::distance(kCommandArr.begin(), itr)) {
    case GET_OTP_DATA:
      factory_server->GetOtpData();
      break;
    case GET_SENSOR_TEMPERATURE:
      factory_server->GetSensorTemperature();
      break;
    case SET_AWB_MODE: {
      std::stringstream ss(value);
      std::string substr;
      std::array<uint32_t, 2> args{};
      for (size_t i = 0; i < args.size(); ++i) {
        getline(ss, substr, ',');
        args[i] = std::stof(substr);
      }
      factory_server->SetAWBMode(static_cast<fuchsia::factory::camera::WhiteBalanceMode>(args[0]),
                                 args[1]);
      break;
    }
    case SET_AE_MODE:
      factory_server->SetAEMode(
          static_cast<fuchsia::factory::camera::ExposureMode>(std::stoi(value)));
      break;
    case SET_EXPOSURE: {
      std::stringstream ss(value);
      std::string substr;
      std::array<float, 3> args{};
      for (size_t i = 0; i < args.size(); ++i) {
        getline(ss, substr, ',');
        args[i] = std::stof(substr);
      }
      factory_server->SetExposure(args[0], args[1], args[2]);
      break;
    }
    case SET_SENSOR_MODE:
      if (value.length() != 1) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Only accepts one uint";
        break;
      }
      factory_server->SetSensorMode(std::stoi(value));
      break;
    case SET_TEST_PATTERN_MODE:
      if (value.length() != 1) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Only accepts one uint";
        break;
      }
      factory_server->SetTestPatternMode(std::stoi(value));
      break;
    case CAPTURE_FRAMES:
      // TODO(fxbug.dev/58189) does not work, needs to wait for streaming
      factory_server->Capture();
      break;
    case START_STREAM:
      // just run the loop so streamers can start, and webui can be used
      break;
    case STOP_STREAM:
    case DISPLAY_TO_SCREEN:
    default:
      FX_PLOGS(ERROR, ZX_ERR_NOT_SUPPORTED) << "Command not implemented yet";
      return EXIT_FAILURE;
  }

  loop.Run();

  factory_server = nullptr;
  FX_LOGS(INFO) << "FactoryServer ran with command " << command;

  return EXIT_SUCCESS;
}
