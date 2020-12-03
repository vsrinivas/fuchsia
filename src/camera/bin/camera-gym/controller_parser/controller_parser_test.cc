// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/controller_parser/controller_parser.h"

#include <fuchsia/camera/gym/cpp/fidl.h>
#include <lib/fit/result.h>

#include <gtest/gtest.h>

namespace camera {

class ControllerParserTest : public testing::Test {
 public:
  fit::result<std::vector<fuchsia::camera::gym::Command>> Parse(int argc, const char* argv[]) {
    return controller_parser_->ParseArgcArgv(argc, argv);
  }
  void set_controller_parser(std::unique_ptr<ControllerParser> parser) {
    controller_parser_ = std::move(parser);
  }

 private:
  std::unique_ptr<ControllerParser> controller_parser_;
};

TEST_F(ControllerParserTest, TestSingleCommandPassCases) {
  fit::result<std::vector<fuchsia::camera::gym::Command>> result;
  set_controller_parser(std::make_unique<ControllerParser>("fake-app-name"));
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config=0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetConfig);
    auto& set_config = command.set_config();
    EXPECT_EQ(set_config.config_id, 0U);
    EXPECT_FALSE(set_config.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config=1", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetConfig);
    auto& set_config = command.set_config();
    EXPECT_EQ(set_config.config_id, 1U);
    EXPECT_FALSE(set_config.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config=2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetConfig);
    auto& set_config = command.set_config();
    EXPECT_EQ(set_config.config_id, 2U);
    EXPECT_FALSE(set_config.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config-async=0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetConfig);
    auto& set_config = command.set_config();
    EXPECT_EQ(set_config.config_id, 0U);
    EXPECT_TRUE(set_config.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config-async=1", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetConfig);
    auto& set_config = command.set_config();
    EXPECT_EQ(set_config.config_id, 1U);
    EXPECT_TRUE(set_config.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config-async=2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetConfig);
    auto& set_config = command.set_config();
    EXPECT_EQ(set_config.config_id, 2U);
    EXPECT_TRUE(set_config.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream=0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kAddStream);
    auto& add_stream = command.add_stream();
    EXPECT_EQ(add_stream.stream_id, 0U);
    EXPECT_FALSE(add_stream.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream=1", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kAddStream);
    auto& add_stream = command.add_stream();
    EXPECT_EQ(add_stream.stream_id, 1U);
    EXPECT_FALSE(add_stream.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream=2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kAddStream);
    auto& add_stream = command.add_stream();
    EXPECT_EQ(add_stream.stream_id, 2U);
    EXPECT_FALSE(add_stream.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream-async=0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kAddStream);
    auto& add_stream = command.add_stream();
    EXPECT_EQ(add_stream.stream_id, 0U);
    EXPECT_TRUE(add_stream.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream-async=1", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kAddStream);
    auto& add_stream = command.add_stream();
    EXPECT_EQ(add_stream.stream_id, 1U);
    EXPECT_TRUE(add_stream.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream-async=2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kAddStream);
    auto& add_stream = command.add_stream();
    EXPECT_EQ(add_stream.stream_id, 2U);
    EXPECT_TRUE(add_stream.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=1,0.0,0.0,0.0,0.0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetCrop);
    auto& set_crop = command.set_crop();
    EXPECT_EQ(set_crop.stream_id, 1U);
    EXPECT_EQ(set_crop.x, 0.0);
    EXPECT_EQ(set_crop.y, 0.0);
    EXPECT_EQ(set_crop.width, 0.0);
    EXPECT_EQ(set_crop.height, 0.0);
    EXPECT_FALSE(set_crop.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=2,0.0,0.0,0.125,0.875", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetCrop);
    auto& set_crop = command.set_crop();
    EXPECT_EQ(set_crop.stream_id, 2U);
    EXPECT_EQ(set_crop.x, 0.0);
    EXPECT_EQ(set_crop.y, 0.0);
    EXPECT_EQ(set_crop.width, 0.125);
    EXPECT_EQ(set_crop.height, 0.875);
    EXPECT_FALSE(set_crop.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=3,0.0,0.0,1.0,1.0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetCrop);
    auto& set_crop = command.set_crop();
    EXPECT_EQ(set_crop.stream_id, 3U);
    EXPECT_EQ(set_crop.x, 0.0);
    EXPECT_EQ(set_crop.y, 0.0);
    EXPECT_EQ(set_crop.width, 1.0);
    EXPECT_EQ(set_crop.height, 1.0);
    EXPECT_FALSE(set_crop.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=1,0.0,0.0,0.0,0.0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetCrop);
    auto& set_crop = command.set_crop();
    EXPECT_EQ(set_crop.stream_id, 1U);
    EXPECT_EQ(set_crop.x, 0.0);
    EXPECT_EQ(set_crop.y, 0.0);
    EXPECT_EQ(set_crop.width, 0.0);
    EXPECT_EQ(set_crop.height, 0.0);
    EXPECT_TRUE(set_crop.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=2,0.0,0.0,0.125,0.625", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetCrop);
    auto& set_crop = command.set_crop();
    EXPECT_EQ(set_crop.stream_id, 2U);
    EXPECT_EQ(set_crop.x, 0.0);
    EXPECT_EQ(set_crop.y, 0.0);
    EXPECT_EQ(set_crop.width, 0.125);
    EXPECT_EQ(set_crop.height, 0.625);
    EXPECT_TRUE(set_crop.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=3,0.0,0.0,1.0,1.0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetCrop);
    auto& set_crop = command.set_crop();
    EXPECT_EQ(set_crop.stream_id, 3U);
    EXPECT_EQ(set_crop.x, 0.0);
    EXPECT_EQ(set_crop.y, 0.0);
    EXPECT_EQ(set_crop.width, 1.0);
    EXPECT_EQ(set_crop.height, 1.0);
    EXPECT_TRUE(set_crop.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=4,0,0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetResolution);
    auto& set_resolution = command.set_resolution();
    EXPECT_EQ(set_resolution.stream_id, 4U);
    EXPECT_EQ(set_resolution.width, 0U);
    EXPECT_EQ(set_resolution.height, 0U);
    EXPECT_FALSE(set_resolution.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=4,1,2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetResolution);
    auto& set_resolution = command.set_resolution();
    EXPECT_EQ(set_resolution.stream_id, 4U);
    EXPECT_EQ(set_resolution.width, 1U);
    EXPECT_EQ(set_resolution.height, 2U);
    EXPECT_FALSE(set_resolution.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=4,4567,123", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetResolution);
    auto& set_resolution = command.set_resolution();
    EXPECT_EQ(set_resolution.stream_id, 4U);
    EXPECT_EQ(set_resolution.width, 4567U);
    EXPECT_EQ(set_resolution.height, 123U);
    EXPECT_FALSE(set_resolution.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=3,0,0", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetResolution);
    auto& set_resolution = command.set_resolution();
    EXPECT_EQ(set_resolution.stream_id, 3U);
    EXPECT_EQ(set_resolution.width, 0U);
    EXPECT_EQ(set_resolution.height, 0U);
    EXPECT_TRUE(set_resolution.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=3,1,2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetResolution);
    auto& set_resolution = command.set_resolution();
    EXPECT_EQ(set_resolution.stream_id, 3U);
    EXPECT_EQ(set_resolution.width, 1U);
    EXPECT_EQ(set_resolution.height, 2U);
    EXPECT_TRUE(set_resolution.async);
  }
  {
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=3,4567,123", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 1U);
    auto& command = commands[0];
    EXPECT_EQ(command.Which(), fuchsia::camera::gym::Command::Tag::kSetResolution);
    auto& set_resolution = command.set_resolution();
    EXPECT_EQ(set_resolution.stream_id, 3U);
    EXPECT_EQ(set_resolution.width, 4567U);
    EXPECT_EQ(set_resolution.height, 123U);
    EXPECT_TRUE(set_resolution.async);
  }
}

TEST_F(ControllerParserTest, TestSingleCommandFailCases) {
  fit::result<std::vector<fuchsia::camera::gym::Command>> result;
  set_controller_parser(std::make_unique<ControllerParser>("fake-app-name"));
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config=", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config=3x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config=x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Too many arguments.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config=9,8", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config-async=", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config-async=1x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config-async=x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Too many arguments.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-config-async=9,8", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream=", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream=2x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream=x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Too many arguments.
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream=9,8", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream-async=", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream-async=1x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream-async=e", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Too many arguments.
    const int argc = 2;
    const char* argv[] = {"fake", "--add-stream-async=9,8", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=8y", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=y", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=4,1.1", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=5,2.0,2.2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=6,2.0,2.2,3.3,", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Too many arguments.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop=7,2.0,2.2,3.3,4.4,5.5", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=3y", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=y", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=4,1.1", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=5,2.0,2.2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=6,2.0,2.2,3.3,", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Too many arguments.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-crop-async=7,2.0,2.2,3.3,4.4,5.5", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=4x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=1,6.", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=1,2", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=1,73,", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution=1,2.31,", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=5.x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Excess characters after argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=124i", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=x", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=14,2.12", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Missing argument.
    const int argc = 2;
    const char* argv[] = {"fake", "--set-resolution-async=91,301,", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
}

TEST_F(ControllerParserTest, TestMultipleCommandPassCases) {
  fit::result<std::vector<fuchsia::camera::gym::Command>> result;
  set_controller_parser(std::make_unique<ControllerParser>("fake-app-name"));
  {
    const int argc = 3;
    const char* argv[] = {"fake", "--set-config-async=234", "--add-stream=876", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 2U);

    auto& command0 = commands[0];
    EXPECT_EQ(command0.Which(), fuchsia::camera::gym::Command::Tag::kSetConfig);
    auto& set_config = command0.set_config();
    EXPECT_EQ(set_config.config_id, 234U);
    EXPECT_TRUE(set_config.async);

    auto& command1 = commands[1];
    EXPECT_EQ(command1.Which(), fuchsia::camera::gym::Command::Tag::kAddStream);
    auto& add_stream = command1.add_stream();
    EXPECT_EQ(add_stream.stream_id, 876U);
    EXPECT_FALSE(add_stream.async);
  }
  {
    const int argc = 4;
    const char* argv[] = {"fake", "--set-config-async=44", "--add-stream=55",
                          "--set-resolution=66,77,88", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_ok());
    auto commands = result.take_value();
    EXPECT_EQ(commands.size(), 3U);

    auto& command0 = commands[0];
    EXPECT_EQ(command0.Which(), fuchsia::camera::gym::Command::Tag::kSetConfig);
    auto& set_config = command0.set_config();
    EXPECT_EQ(set_config.config_id, 44U);
    EXPECT_TRUE(set_config.async);

    auto& command1 = commands[1];
    EXPECT_EQ(command1.Which(), fuchsia::camera::gym::Command::Tag::kAddStream);
    auto& add_stream = command1.add_stream();
    EXPECT_EQ(add_stream.stream_id, 55U);
    EXPECT_FALSE(add_stream.async);

    auto& command2 = commands[2];
    EXPECT_EQ(command2.Which(), fuchsia::camera::gym::Command::Tag::kSetResolution);
    auto& set_resolution = command2.set_resolution();
    EXPECT_EQ(set_resolution.stream_id, 66U);
    EXPECT_EQ(set_resolution.width, 77U);
    EXPECT_EQ(set_resolution.height, 88U);
    EXPECT_FALSE(set_resolution.async);
  }
}

TEST_F(ControllerParserTest, TestMultipleCommandFailCases) {
  fit::result<std::vector<fuchsia::camera::gym::Command>> result;
  set_controller_parser(std::make_unique<ControllerParser>("fake-app-name"));
  {
    // Should fail: Invalid character for argument
    const int argc = 3;
    const char* argv[] = {"fake", "--set-config-async=234", "--add-stream=xyz", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
  {
    // Should fail: Invalid character for argument.
    const int argc = 4;
    const char* argv[] = {"fake", "--set-config-async=234", "--add-stream=xyz",
                          "--set_resolution=54,321,987", nullptr};
    result = Parse(argc, argv);
    EXPECT_TRUE(result.is_error());
  }
}

}  // namespace camera
