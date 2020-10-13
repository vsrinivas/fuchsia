// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/factory/camera/cpp/fidl.h>
#include <fuchsia/images/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>

#include <fbl/unique_fd.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

enum Target {
  CONTROLLER,
  ISP,
};

enum Command {
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
  SET_BYPASS_MODE,
};

constexpr std::string_view kDevicePath = "/dev/class/isp/000";
constexpr std::string_view kFlag = "command";

// Controller Commands
constexpr std::string_view kCommand2 = "CaptureFrames";
constexpr std::string_view kCommand3 = "DisplayToScreen";
// ISP Commands
constexpr std::string_view kCommand4 = "GetOtpData";
constexpr std::string_view kCommand5 = "GetSensorTemperature";
constexpr std::string_view kCommand6 = "SetAWBMode";
constexpr std::string_view kCommand7 = "SetAEMode";
constexpr std::string_view kCommand8 = "SetExposure";
constexpr std::string_view kCommand9 = "SetSensorMode";
constexpr std::string_view kCommand10 = "SetTestPatternMode";
constexpr std::string_view kCommand11 = "SetBypassMode";

fit::result<Command, zx_status_t> StrToCommand(const std::string& str) {
  // Controller
  if (str == kCommand2)
    return fit::ok(CAPTURE_FRAMES);
  if (str == kCommand3)
    return fit::ok(DISPLAY_TO_SCREEN);
  // ISP
  if (str == kCommand4)
    return fit::ok(GET_OTP_DATA);
  if (str == kCommand5)
    return fit::ok(GET_SENSOR_TEMPERATURE);
  if (str == kCommand6)
    return fit::ok(SET_AWB_MODE);
  if (str == kCommand7)
    return fit::ok(SET_AE_MODE);
  if (str == kCommand8)
    return fit::ok(SET_EXPOSURE);
  if (str == kCommand9)
    return fit::ok(SET_SENSOR_MODE);
  if (str == kCommand10)
    return fit::ok(SET_TEST_PATTERN_MODE);
  if (str == kCommand11)
    return fit::ok(SET_BYPASS_MODE);
  return fit::error(ZX_ERR_INVALID_ARGS);
}

// TODO(fxbug.dev/58025): The varius std::stoi() calls can fail here and cause a crash, be sure to
// sanitize input.
zx_status_t RunCommand(fuchsia::factory::camera::ControllerPtr& controller,
                       fuchsia::factory::camera::IspPtr& isp, const Command command,
                       const std::vector<std::string> args,
                       fit::function<void(const std::string)> exit_on_success_cb,
                       fit::function<void(zx_status_t, const std::string)> exit_on_failure_cb) {
  switch (command) {
    case CAPTURE_FRAMES: {
      if (args.size() > 1) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Accepts at most one arg";
        return ZX_ERR_INVALID_ARGS;
      }
      controller->CaptureFrames(
          args.size() == 0 ? "" : args[0],
          [success_cb = std::move(exit_on_success_cb), failure_cb = std::move(exit_on_failure_cb)](
              zx_status_t capture_frames_status, fuchsia::images::ImageInfo info) {
            if (capture_frames_status != ZX_OK) {
              failure_cb(capture_frames_status, std::string{kCommand2});
              return;
            }
            success_cb(std::string{kCommand2});
          });
      break;
    }
    case GET_OTP_DATA:
      isp->GetOtpData(
          [success_cb = std::move(exit_on_success_cb), failure_cb = std::move(exit_on_failure_cb)](
              zx_status_t get_otp_status, size_t byte_count, zx::vmo otp_data) {
            if (get_otp_status != ZX_OK) {
              failure_cb(get_otp_status, std::string{kCommand4});
              return;
            }
            uint8_t buf[byte_count];
            otp_data.read(buf, 0, byte_count);
            for (auto byte : buf) {
              std::cout << byte;
            }
            success_cb(std::string{kCommand4});
          });
      break;
    case GET_SENSOR_TEMPERATURE:
      isp->GetSensorTemperature(
          [success_cb = std::move(exit_on_success_cb), failure_cb = std::move(exit_on_failure_cb)](
              zx_status_t get_sensor_temperature_status, int32_t temperature) {
            if (get_sensor_temperature_status != ZX_OK) {
              failure_cb(get_sensor_temperature_status, std::string{kCommand5});
              return;
            }
            success_cb(std::string{kCommand10});
          });
      break;
    case SET_AWB_MODE: {
      if (args.size() != 2) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Accepts exactly two args";
        return ZX_ERR_INVALID_ARGS;
      }
      isp->SetAWBMode(static_cast<fuchsia::factory::camera::WhiteBalanceMode>(std::stoi(args[0])),
                      std::stoi(args[1]), [success_cb = std::move(exit_on_success_cb)]() {
                        success_cb(std::string{kCommand6});
                      });
      break;
    }
    case SET_AE_MODE:
      if (args.size() != 1) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Accepts exactly one arg";
        return ZX_ERR_INVALID_ARGS;
      }
      isp->SetAEMode(
          static_cast<fuchsia::factory::camera::ExposureMode>(std::stoi(args[0])),
          [success_cb = std::move(exit_on_success_cb)]() { success_cb(std::string{kCommand7}); });
      break;
    case SET_EXPOSURE: {
      if (args.size() != 3) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Accepts exactly three args";
        return ZX_ERR_INVALID_ARGS;
      }
      isp->SetExposure(
          std::stof(args[0]), std::stof(args[1]), std::stof(args[2]),
          [success_cb = std::move(exit_on_success_cb)]() { success_cb(std::string{kCommand8}); });
      break;
    }
    case SET_SENSOR_MODE:
      if (args.size() != 1) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Accepts exactly one arg";
        return ZX_ERR_INVALID_ARGS;
      }
      isp->SetSensorMode(
          static_cast<uint16_t>(std::stoul(args[0])),
          [success_cb = std::move(exit_on_success_cb)]() { success_cb(std::string{kCommand9}); });
      break;
    case SET_TEST_PATTERN_MODE:
      if (args.size() != 1) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Accepts exactly one arg";
        return ZX_ERR_INVALID_ARGS;
      }
      isp->SetTestPatternMode(
          static_cast<uint16_t>(std::stoul(args[0])),
          [success_cb = std::move(exit_on_success_cb)]() { success_cb(std::string{kCommand10}); });
      break;
    case SET_BYPASS_MODE:
      if (args.size() != 1) {
        FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Accepts exactly one arg";
        return ZX_ERR_INVALID_ARGS;
      }
      isp->SetBypassMode(
          static_cast<uint16_t>(std::stoul(args[0])),
          [success_cb = std::move(exit_on_success_cb)]() { success_cb(std::string{kCommand11}); });
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

