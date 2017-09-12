// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <mx/vmo.h>

#include <string>

#include "gtest/gtest.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/strings.h"

namespace fsl {
namespace {

TEST(VMOAndFile, VmoFromFd) {
  files::ScopedTempDir temp_dir;

  std::string path;
  EXPECT_TRUE(temp_dir.NewTempFile(&path));

  fxl::UniqueFD fd(open(path.c_str(), O_RDWR));
  EXPECT_TRUE(fd.is_valid());
  EXPECT_EQ(7, write(fd.get(), "Payload", 7));

  mx::vmo vmo;
  EXPECT_TRUE(VmoFromFd(std::move(fd), &vmo));

  std::string data;
  EXPECT_TRUE(StringFromVmo(vmo, &data));

  EXPECT_EQ("Payload", data);
}

TEST(VMOAndFile, VmoFromFilename) {
  files::ScopedTempDir temp_dir;

  std::string path;
  EXPECT_TRUE(temp_dir.NewTempFile(&path));

  fxl::UniqueFD fd(open(path.c_str(), O_RDWR));
  EXPECT_TRUE(fd.is_valid());
  EXPECT_EQ(16, write(fd.get(), "Another playload", 16));
  fd.reset();

  mx::vmo vmo;
  EXPECT_TRUE(VmoFromFilename(path.c_str(), &vmo));

  std::string data;
  EXPECT_TRUE(StringFromVmo(vmo, &data));

  EXPECT_EQ("Another playload", data);
}

}  // namespace
}  // namespace fsl
