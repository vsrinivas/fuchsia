// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/channel.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

TEST(UnsafeTest, BorrowChannel) {
  fbl::unique_fd fd(open("/svc", O_DIRECTORY | O_RDONLY));
  ASSERT_LE(0, fd.get());

  fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
  ASSERT_NOT_NULL(io);

  zx::unowned_channel dir(fdio_unsafe_borrow_channel(io));
  ASSERT_TRUE(dir->is_valid());

  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  auto result = ::llcpp::fuchsia::io::Node::Call::Clone(
      std::move(dir), ::llcpp::fuchsia::io::CLONE_FLAG_SAME_RIGHTS, std::move(h1));
  ASSERT_OK(result.status());

  fdio_unsafe_release(io);
  fd.reset();
}
