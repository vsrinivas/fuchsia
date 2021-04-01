// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/inotify.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>
#include <limits.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

namespace {

namespace fio = fuchsia_io;
namespace fio2 = fuchsia_io2;
constexpr char kTmpfsPath[] = "/tmp-inotify";

class TestServer final : public fio::Directory::RawChannelInterface {
 public:
  TestServer() = default;

  void Close(CloseCompleter::Sync& completer) override { completer.Close(ZX_ERR_NOT_SUPPORTED); }

  void Clone(uint32_t flags, zx::channel object, CloneCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Describe(DescribeCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Sync(SyncCompleter::Sync& completer) override { completer.Close(ZX_ERR_NOT_SUPPORTED); }

  void GetAttr(GetAttrCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void SetAttr(uint32_t flags, fio::wire::NodeAttributes attribute,
               SetAttrCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Open(uint32_t flags, uint32_t mode, ::fidl::StringView path, ::zx::channel object,
            OpenCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void AddInotifyFilter(::fidl::StringView path, fio2::wire::InotifyWatchMask filters,
                        uint32_t watch_descriptor, ::zx::socket socket,
                        AddInotifyFilterCompleter::Sync& completer) override {
    completer.Close(ZX_OK);
  }

  void Unlink(::fidl::StringView path, UnlinkCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void ReadDirents(uint64_t max_bytes, ReadDirentsCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Rewind(RewindCompleter::Sync& completer) override { completer.Close(ZX_ERR_NOT_SUPPORTED); }

  void GetToken(GetTokenCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Rename(::fidl::StringView src, ::zx::handle dst_parent_token, ::fidl::StringView dst,
              RenameCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Link(::fidl::StringView src, ::zx::handle dst_parent_token, ::fidl::StringView dst,
            LinkCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Watch(uint32_t mask, uint32_t options, ::zx::channel watcher,
             WatchCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
};

class InotifyAddFilter : public zxtest::Test {
 public:
  void SetUp() override {
    server_ = std::make_unique<TestServer>();
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_OK(loop_->StartThread("fake-filesystem"));
    ASSERT_TRUE(fd_ = fbl::unique_fd(inotify_init1(0)), "%s", strerror(errno));

    // client-server channel logic
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_OK(fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(endpoints->server),
                                           server_.get()));

    // install namespace for local-filesystem.
    ASSERT_OK(fdio_ns_get_installed(&namespace_));
    ASSERT_OK(fdio_ns_bind(namespace_, kTmpfsPath, endpoints->client.channel().release()));
  }

  const fbl::unique_fd& fd() { return fd_; }
  void TearDown() final { ASSERT_OK(fdio_ns_unbind(namespace_, kTmpfsPath)); }

 protected:
  std::unique_ptr<TestServer> server_;
  std::unique_ptr<async::Loop> loop_;
  fdio_ns_t* namespace_;
  fbl::unique_fd fd_;
};

TEST(InotifyTest, InitBadFlags) {
  int inotifyfd = inotify_init1(5);
  ASSERT_EQ(inotifyfd, -1, "inotify_init1() did not fail with bad flags.");
}

TEST_F(InotifyAddFilter, AddWatchWithNullFilePath) {
  ASSERT_EQ(inotify_add_watch(fd().get(), nullptr, 0), -1);
  ASSERT_ERRNO(EFAULT);
}

TEST_F(InotifyAddFilter, AddWatchWithZeroLengthFilePath) {
  ASSERT_EQ(inotify_add_watch(fd().get(), "", 0), -1);
  ASSERT_ERRNO(EINVAL);
}

TEST_F(InotifyAddFilter, AddWatch) {
  ASSERT_GE(inotify_add_watch(fd().get(), kTmpfsPath, IN_CREATE), 0);
}

TEST_F(InotifyAddFilter, AddWatchWithTooLongFilePath) {
  std::string long_filepath(PATH_MAX + 1, 'x');
  ASSERT_EQ(inotify_add_watch(fd().get(), long_filepath.c_str(), 0), -1);
  ASSERT_ERRNO(ENAMETOOLONG);
}

class InotifyRemove : public InotifyAddFilter {
 protected:
  void SetUp() override {
    InotifyAddFilter::SetUp();
    ASSERT_GE(wd_ = inotify_add_watch(fd().get(), kTmpfsPath, IN_CREATE), 0, "%s", strerror(errno));
  }

  int wd() const { return wd_; }

 private:
  int wd_;
};

TEST_F(InotifyRemove, Remove) { ASSERT_SUCCESS(inotify_rm_watch(fd().get(), wd())); }

TEST_F(InotifyRemove, RemoveWithInvalidInotifyDescriptor) {
  ASSERT_EQ(inotify_rm_watch(fd().get() + 1, wd()), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(InotifyRemove, RemoveWithInvalidWatchDescriptor) {
  ASSERT_EQ(inotify_rm_watch(fd().get(), wd() + 1), -1);
  ASSERT_ERRNO(EINVAL);
}

}  // namespace
