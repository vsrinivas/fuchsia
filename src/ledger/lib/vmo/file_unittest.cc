// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/vmo/file.h"

#include <fcntl.h>
#include <lib/zx/vmo.h>

#include <string>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/lib/vmo/sized_vmo.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {
namespace {

TEST(VMOAndFile, VmoFromFd) {
  scoped_tmpfs::ScopedTmpFS tmpfs;
  fbl::unique_fd fd(openat(tmpfs.root_fd(), "file", O_RDWR | O_CREAT));
  EXPECT_TRUE(fd.is_valid());

  constexpr fxl::StringView payload = "Payload";
  EXPECT_EQ(static_cast<ssize_t>(payload.size()), write(fd.get(), payload.data(), payload.size()));

  ledger::SizedVmo vmo;
  EXPECT_TRUE(VmoFromFd(std::move(fd), &vmo));

  std::string data;
  EXPECT_TRUE(StringFromVmo(vmo, &data));

  EXPECT_EQ(payload, data);
}

TEST(VMOAndFile, VmoFromFilename) {
  scoped_tmpfs::ScopedTmpFS tmpfs;
  fbl::unique_fd fd(openat(tmpfs.root_fd(), "file", O_RDWR | O_CREAT));
  EXPECT_TRUE(fd.is_valid());

  constexpr fxl::StringView payload = "Another playload";
  EXPECT_EQ(static_cast<ssize_t>(payload.size()), write(fd.get(), payload.data(), payload.size()));
  fd.reset();

  ledger::SizedVmo vmo;
  EXPECT_TRUE(VmoFromFilenameAt(tmpfs.root_fd(), "file", &vmo));

  std::string data;
  EXPECT_TRUE(StringFromVmo(vmo, &data));

  EXPECT_EQ("Another playload", data);
}

}  // namespace
}  // namespace ledger
