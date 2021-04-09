// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl_test_base.h>
#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/inotify.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zxio/inception.h>
#include <limits.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

namespace {

namespace fio = fuchsia_io;
namespace fio2 = fuchsia_io2;
constexpr char kTmpfsPath[] = "/tmp-inotify";
constexpr char kPayload[] = "fake inotify event";

class Server final : public fio::testing::Directory_TestBase {
 public:
  explicit Server(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Open(uint32_t flags, uint32_t mode, fidl::StringView path,
            fidl::ServerEnd<::fuchsia_io::Node> object, OpenCompleter::Sync& completer) override {
    // Normally inotify would send an event on the registered socket. At the time of writing, we are
    // only interested in testing that AddInotifyFilter and Open are properly serialized by the
    // client, so we just send a known payload as a simpler alternative that we can observe in the
    // test.
    ASSERT_TRUE(inotify_socket_.has_value());
    ASSERT_OK(inotify_socket_.value().write(0, kPayload, sizeof(kPayload), nullptr));
    completer.Close(ZX_OK);
  }

  void AddInotifyFilter(fidl::StringView path, fio2::wire::InotifyWatchMask filters,
                        uint32_t watch_descriptor, zx::socket socket,
                        AddInotifyFilterCompleter::Sync& completer) override {
    if (add_inotify_filter_async_) {
      async::PostDelayedTask(
          dispatcher_,
          [&, socket = std::move(socket), completer = completer.ToAsync()]() mutable {
            ASSERT_FALSE(inotify_socket_.has_value());
            inotify_socket_ = std::move(socket);
            completer.Reply();
          },
          zx::msec(50));
    } else {
      ASSERT_FALSE(inotify_socket_.has_value());
      inotify_socket_ = std::move(socket);
      completer.Reply();
    }
  }

  void SetAddInotifyFilterAsync() { add_inotify_filter_async_ = true; }

 private:
  async_dispatcher_t* dispatcher_;

  bool add_inotify_filter_async_ = false;
  std::optional<zx::socket> inotify_socket_;
};

class InotifyAddFilter : public zxtest::Test {
 protected:
  InotifyAddFilter()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), server_(Server(loop_.dispatcher())) {}

  void SetUp() override {
    ASSERT_OK(loop_.StartThread("fake-filesystem"));
    ASSERT_TRUE(fd_ = fbl::unique_fd(inotify_init1(0)), "%s", strerror(errno));

    // client-server channel logic
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_OK(endpoints.status_value());
    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &server_);

    // install namespace for local-filesystem.
    ASSERT_OK(fdio_ns_get_installed(&namespace_));
    ASSERT_OK(fdio_ns_bind(namespace_, kTmpfsPath, endpoints->client.channel().release()));
  }

  void TearDown() override { ASSERT_OK(fdio_ns_unbind(namespace_, kTmpfsPath)); }

  const fbl::unique_fd& fd() { return fd_; }

  Server& mutable_server() { return server_; }

 private:
  async::Loop loop_;
  Server server_;
  fdio_ns_t* namespace_;
  fbl::unique_fd fd_;
};

TEST(InotifyTest, InitBadFlags) {
  ASSERT_EQ(inotify_init1(5), -1);
  ASSERT_ERRNO(EINVAL);
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

TEST_F(InotifyAddFilter, AddWatchOpenRace) {
  mutable_server().SetAddInotifyFilterAsync();

  ASSERT_GE(inotify_add_watch(fd().get(), kTmpfsPath, IN_CREATE), 0);

  // Expected to fail since Server::Open drops the request on the floor. That's OK, we don't really
  // need to open, just to trigger an inotify event.
  ASSERT_EQ(open(((std::string(kTmpfsPath) + "/" + __FUNCTION__).c_str()), O_CREAT, 0644), -1);
  ASSERT_ERRNO(EPIPE);

  // Mark the inotify fd nonblocking to avoid deadlock in this test's failure case.
  int flags;
  ASSERT_GE(flags = fcntl(fd().get(), F_GETFL), 0, "%s", strerror(errno));
  ASSERT_SUCCESS(fcntl(fd().get(), F_SETFL, flags | O_NONBLOCK));

  char buf[sizeof(kPayload) + 1];
  ASSERT_EQ(read(fd().get(), buf, sizeof(buf)), sizeof(kPayload), "%s", strerror(errno));
  buf[sizeof(buf) - 1] = 0;
  ASSERT_STREQ(buf, kPayload);
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
