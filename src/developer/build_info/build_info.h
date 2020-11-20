// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_BUILD_INFO_BUILD_INFO_H_
#define SRC_DEVELOPER_BUILD_INFO_BUILD_INFO_H_

#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>

// Returns system build information.
class ProviderImpl : public fuchsia::buildinfo::Provider {
 public:
  // Returns product, board, version, and timestamp information used at build time.
  void GetBuildInfo(GetBuildInfoCallback callback) override;

  // Returns a vmo containing the jiri snapshot of the most recent ‘jiri update’.
  void GetSnapshotInfo(GetSnapshotInfoCallback callback) override;

 private:
  std::unique_ptr<std::string> product_config_;
  std::unique_ptr<std::string> board_config_;
  std::unique_ptr<std::string> version_;
  std::unique_ptr<std::string> latest_commit_date_;
};

#endif  // SRC_DEVELOPER_BUILD_INFO_BUILD_INFO_H_
