// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "application/src/archiver/archive_reader.h"
#include "application/src/archiver/archive_writer.h"
#include "application/src/archiver/manifest.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/unique_fd.h"

namespace archive {

// Commands
constexpr char kCreate[] = "create";
constexpr char kList[] = "list";
constexpr char kExtractFile[] = "extract-file";

constexpr char kKnownCommands[] = "create, list, or extract-file";

// Options
constexpr char kArchive[] = "archive";
constexpr char kManifest[] = "manifest";
constexpr char kFile[] = "file";
constexpr char kOuput[] = "output";

constexpr char kCreateUsuage[] =
    "create --archive=<archive> --manifest=<manifest>";
constexpr char kListUsuage[] = "list --archive=<archive>";
constexpr char kExtractFileUsuage[] =
    "extract-file --archive=<archive> --file=<path> --output=<path>";

bool GetOptionValue(const ftl::CommandLine& command_line,
                    const char* option,
                    const char* usage,
                    std::string* value) {
  if (!command_line.GetOptionValue(option, value)) {
    fprintf(stderr,
            "error: Missing --%s argument.\n"
            "Usuage: %s %s\n",
            option, command_line.argv0().c_str(), usage);
    return false;
  }
  return true;
}

int Create(const ftl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kCreateUsuage, &archive_path))
    return -1;

  std::string manifest_path;
  if (!GetOptionValue(command_line, kManifest, kCreateUsuage, &manifest_path))
    return -1;

  archive::ArchiveWriter writer;
  if (!archive::ReadManifest(manifest_path.c_str(), &writer))
    return -1;
  ftl::UniqueFD fd(open(archive_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
  if (!fd.is_valid())
    return -1;
  return writer.Write(fd.get()) ? 0 : -1;
}

int List(const ftl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kListUsuage, &archive_path))
    return -1;

  ftl::UniqueFD fd(open(archive_path.c_str(), O_RDONLY));
  if (!fd.is_valid())
    return -1;
  archive::ArchiveReader reader(std::move(fd));
  if (!reader.Read())
    return -1;
  reader.ListDirectory([](ftl::StringView string) {
    printf("%.*s\n", static_cast<int>(string.size()), string.data());
  });
  return 0;
}

int ExtractFile(const ftl::CommandLine& command_line) {
  std::string archive_path;
  if (!GetOptionValue(command_line, kArchive, kExtractFileUsuage,
                      &archive_path))
    return -1;

  std::string file_path;
  if (!GetOptionValue(command_line, kFile, kExtractFileUsuage, &file_path))
    return -1;

  std::string output_path;
  if (!GetOptionValue(command_line, kOuput, kExtractFileUsuage, &output_path))
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

int RunCommand(const ftl::CommandLine& command_line) {
  if (command_line.positional_args().empty()) {
    fprintf(stderr,
            "error: Missing command.\n"
            "Usuage: %s <command> ...\n"
            "  where <command> is %s.\n",
            command_line.argv0().c_str(), kKnownCommands);
    return -1;
  }

  const std::string& command = command_line.positional_args().front();
  if (command == kCreate) {
    return archive::Create(command_line);
  } else if (command == kList) {
    return archive::Create(command_line);
  } else if (command == kExtractFile) {
    return archive::ExtractFile(command_line);
  } else {
    fprintf(stderr,
            "error: Unknown command: %s\n"
            "Known commands: %s.\n",
            command.c_str(), kKnownCommands);
    return -1;
  }
}

}  // namespace archive

int main(int argc, char** argv) {
  return archive::RunCommand(ftl::CommandLineFromArgcArgv(argc, argv));
}
