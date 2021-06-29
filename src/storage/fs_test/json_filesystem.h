// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_JSON_FILESYSTEM_H_
#define SRC_STORAGE_FS_TEST_JSON_FILESYSTEM_H_

#include <rapidjson/document.h>

#include "src/storage/fs_test/fs_test.h"

namespace fs_test {

// Represents a filesystem that is configured using json.  See fs_test.schema.json for the schema.
class JsonFilesystem : public FilesystemImplWithDefaultMake<JsonFilesystem> {
 public:
  static zx::status<std::unique_ptr<JsonFilesystem>> NewFilesystem(
      const rapidjson::Document& config);

  JsonFilesystem(Traits traits, disk_format_t format)
      : traits_(std::move(traits)), format_(format) {}
  virtual ~JsonFilesystem() = default;

  const Traits& GetTraits() const override { return traits_; }

  std::unique_ptr<FilesystemInstance> Create(RamDevice device,
                                             std::string device_path) const override;

  zx::status<std::unique_ptr<FilesystemInstance>> Open(
      const TestFilesystemOptions& options) const override;

 private:
  Traits traits_;
  disk_format_t format_;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_JSON_FILESYSTEM_H_
