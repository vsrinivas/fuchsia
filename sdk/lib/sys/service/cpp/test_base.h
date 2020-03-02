// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_SERVICE_CPP_TEST_BASE_H_
#define LIB_SYS_SERVICE_CPP_TEST_BASE_H_

#include <fcntl.h>
#include <lib/fdio/namespace.h>
#include <stdlib.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace sys {
namespace testing {

class TestBase : public ::testing::Test {
 protected:
  fdio_ns_t* ns() const { return ns_; }

  int MkDir(std::string dir) {
    std::string path = svc_ + dir;
    return mkdir(path.data(), 066);
  }

  int RmDir(std::string dir) {
    std::string path = svc_ + dir;
    return rmdir(path.data());
  }

 private:
  fdio_ns_t* ns_;
  std::string svc_;

  void SetUp() override {
    zx_status_t status = fdio_ns_create(&ns_);
    ASSERT_EQ(ZX_OK, status);

    char buf[] = "/tmp/svc.XXXXXX";
    svc_ = mkdtemp(buf);

    int ret = MkDir("/fuchsia.examples.MyService");
    ASSERT_EQ(0, ret);
    ret = MkDir("/fuchsia.examples.MyService/default");
    ASSERT_EQ(0, ret);
    ret = MkDir("/fuchsia.examples.MyService/my_instance");
    ASSERT_EQ(0, ret);

    fbl::unique_fd fd(open(svc_.data(), O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(fd.is_valid());
    status = fdio_ns_bind_fd(ns_, "/svc", fd.get());
    ASSERT_EQ(ZX_OK, status);
  }

  void TearDown() override { fdio_ns_destroy(ns_); }
};

}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_SERVICE_CPP_TEST_BASE_H_
