// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/autocomplete.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <utility>

#include "src/developer/cmd/command.h"

namespace cmd {
namespace {

void GetNextToken(const std::string& line, size_t pos, size_t* start, size_t* end) {
  *start = line.find_first_not_of(Command::kWhitespace, pos);
  *end = line.find_first_of(Command::kWhitespace, *start);
}

void SearchDirectory(const std::string& directory, const std::string& entry_prefix,
                     fit::function<void(const char*)> callback) {
  DIR* dir = opendir(directory.c_str());
  if (dir == NULL) {
    return;
  }

  struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != NULL) {
    if (strncmp(entry_prefix.c_str(), entry->d_name, entry_prefix.length())) {
      continue;
    }
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
      continue;
    }
    // TODO: Consider returning only executable files.
    callback(entry->d_name);
  }

  closedir(dir);
}

};  // namespace

Autocomplete::Autocomplete(const std::string& line) {
  size_t pos = 0u;
  while (pos < line.size()) {
    size_t start = 0u;
    size_t end = 0u;
    GetNextToken(line, pos, &start, &end);
    if (start != std::string::npos && end != std::string::npos) {
      tokens_.push_back(line.substr(start, end - start));
      pos = end;
    } else {
      fragment_prefix_ = line.substr(0, start);
      if (start != std::string::npos) {
        fragment_ = line.substr(start, end - start);
      }
      break;
    }
  }
}

Autocomplete::~Autocomplete() = default;

void Autocomplete::AddCompletion(const std::string& completion) {
  completions_.push_back(fragment_prefix_ + completion);
}

void Autocomplete::CompleteAsPath() {
  size_t split = fragment_.rfind('/');
  if (split == std::string::npos) {
    CompleteAsDirectoryEntry(".");
  } else {
    std::string dirname = fragment_.substr(0, split + 1);
    std::string entry_prefix = fragment_.substr(split + 1);
    SearchDirectory(dirname, entry_prefix,
                    [this, &dirname](const char* entry) { AddCompletion(dirname + entry); });
  }
}

void Autocomplete::CompleteAsDirectoryEntry(const std::string& directory) {
  SearchDirectory(directory, fragment_, [this](const char* entry) { AddCompletion(entry); });
}

void Autocomplete::CompleteAsEnvironmentVariable() {
  if (fragment_.find('=') != std::string::npos) {
    return;
  }
  for (char** variable = environ; *variable; variable++) {
    if (!strncmp(fragment_.c_str(), *variable, fragment_.length())) {
      AddCompletion(std::string(*variable, strchr(*variable, '=') - *variable));
    }
  }
}

std::vector<std::string> Autocomplete::TakeCompletions() { return std::move(completions_); }

}  // namespace cmd
