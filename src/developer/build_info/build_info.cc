// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build_info.h"

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/io.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/unique_fd.h>

#include "src/lib/files/file.h"
#include "zircon/status.h"

namespace {
const char kProductFilePath[] = "/config/build-info/product";
const char kBoardFilePath[] = "/config/build-info/board";
const char kVersionFilePath[] = "/config/build-info/version";
const char kLatestCommitDateFilePath[] = "/config/build-info/latest-commit-date";
const char kSnapshotFilePath[] = "/config/build-info/snapshot";

// Returns the contents of |file_path| with any trailing whitespace removed.
std::string ContentsOfFileAtPath(const std::string &file_path) {
  std::string file_contents;
  if (!files::ReadFileToString(file_path, &file_contents)) {
    FX_LOGS(ERROR) << "Error reading " << file_path;
  }

  // Trim trailing whitespace.
  const std::string whitespace = " \n\r\t";
  size_t end = file_contents.find_last_not_of(whitespace);

  return (end == std::string::npos) ? "" : file_contents.substr(0, end + 1);
}

}  // namespace

void ProviderImpl::GetBuildInfo(GetBuildInfoCallback callback) {
  fuchsia::buildinfo::BuildInfo build_info;

  if (!product_config_) {
    product_config_ = std::make_unique<std::string>(ContentsOfFileAtPath(kProductFilePath));
  }
  build_info.set_product_config(*product_config_);

  if (!board_config_) {
    board_config_ = std::make_unique<std::string>(ContentsOfFileAtPath(kBoardFilePath));
  }
  build_info.set_board_config(*board_config_);

  if (!version_) {
    version_ = std::make_unique<std::string>(ContentsOfFileAtPath(kVersionFilePath));
  }
  build_info.set_version(*version_);

  if (!latest_commit_date_) {
    latest_commit_date_ =
        std::make_unique<std::string>(ContentsOfFileAtPath(kLatestCommitDateFilePath));
  }
  build_info.set_latest_commit_date(*latest_commit_date_);

  callback(std::move(build_info));
}

void ProviderImpl::GetSnapshotInfo(GetSnapshotInfoCallback callback) {
  zx::vmo file_vmo;

  // Open the snapshot file.
  fbl::unique_fd fd(open(kSnapshotFilePath, O_RDONLY));

  if (!fd.is_valid()) {
    FX_LOGS(ERROR) << "Failed to open " << kSnapshotFilePath << ": " << strerror(errno);
    callback(std::move(file_vmo));
    return;
  }

  // Copy the file contents into a vmo.
  zx_handle_t vmo_handle;
  zx_status_t status;
  status = fdio_get_vmo_copy(fd.get(), &vmo_handle);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get vmo for " << kSnapshotFilePath << ": "
                   << zx_status_get_string(status);
  } else {
    file_vmo.reset(vmo_handle);
  }

  // Return the vmo.
  callback(std::move(file_vmo));
}
