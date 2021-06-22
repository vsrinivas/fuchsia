// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/extractor/bin/parse.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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

  // Common arguments for both extract and deflate.
  char command[kLength] = "command";
  char sub_command[kLength] = "extract";
  char input_file[kLength] = "/tmp/extract-input.XXXXXX";
  char output_file[kLength] = "/tmp/extract-output.XXXXXX";
  char help[kLength] = "--help";

  // Arguments applicable only for 'extract' subcommand.
  char type[kLength] = "--type";
  char type_arg[kLength] = "minfs";
  char disk[kLength] = "--disk";
  char image[kLength] = "--image";
  char extra[kLength] = "--extra";
  char pii_dump[kLength] = "--dump-pii";

  // Arguments applicable only for 'deflate' subcommand.
  char input[kLength] = "--input_file";
  char output[kLength] = "--output_file";
  char verbose[kLength] = "--verbose";
};

TestArguments setup(bool create_disk, bool create_image_file,
                    SubCommand sub_command = SubCommand::kExtract) {
  TestArguments args;

  if (sub_command == SubCommand::kDeflate) {
    strcpy(args.sub_command, "deflate");
  }

  if (create_image_file) {
    fbl::unique_fd input_fd(mkostemp(args.output_file, O_RDONLY | O_CREAT | O_EXCL));
    EXPECT_TRUE(input_fd);
  }

  if (!create_disk) {
    return args;
  }

  fbl::unique_fd output_fd(mkostemp(args.input_file, O_RDWR | O_CREAT | O_EXCL));
  EXPECT_TRUE(output_fd);
  return args;
}

TEST(Parse, NoArgument) {
  auto args = setup(/*create_disk=*/false, /*create_image_file=*/false);
  std::vector<char*> argv{args.command};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, ExtractAllArgument) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command,  args.sub_command, args.type,  args.type_arg,
                          args.disk,     args.input_file,  args.image, args.output_file,
                          args.pii_dump, args.help};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, ExtractMissingType) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command, args.sub_command, args.disk,    args.input_file,
                          args.image,   args.output_file, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, ExtractInvalidType) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  strncpy(args.type_arg, "njgenkgnaw", sizeof(args.type_arg));
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command,  args.sub_command, args.type,
                          args.type_arg, args.disk,        args.input_file,
                          args.image,    args.output_file, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, ExtractMissingDisk) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command, args.sub_command, args.type,    args.type_arg,
                          args.image,   args.output_file, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, ExtractMissingImage) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/false);
  std::vector<char*> argv{args.command, args.sub_command, args.type,    args.type_arg,
                          args.disk,    args.input_file,  args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, ExtractDiskDoesNotExists) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.input_file), 0);
  std::vector<char*> argv{args.command,  args.sub_command, args.type,
                          args.type_arg, args.disk,        args.input_file,
                          args.image,    args.output_file, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_IO);
}

TEST(Parse, ExtractImageFileAlreadyExists) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  std::vector<char*> argv{args.command,  args.sub_command, args.type,
                          args.type_arg, args.disk,        args.input_file,
                          args.image,    args.output_file, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_ALREADY_EXISTS);
}

TEST(Parse, ExtractFailureToCreateImageFile) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  auto len = strlen(args.output_file);
  args.output_file[len] = '/';
  args.output_file[len + 1] = '\0';
  std::vector<char*> argv{args.command,  args.sub_command, args.type,
                          args.type_arg, args.disk,        args.input_file,
                          args.image,    args.output_file, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_IO);
}

TEST(Parse, ExtractExtraArgument) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command,  args.sub_command, args.type,  args.type_arg,
                          args.disk,     args.input_file,  args.image, args.output_file,
                          args.pii_dump, args.extra};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, ExtractDumpPii) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command,  args.sub_command, args.type,
                          args.type_arg, args.disk,        args.input_file,
                          args.image,    args.output_file, args.pii_dump};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).value().dump_pii, true);
}

TEST(Parse, ExtractDontDumpPii) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command, args.sub_command, args.type,  args.type_arg,
                          args.disk,    args.input_file,  args.image, args.output_file};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).value().dump_pii, false);
}

TEST(Parse, DeflateOnlyOneArg) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true, SubCommand::kDeflate);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command, args.sub_command, args.output, args.output_file};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(Parse, DeflateDiskDoesNotExists) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true, SubCommand::kDeflate);
  EXPECT_EQ(remove(args.input_file), 0);
  std::vector<char*> argv{args.command,    args.sub_command, args.input,
                          args.input_file, args.output,      args.output_file};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_IO);
}

TEST(Parse, DeflateImageFileAlreadyExists) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true, SubCommand::kDeflate);
  std::vector<char*> argv{args.command,    args.sub_command, args.input,
                          args.input_file, args.output,      args.output_file};
  ASSERT_EQ(extractor::ParseCommandLineArguments(argv.size(), argv.data()).error_value(),
            ZX_ERR_ALREADY_EXISTS);
}

TEST(Parse, DeflateValidArguments) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true, SubCommand::kDeflate);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command,    args.sub_command, args.input,
                          args.input_file, args.output,      args.output_file};
  auto opts = extractor::ParseCommandLineArguments(argv.size(), argv.data()).value();
  ASSERT_EQ(opts.sub_command, SubCommand::kDeflate);
  ASSERT_EQ(opts.verbose, false);
}

TEST(Parse, DeflateValidArgumentsWithVerbose) {
  auto args = setup(/*create_disk=*/true, /*create_image_file=*/true, SubCommand::kDeflate);
  EXPECT_EQ(remove(args.output_file), 0);
  std::vector<char*> argv{args.command, args.sub_command, args.input,  args.input_file,
                          args.output,  args.output_file, args.verbose};
  auto opts = extractor::ParseCommandLineArguments(argv.size(), argv.data()).value();
  ASSERT_EQ(opts.sub_command, SubCommand::kDeflate);
  ASSERT_EQ(opts.verbose, true);
}

}  // namespace

}  // namespace extractor
