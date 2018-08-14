// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/fxl/command_line.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/substitute.h>

#include "connect.h"
#include "find.h"
#include "formatter.h"
#include "formatters/text.h"

struct Options {
  enum Mode {
    UNSET,
    CAT,
    FIND,
    LS,
  };
  Mode mode = UNSET;
  bool full_paths = false;
  bool absolute_paths = false;

  std::vector<std::string> paths;
  std::unique_ptr<iquery::Formatter> formatter;

  Options(const fxl::CommandLine& command_line) {
    if (command_line.HasOption("cat") && !SetMode(command_line, CAT))
      return;
    else if (command_line.HasOption("find") && !SetMode(command_line, FIND))
      return;
    else if (command_line.HasOption("ls") && !SetMode(command_line, LS))
      return;
    else if (mode == UNSET)
      SetMode(command_line, CAT);

    // TODO(crjohns): Add more formatters.
    formatter = std::make_unique<iquery::TextFormatter>();

    full_paths = command_line.HasOption("full_paths");
    absolute_paths = command_line.HasOption("absolute_paths");

    std::copy(command_line.positional_args().begin(),
              command_line.positional_args().end(), std::back_inserter(paths));
  }

  void Usage(const std::string& argv0) {
    std::cout << fxl::Substitute(
        R"txt(Usage: $0 (--cat|--find|--ls) [--full_paths] PATH [...PATH]
  Utility for querying exposed object directories.

  Mode options:
  --cat:  Print the data for the object(s) given by PATH. (DEFAULT)
  --find: Recursively find all objects under PATH.
  --ls:   List the children of the object(s) given by PATH.

  --full_paths:     Include the full path in object names.
  --absolute_paths: Include full absolute path in objectnames.
                    Overrides --full_paths.
)txt",
        argv0);
  }

  bool Valid() { return valid_; }

 private:
  bool SetMode(const fxl::CommandLine& command_line, Mode m) {
    if (mode != UNSET) {
      Invalid(command_line.argv0(), "multiple modes specified");
      return false;
    }
    mode = m;
    return true;
  }

  void Invalid(const std::string& argv0, std::string reason) {
    std::cerr << "Invalid command line args: " << reason << std::endl;
    Usage(argv0);
    valid_ = false;
  }
  bool valid_ = true;
};

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  Options options(command_line);

  auto FormatPath = [&options](const std::string& path,
                               fuchsia::inspect::Object* object) {
    if (options.absolute_paths) {
      object->name = files::AbsolutePath(path);
    } else if (options.full_paths) {
      object->name = path;
    }
  };

  if (!options.Valid()) {
    return 1;
  }

  if (command_line.HasOption("help") || options.paths.size() == 0) {
    options.Usage(command_line.argv0());
    return 0;
  }

  if (options.mode == Options::CAT) {
    std::vector<fuchsia::inspect::Object> objects;
    for (const auto& path : options.paths) {
      iquery::Connection connection(path);
      auto ptr = connection.SyncOpen();
      if (!ptr) {
        FXL_LOG(ERROR) << "Failed opening " << path;
        return 1;
      }
      auto it = objects.emplace(objects.end());
      FXL_VLOG(1) << "Reading " << path;
      auto status = ptr->ReadData(&(*it));
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed reading " << path;
        FXL_VLOG(1) << "Status " << status;
        return 1;
      }
      FXL_VLOG(1) << "Reading done for " << path;
      FormatPath(path, &(*it));
    }
    std::cout << options.formatter->FormatCat(objects);
  } else if (options.mode == Options::FIND) {
    std::vector<std::string> objects;
    for (const auto& path : options.paths) {
      if (!iquery::FindObjects(path, &objects)) {
        FXL_LOG(WARNING) << "Failed searching " << path;
      }
    }
    std::cout << options.formatter->FormatFind(objects);
  } else if (options.mode == Options::LS) {
    std::vector<fuchsia::inspect::Object> children;
    for (const auto& path : options.paths) {
      iquery::Connection connection(path);
      auto ptr = connection.SyncOpen();
      if (!ptr) {
        FXL_LOG(ERROR) << "Failed listing " << path;
        return 1;
      }

      ::fidl::VectorPtr<::fidl::StringPtr> result;
      auto status = ptr->ListChildren(&result);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed listing children for " << path;
        FXL_VLOG(1) << "Status " << status;
        return 1;
      }

      for (const auto& name : *result) {
        fuchsia::inspect::Object obj({.name = name});
        FormatPath(path, &obj);
        children.emplace_back(std::move(obj));
      }
    }
    std::cout << options.formatter->FormatLs(children);
  }

  return 0;
}
