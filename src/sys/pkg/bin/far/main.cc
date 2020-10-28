// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/unique_fd.h>

#include "src/lib/fxl/command_line.h"
#include "src/sys/pkg/lib/far/archive_reader.h"
#include "src/sys/pkg/lib/far/archive_writer.h"
#include "src/sys/pkg/lib/far/manifest.h"

namespace archive {

// Commands
constexpr std::string_view kCat = "cat";
constexpr std::string_view kCreate = "create";
constexpr std::string_view kList = "list";
constexpr std::string_view kExtract = "extract";
constexpr std::string_view kExtractFile = "extract-file";

constexpr std::string_view kKnownCommands = "create, list, cat, extract, or extract-file";

// Options
constexpr std::string_view kArchive = "archive";
constexpr std::string_view kManifest = "manifest";
constexpr std::string_view kFile = "file";
constexpr std::string_view kOutput = "output";

constexpr std::string_view kCatUsage = "cat --archive=<archive> --file=<path> ";
constexpr std::string_view kCreateUsage = "create --archive=<archive> --manifest=<manifest>";
constexpr std::string_view kListUsage = "list --archive=<archive>";
constexpr std::string_view kExtractUsage = "extract --archive=<archive> --output=<path>";
constexpr std::string_view kExtractFileUsage =
    "extract-file --archive=<archive> --file=<path> --output=<path>";

bool GetOptionValue(const fxl::CommandLine& command_line, std::string_view option,
                    std::string_view usage, std::string* value) {
  if (!command_line.GetOptionValue(option, value)) {
    fprintf(stderr,
            "error: Missing --%s argument.\n"
            "Usage: far %s\n",
            option.data(), usage.data());
    return false;
  }
  return true;
}

int Create(const fxl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kCreateUsage, &archive_path))
    return -1;

  std::vector<std::string_view> manifest_paths = command_line.GetOptionValues(kManifest);
  if (manifest_paths.empty())
    return -1;

  archive::ArchiveWriter writer;
  for (const auto& manifest_path : manifest_paths) {
    if (!archive::ReadManifest(manifest_path, &writer))
      return -1;
  }
  fbl::unique_fd fd(open(archive_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                         S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
  if (!fd.is_valid()) {
    fprintf(stderr, "error: unable to open file: %s\n", archive_path.c_str());
    return -1;
  }
  return writer.Write(fd.get()) ? 0 : -1;
}

int List(const fxl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kListUsage, &archive_path))
    return -1;

  fbl::unique_fd fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    fprintf(stderr, "error: unable to open file: %s\n", archive_path.c_str());
    return -1;
  }
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  reader.ListPaths([](std::string_view string) {
    printf("%.*s\n", static_cast<int>(string.size()), string.data());
  });
  return 0;
}

int Extract(const fxl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kExtractUsage, &archive_path))
    return -1;

  std::string output_dir;
  if (!GetOptionValue(command_line, kOutput, kExtractUsage, &output_dir))
    return -1;

  fbl::unique_fd fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    fprintf(stderr, "error: unable to open file: %s\n", archive_path.c_str());
    return -1;
  }
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  if (!reader.Extract(output_dir))
    return -1;
  return 0;
}

int ExtractFile(const fxl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kExtractFileUsage, &archive_path))
    return -1;

  std::string file_path;
  if (!GetOptionValue(command_line, kFile, kExtractFileUsage, &file_path))
    return -1;

  std::string output_path;
  if (!GetOptionValue(command_line, kOutput, kExtractFileUsage, &output_path))
    return -1;

  fbl::unique_fd fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    fprintf(stderr, "error: unable to open file: %s\n", archive_path.c_str());
    return -1;
  }
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  if (!reader.ExtractFile(file_path, output_path.c_str()))
    return -1;
  return 0;
}

int Cat(const fxl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kCatUsage, &archive_path))
    return -1;

  std::string file_path;
  if (!GetOptionValue(command_line, kFile, kCatUsage, &file_path))
    return -1;

  fbl::unique_fd fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    fprintf(stderr, "error: unable to open file: %s\n", archive_path.c_str());
    return -1;
  }
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  if (!reader.CopyFile(file_path, STDOUT_FILENO))
    return -1;
  return 0;
}

int RunCommand(std::string command, const fxl::CommandLine& command_line) {
  if (command == kCreate) {
    return archive::Create(command_line);
  } else if (command == kList) {
    return archive::List(command_line);
  } else if (command == kExtract) {
    return archive::Extract(command_line);
  } else if (command == kExtractFile) {
    return archive::ExtractFile(command_line);
  } else if (command == kCat) {
    return archive::Cat(command_line);
  } else {
    fprintf(stderr,
            "error: Unknown command: %s\n"
            "Known commands: %s.\n",
            command.c_str(), kKnownCommands.data());
    return -1;
  }
}

}  // namespace archive

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "error: Missing command.\n"
            "Usage: far <command> ...\n"
            "  where <command> is %s.\n",
            archive::kKnownCommands.data());
    return -1;
  }

  return archive::RunCommand(argv[1], fxl::CommandLineFromArgcArgv(argc - 1, argv + 1));
}
