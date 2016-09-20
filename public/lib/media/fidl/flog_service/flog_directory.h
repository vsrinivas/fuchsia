// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FLOG_SERVICE_FLOG_DIRECTORY_H_
#define APPS_MEDIA_SERVICES_FLOG_SERVICE_FLOG_DIRECTORY_H_

#include <map>

#include "lib/ftl/files/unique_fd.h"

namespace mojo {
namespace flog {

// Flog directory management.
class FlogDirectory {
 public:
  using GetExistingFilesCallback =
      std::function<void(std::unique_ptr<std::map<uint32_t, std::string>>)>;

  FlogDirectory(Shell* shell);

  ~FlogDirectory();

  // Calls back with a map (id -> label) of existing files.
  void GetExistingFiles(GetExistingFilesCallback callback);

  // Gets a FilePtr for the indicated file.
  files::FilePtr GetFile(uint32_t id, const std::string& label, bool create);

  // Deletes the indicated file.
  void DeleteFile(uint32_t id, const std::string& label);

 private:
  static const size_t kLogIdWidth = 8;

  // Returns a log file name given the id and label of the log.
  std::string LogFileName(uint32_t id, const std::string& label);

  // Parses a log file name.
  bool ParseLogFileName(const std::string& name,
                        uint32_t* id_out,
                        std::string* label_out);

  files::FilesPtr files_;
  files::DirectoryPtr file_system_;
};

}  // namespace flog
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FLOG_SERVICE_FLOG_DIRECTORY_H_
