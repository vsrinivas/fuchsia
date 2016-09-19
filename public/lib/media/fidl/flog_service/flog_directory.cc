// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctime>
#include <iomanip>
#include <sstream>

#include "base/logging.h"
#include "mojo/public/cpp/application/connect.h"
#include "services/flog/flog_directory.h"

namespace mojo {
namespace flog {

FlogDirectory::FlogDirectory(Shell* shell) {
  ConnectToService(shell, "mojo:files", GetProxy(&files_));
  files_->OpenFileSystem("app_persistent_cache", GetProxy(&file_system_),
                         [this](files::Error error) {
                           files_.reset();
                           if (error != files::Error::OK) {
                             DCHECK(false) << "Failed to open file system: "
                                           << error;
                             delete this;
                           }
                         });
}

FlogDirectory::~FlogDirectory() {}

void FlogDirectory::GetExistingFiles(GetExistingFilesCallback callback) {
  file_system_->Read([this, callback](files::Error error,
                                      Array<files::DirectoryEntryPtr> entries) {
    std::unique_ptr<std::map<uint32_t, std::string>> labels_by_id =
        std::unique_ptr<std::map<uint32_t, std::string>>(
            new std::map<uint32_t, std::string>);
    for (const files::DirectoryEntryPtr& entry : entries) {
      if (entry->type == files::FileType::REGULAR_FILE) {
        uint32_t id;
        std::string label;
        if (ParseLogFileName(entry->name, &id, &label)) {
          labels_by_id->insert(std::pair<uint32_t, std::string>(id, label));
        }
      }
    }

    callback(std::move(labels_by_id));
  });
}

files::FilePtr FlogDirectory::GetFile(uint32_t id,
                                      const std::string& label,
                                      bool create) {
  files::FilePtr file;
  file_system_->OpenFile(
      LogFileName(id, label), GetProxy(&file),
      files::kOpenFlagRead |
          (create ? (files::kOpenFlagWrite | files::kOpenFlagCreate) : 0),
      [this](files::Error error) {
        if (error != files::Error::OK) {
          DCHECK(false) << "Failed to OpenFile" << error;
          // TODO: Fail.
          return;
        }
      });
  file_system_.WaitForIncomingResponse();
  return file.Pass();
}

void FlogDirectory::DeleteFile(uint32_t id, const std::string& label) {
  file_system_->Delete(LogFileName(id, label), files::kDeleteFlagFileOnly,
                       [this](files::Error error) {
                         if (error != files::Error::OK) {
                           DCHECK(false) << "Failed to Delete" << error;
                           // TODO: Fail.
                           return;
                         }
                       });
  file_system_.WaitForIncomingResponse();
}

std::string FlogDirectory::LogFileName(uint32_t id, const std::string& label) {
  // Format is "<id>_<label>.flog" where <id> is the kLogIdWidth-digit,
  // zero-padded info.id_ and <label> is the label.
  std::ostringstream file_name_stream;
  file_name_stream << std::setfill('0') << std::setw(kLogIdWidth) << id << "_"
                   << label << ".flog";
  return file_name_stream.str();
}

bool FlogDirectory::ParseLogFileName(const std::string& name,
                                     uint32_t* id_out,
                                     std::string* label_out) {
  DCHECK(id_out != nullptr);
  DCHECK(label_out != nullptr);

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
}  // namespace mojo
