// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/flog_service/flog_directory.h"

#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

#include <ctime>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/eintr_wrapper.h"
#include "lib/fxl/files/path.h"

namespace flog {
namespace {

void SafeCloseDir(DIR* dir) {
  if (dir)
    closedir(dir);
}

bool ForEachEntry(const std::string& path,
                  std::function<bool(const std::string& path)> callback) {
  std::unique_ptr<DIR, decltype(&SafeCloseDir)> dir(opendir(path.c_str()),
                                                    SafeCloseDir);
  if (!dir.get())
    return false;
  for (struct dirent* entry = readdir(dir.get()); entry != nullptr;
       entry = readdir(dir.get())) {
    char* name = entry->d_name;
    if (name[0]) {
      if (name[0] == '.') {
        if (!name[1] || (name[1] == '.' && !name[2])) {
          // . or ..
          continue;
        }
      }
      if (!callback(path + "/" + name))
        return false;
    }
  }
  return true;
}

}  // namespace

// static
const std::string FlogDirectory::kDirName = "/app_local/flog_viewer";

FlogDirectory::FlogDirectory() {
  bool result = files::CreateDirectory(kDirName);
  FXL_DCHECK(result) << "Failed to create directory " << kDirName;
}

FlogDirectory::~FlogDirectory() {}

void FlogDirectory::GetExistingFiles(GetExistingFilesCallback callback) {
  auto labels_by_id = std::unique_ptr<std::map<uint32_t, std::string>>(
      new std::map<uint32_t, std::string>);

  ForEachEntry(kDirName, [this, &labels_by_id](const std::string& path) {
    if (!files::IsDirectory(path)) {
      uint32_t id;
      std::string label;
      if (ParseLogFilePath(path, &id, &label)) {
        labels_by_id->insert(std::pair<uint32_t, std::string>(id, label));
      }
    }

    return true;
  });

  callback(std::move(labels_by_id));
}

fxl::UniqueFD FlogDirectory::GetFile(uint32_t id,
                                     const std::string& label,
                                     bool create) {
  std::string path = LogFilePath(id, label);
  if (create) {
    return fxl::UniqueFD(HANDLE_EINTR(creat(path.c_str(), 0644)));
  } else {
    return fxl::UniqueFD(open(path.c_str(), O_RDONLY));
  }
}

void FlogDirectory::DeleteFile(uint32_t id, const std::string& label) {
  files::DeletePath(LogFilePath(id, label), false);
}

std::string FlogDirectory::LogFilePath(uint32_t id, const std::string& label) {
  // Format is "<id>_<label>.flog" where <id> is the kLogIdWidth-digit,
  // zero-padded info.id_ and <label> is the label.
  std::ostringstream file_path_stream;
  file_path_stream << kDirName << "/" << std::setfill('0')
                   << std::setw(kLogIdWidth) << id << "_" << label << ".flog";
  return file_path_stream.str();
}

bool FlogDirectory::ParseLogFilePath(const std::string& path,
                                     uint32_t* id_out,
                                     std::string* label_out) {
  FXL_DCHECK(id_out != nullptr);
  FXL_DCHECK(label_out != nullptr);

  size_t separator = path.rfind('/');
  std::string name = path.substr(separator + 1);

  if (name.size() < kLogIdWidth + 2) {
    return false;
  }

  for (size_t i = 0; i < kLogIdWidth; i++) {
    if (!isdigit(name[i])) {
      return false;
    }
  }

  if (name[kLogIdWidth] != '_') {
    return false;
  }

  size_t after_label = name.find_first_of('.', kLogIdWidth + 1);
  if (after_label == std::string::npos) {
    return false;
  }

  *id_out = std::stoul(name);
  *label_out = name.substr(kLogIdWidth + 1, after_label - (kLogIdWidth + 1));

  return true;
}

}  // namespace flog
