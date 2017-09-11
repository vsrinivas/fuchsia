// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/socket/files.h"

#include <fcntl.h>
#include <mx/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gtest/gtest.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mtl {
namespace {

TEST(SocketAndFile, CopyToFileDescriptor) {
  files::ScopedTempDir tmp_dir;
  std::string tmp_file;
  tmp_dir.NewTempFile(&tmp_file);
  MessageLoop message_loop;

  fxl::UniqueFD destination(open(tmp_file.c_str(), O_WRONLY));
  EXPECT_TRUE(destination.is_valid());

  bool success;
  CopyToFileDescriptor(
      mtl::WriteStringToSocket("Hello"), std::move(destination),
      message_loop.task_runner(),
      [&message_loop, &success](bool success_value, fxl::UniqueFD fd) {
        success = success_value;
        message_loop.PostQuitTask();
      });
  message_loop.Run();

  EXPECT_TRUE(success);
  std::string content;
  EXPECT_TRUE(files::ReadFileToString(tmp_file, &content));
  EXPECT_EQ("Hello", content);
}

TEST(SocketAndFile, CopyFromFileDescriptor) {
  files::ScopedTempDir tmp_dir;
  std::string tmp_file;
  tmp_dir.NewTempFile(&tmp_file);
  MessageLoop message_loop;

  files::WriteFile(tmp_file, "Hello", 5);
  fxl::UniqueFD source(open(tmp_file.c_str(), O_RDONLY));
  EXPECT_TRUE(source.is_valid());

  mx::socket socket1, socket2;
  EXPECT_EQ(MX_OK, mx::socket::create(0u, &socket1, &socket2));

  bool success;
  CopyFromFileDescriptor(
      std::move(source), std::move(socket1), message_loop.task_runner(),
      [&message_loop, &success](bool success_value, fxl::UniqueFD fd) {
        success = success_value;
        message_loop.PostQuitTask();
      });
  message_loop.Run();

  EXPECT_TRUE(success);
  std::string content;
  EXPECT_TRUE(mtl::BlockingCopyToString(std::move(socket2), &content));
  EXPECT_EQ("Hello", content);
}

}  // namespace
}  // namespace mtl
