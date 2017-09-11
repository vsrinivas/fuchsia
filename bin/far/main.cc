// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "application/lib/far/archive_reader.h"
#include "application/lib/far/archive_writer.h"
#include "application/lib/far/manifest.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/unique_fd.h"

namespace archive {

// Commands
constexpr ftl::StringView kCat = "cat";
constexpr ftl::StringView kCreate = "create";
constexpr ftl::StringView kList = "list";
constexpr ftl::StringView kExtract = "extract";
constexpr ftl::StringView kExtractFile = "extract-file";

constexpr ftl::StringView kKnownCommands =
    "create, list, cat, extract, or extract-file";

// Options
constexpr ftl::StringView kArchive = "archive";
constexpr ftl::StringView kManifest = "manifest";
constexpr ftl::StringView kFile = "file";
constexpr ftl::StringView kOuput = "output";

constexpr ftl::StringView kCatUsage = "cat --archive=<archive> --file=<path> ";
constexpr ftl::StringView kCreateUsage =
    "create --archive=<archive> --manifest=<manifest>";
constexpr ftl::StringView kListUsage = "list --archive=<archive>";
constexpr ftl::StringView kExtractUsage =
    "extract --archive=<archive> --output=<path>";
constexpr ftl::StringView kExtractFileUsage =
    "extract-file --archive=<archive> --file=<path> --output=<path>";

bool GetOptionValue(const ftl::CommandLine& command_line,
                    ftl::StringView option,
                    ftl::StringView usage,
                    std::string* value) {
  if (!command_line.GetOptionValue(option, value)) {
    fprintf(stderr,
            "error: Missing --%s argument.\n"
            "Usuage: far %s\n",
            option.data(), usage.data());
    return false;
  }
  return true;
}

int Create(const ftl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kCreateUsage, &archive_path))
    return -1;

  std::vector<ftl::StringView> manifest_paths =
      command_line.GetOptionValues(kManifest);
  if (manifest_paths.empty())
    return -1;

  archive::ArchiveWriter writer;
  for (const auto& manifest_path : manifest_paths) {
    if (!archive::ReadManifest(manifest_path, &writer))
      return -1;
  }
  ftl::UniqueFD fd(open(archive_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
  if (!fd.is_valid())
    return -1;
  return writer.Write(fd.get()) ? 0 : -1;
}

int List(const ftl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kListUsage, &archive_path))
    return -1;

  ftl::UniqueFD fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid())
    return -1;
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  reader.ListPaths([](ftl::StringView string) {
    printf("%.*s\n", static_cast<int>(string.size()), string.data());
  });
  return 0;
}

int Extract(const ftl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kExtractUsage, &archive_path))
    return -1;

  std::string output_dir;
  if (!GetOptionValue(command_line, kOuput, kExtractUsage, &output_dir))
    return -1;

  ftl::UniqueFD fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid())
    return -1;
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  if (!reader.Extract(output_dir))
    return -1;
  return 0;
}

int ExtractFile(const ftl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kExtractFileUsage, &archive_path))
    return -1;

  std::string file_path;
  if (!GetOptionValue(command_line, kFile, kExtractFileUsage, &file_path))
    return -1;

  std::string output_path;
  if (!GetOptionValue(command_line, kOuput, kExtractFileUsage, &output_path))
    return -1;

  ftl::UniqueFD fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid())
    return -1;
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  if (!reader.ExtractFile(file_path, output_path.c_str()))
    return -1;
  return 0;
}

int Cat(const ftl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kCatUsage, &archive_path))
    return -1;

  std::string file_path;
  if (!GetOptionValue(command_line, kFile, kCatUsage, &file_path))
    return -1;

  ftl::UniqueFD fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid())
    return -1;
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  if (!reader.CopyFile(file_path, STDOUT_FILENO))
    return -1;
  return 0;
}

int RunCommand(std::string command, const ftl::CommandLine& command_line) {
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
            "Usuage: far <command> ...\n"
            "  where <command> is %s.\n",
            archive::kKnownCommands.data());
    return -1;
  }

  return archive::RunCommand(argv[1],
                             ftl::CommandLineFromArgcArgv(argc - 1, argv + 1));
}
