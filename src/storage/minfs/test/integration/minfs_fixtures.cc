// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minfs_fixtures.h"

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>

#include <fvm/format.h>
#include <zxtest/zxtest.h>

namespace {

void CheckMinfsInfo(fs::FilesystemTest* test) {
  ::llcpp::fuchsia::io::FilesystemInfo info;
  ASSERT_NO_FAILURES(test->GetFsInfo(&info));

  const char kFsName[] = "minfs";
  const char* name = reinterpret_cast<const char*>(info.name.data());
  ASSERT_STR_EQ(kFsName, name);
  ASSERT_LE(info.used_nodes, info.total_nodes, "Used nodes greater than free nodes");
  ASSERT_LE(info.used_bytes, info.total_bytes, "Used bytes greater than free bytes");
}

}  // namespace

void MinfsTest::CheckInfo() { CheckMinfsInfo(this); }

void MinfsTestWithFvm::CheckInfo() { CheckMinfsInfo(this); }
