// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <zx/vmo.h>

#include <string>

#include "gtest/gtest.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/strings/string_view.h"

namespace fsl {
namespace {

TEST(VMOAndFile, VmoFromFd) {
  files::ScopedTempDir temp_dir;

  std::string path;
  EXPECT_TRUE(temp_dir.NewTempFile(&path));

  fxl::UniqueFD fd(open(path.c_str(), O_RDWR));
  EXPECT_TRUE(fd.is_valid());
  constexpr fxl::StringView payload = "Payload";
  EXPECT_EQ(static_cast<ssize_t>(payload.size()),
            write(fd.get(), payload.data(), payload.size()));

  fsl::SizedVmo vmo;
  EXPECT_TRUE(VmoFromFd(std::move(fd), &vmo));

  std::string data;
  EXPECT_TRUE(StringFromVmo(vmo, &data));

  EXPECT_EQ(payload, data);
}

TEST(VMOAndFile, VmoFromFilename) {
  files::ScopedTempDir temp_dir;

  std::string path;
  EXPECT_TRUE(temp_dir.NewTempFile(&path));

  fxl::UniqueFD fd(open(path.c_str(), O_RDWR));
  EXPECT_TRUE(fd.is_valid());
  constexpr fxl::StringView payload = "Another playload";
  EXPECT_EQ(static_cast<ssize_t>(payload.size()),
            write(fd.get(), payload.data(), payload.size()));
  fd.reset();

  fsl::SizedVmo vmo;
  EXPECT_TRUE(VmoFromFilename(path.c_str(), &vmo));

  std::string data;
  EXPECT_TRUE(StringFromVmo(vmo, &data));

  EXPECT_EQ("Another playload", data);
}

}  // namespace
}  // namespace fsl
