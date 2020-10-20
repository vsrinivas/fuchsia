// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/extractor/bin/parse.h"

#include <fcntl.h>
#include <stdio.h>
#include <zircon/errors.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace extractor {

namespace {

struct TestArguments {
  static constexpr uint8_t kLength = 64;
  char command[kLength] = "command";
  char type[kLength] = "--type";
  char type_arg[kLength] = "minfs";
  char disk[kLength] = "--disk";
  char disk_arg[kLength] = "/tmp/extract-input.XXXXXX";
  char image[kLength] = "--image";
  char image_arg[kLength] = "/tmp/extract-output.XXXXXX";
  char pii_dump[kLength] = "--dump-pii";
  char help[kLength] = "--help";
  char extra[kLength] = "--extra";
};

TestArguments setup(bool create_disk, bool create_image_file) {
  TestArguments args;
  if (create_image_file) {
    fbl::unique_fd input_fd(mkostemp(args.image_arg, O_RDONLY | O_CREAT | O_EXCL));
    EXPECT_TRUE(input_fd);
  }

  if (!create_disk) {
    return args;
  }

  fbl::unique_fd output_fd(mkostemp(args.disk_arg, O_RDWR | O_CREAT | O_EXCL));
  EXPECT_TRUE(output_fd);
  return args;
}

TEST(Parse, NoArgument) {
  auto args = setup(/*create_disk=*/false, /*create_image_file=*/false);
  std::vector<char*> argv{args.command};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, AllArgument) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.image_arg), 0);
  std::vector<char*> argv{args.command, args.type,      args.type_arg, args.disk, args.disk_arg,
                          args.image,   args.image_arg, args.pii_dump, args.help};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, MissingType) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.image_arg), 0);
  std::vector<char*> argv{args.command, args.disk,      args.disk_arg,
                          args.image,   args.image_arg, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, InvalidType) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  strncpy(args.type_arg, "blobfs", sizeof(args.type_arg));
  EXPECT_EQ(remove(args.image_arg), 0);
  std::vector<char*> argv{args.command,  args.type,  args.type_arg,  args.disk,
                          args.disk_arg, args.image, args.image_arg, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, MissingDisk) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.image_arg), 0);
  std::vector<char*> argv{args.command, args.type,      args.type_arg,
                          args.image,   args.image_arg, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, MissingImage) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/false);
  std::vector<char*> argv{args.command, args.type,     args.type_arg,
                          args.disk,    args.disk_arg, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, DiskDoesNotExists) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.disk_arg), 0);
  std::vector<char*> argv{args.command,  args.type,  args.type_arg,  args.disk,
                          args.disk_arg, args.image, args.image_arg, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_IO);
}

TEST(Parse, ImageFileAlreadyExists) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  std::vector<char*> argv{args.command,  args.type,  args.type_arg,  args.disk,
                          args.disk_arg, args.image, args.image_arg, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_ALREADY_EXISTS);
}

TEST(Parse, FailureToCreateImageFile) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  auto len = strlen(args.image_arg);
  args.image_arg[len] = '/';
  args.image_arg[len + 1] = '\0';
  std::vector<char*> argv{args.command,  args.type,  args.type_arg,  args.disk,
                          args.disk_arg, args.image, args.image_arg, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_IO);
}

TEST(Parse, ExtraArgument) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.image_arg), 0);
  std::vector<char*> argv{args.command, args.type,      args.type_arg, args.disk, args.disk_arg,
                          args.image,   args.image_arg, args.pii_dump, args.extra};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, DumpPii) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.image_arg), 0);
  std::vector<char*> argv{args.command,  args.type,  args.type_arg,  args.disk,
                          args.disk_arg, args.image, args.image_arg, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).value().dump_pii, true);
}

TEST(Parse, DontDumpPii) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.image_arg), 0);
  std::vector<char*> argv{args.command,  args.type,  args.type_arg, args.disk,
                          args.disk_arg, args.image, args.image_arg};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).value().dump_pii, false);
}

}  // namespace

}  // namespace extractor
