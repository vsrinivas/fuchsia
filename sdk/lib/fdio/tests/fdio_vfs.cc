// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/vfs.h>
#include <sys/stat.h>

#include <zxtest/zxtest.h>

// A compile-time test that ensures that the "V_*" constants in vfs.h match the POSIX "S_*"
// constants used in musl.
TEST(Vfs, TypeConstaants) {
  static_assert(V_TYPE_MASK == S_IFMT);
  static_assert(V_TYPE_SOCK == S_IFSOCK);
  static_assert(V_TYPE_LINK == S_IFLNK);
  static_assert(V_TYPE_FILE == S_IFREG);
  static_assert(V_TYPE_BDEV == S_IFBLK);
  static_assert(V_TYPE_DIR == S_IFDIR);
  static_assert(V_TYPE_CDEV == S_IFCHR);
  static_assert(V_TYPE_PIPE == S_IFIFO);

  static_assert(V_ISUID == S_ISUID);
  static_assert(V_ISGID == S_ISGID);
  static_assert(V_ISVTX == S_ISVTX);
  static_assert(V_IRWXU == S_IRWXU);
  static_assert(V_IRUSR == S_IRUSR);
  static_assert(V_IWUSR == S_IWUSR);
  static_assert(V_IXUSR == S_IXUSR);
  static_assert(V_IRWXG == S_IRWXG);
  static_assert(V_IRGRP == S_IRGRP);
  static_assert(V_IWGRP == S_IWGRP);
  static_assert(V_IXGRP == S_IXGRP);
  static_assert(V_IRWXO == S_IRWXO);
  static_assert(V_IROTH == S_IROTH);
  static_assert(V_IWOTH == S_IWOTH);
  static_assert(V_IXOTH == S_IXOTH);
}