int main(int argc, char* argv[]) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line, {"camera-factory-cli"})) {
    FX_LOGS(ERROR);
    return EXIT_FAILURE;
  }

  if (!command_line.HasOption(kFlag)) {
    FX_LOGS(ERROR) << "User must specify 'command' flag.";
    return EXIT_FAILURE;
  }

  std::string command;
  command_line.GetOptionValue(kFlag, &command);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto exit_on_success_cb = [&loop](const std::string& command) {
    FX_PLOGS(INFO, ZX_OK) << "camera-factory-cli ran with command '" << command << "'";
    loop.Quit();
  };
  auto exit_on_failure_cb = [&loop](zx_status_t status, const std::string& command) {
    FX_PLOGS(INFO, status) << "camera-factory-cli failed with command '" << command << "'.";
    loop.Quit();
  };
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  zx_status_t status;
  // Parse command.
  auto command_result = StrToCommand(command);
  if (command_result.is_error()) {
    FX_LOGS(ERROR) << "Command not valid for specified target.";
    return EXIT_FAILURE;
  }

  // Connect to the camera-factory Controller.
  fuchsia::factory::camera::ControllerHandle controller_handle;
  status = context->svc()->Connect(controller_handle.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request camera-factory Controller service.";
    return EXIT_FAILURE;
  }

  fuchsia::factory::camera::ControllerPtr controller;
  status = controller.Bind(std::move(controller_handle), loop.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get camera-factory Controller pointer.";
    return EXIT_FAILURE;
  }

  // Connect to the ISP.
  int result = open(kDevicePath.data(), O_RDONLY);
  if (result < 0) {
    FX_LOGS(ERROR) << "Error opening device at " << kDevicePath;
    return EXIT_FAILURE;
  }
  fbl::unique_fd fd;
  fd.reset(result);

  zx::channel channel;
  status = fdio_get_service_handle(fd.get(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get handle for device at " << kDevicePath;
    return EXIT_FAILURE;
  }

  fuchsia::factory::camera::IspPtr isp;
  status = isp.Bind(std::move(channel), loop.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get camera-factory Controller pointer.";
    return EXIT_FAILURE;
  }

  // Error handling
  auto shutdown_cb = [&controller, &isp]() {
    controller = nullptr;
    isp = nullptr;
  };

  controller.set_error_handler([&loop, &shutdown_cb](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "camera-factory-cli Controller channel closing due to error.";
    shutdown_cb();
    loop.Quit();
  });

  isp.set_error_handler([&loop, &shutdown_cb](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "camera-factory-cli Isp channel closing due to error.";
    shutdown_cb();
    loop.Quit();
  });

  // Execute command.
  status = RunCommand(controller, isp, command_result.value(), command_line.positional_args(),
                      exit_on_success_cb, exit_on_failure_cb);

  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "camera-factory-cli loop never started";
    shutdown_cb();
    return EXIT_FAILURE;
  }

  // Wait for exit callback.
  FX_LOGS(INFO) << "camera-factory-cli loop started.";
  loop.Run();
  FX_LOGS(INFO) << "camera-factory-cli loop quit after '" << command << "' ran.";

  shutdown_cb();

  return EXIT_SUCCESS;
}
