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
#include <limits.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

namespace {

namespace fio = fuchsia_io;
namespace fio2 = fuchsia_io2;
constexpr char kTmpfsPath[] = "/tmp-inotify";
constexpr struct inotify_event kEvent { .wd = 1, .mask = IN_OPEN, .cookie = 10 };

class Server final : public fio::testing::Directory_TestBase {
 public:
  explicit Server(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Open(OpenRequestView request, OpenCompleter::Sync& completer) override {
    uint32_t incoming_filter = static_cast<uint32_t>(filter_) & IN_OPEN;
    ASSERT_EQ(incoming_filter, IN_OPEN, "Inotify filter %u does not match open event",
              static_cast<uint32_t>(filter_));
    ASSERT_TRUE(inotify_socket_.has_value());

    // Write the Open event to the socket.
    size_t actual;
    ASSERT_OK(inotify_socket_.value().write(0, &kEvent, sizeof(kEvent), &actual));
    ASSERT_EQ(actual, sizeof(kEvent));
  }

  void AddInotifyFilter(AddInotifyFilterRequestView request,
                        AddInotifyFilterCompleter::Sync& completer) override {
    filter_ = request->filter;
    if (add_inotify_filter_async_) {
      async::PostDelayedTask(
          dispatcher_,
          [&, socket = std::move(request->socket), completer = completer.ToAsync()]() mutable {
            ASSERT_FALSE(inotify_socket_.has_value());
            inotify_socket_ = std::move(socket);
            completer.Reply();
          },
          zx::msec(50));
    } else {
      ASSERT_FALSE(inotify_socket_.has_value());
      inotify_socket_ = std::move(request->socket);
      completer.Reply();
    }
  }

  void SetAddInotifyFilterAsync() { add_inotify_filter_async_ = true; }

 private:
  async_dispatcher_t* dispatcher_;

  bool add_inotify_filter_async_ = false;
  std::optional<zx::socket> inotify_socket_;
  fio2::wire::InotifyWatchMask filter_;
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
  ASSERT_GE(inotify_add_watch(fd().get(), kTmpfsPath, IN_OPEN), 0);
}

TEST_F(InotifyAddFilter, AddWatchOpenRace) {
  mutable_server().SetAddInotifyFilterAsync();

  ASSERT_GE(inotify_add_watch(fd().get(), kTmpfsPath, IN_OPEN), 0);

  // Expected to fail since Server::Open drops the request on the floor. That's OK, we don't really
  // need to open, just to trigger an inotify event.
  ASSERT_EQ(open(((std::string(kTmpfsPath) + "/" + __FUNCTION__).c_str()), O_CREAT, 0644), -1);

  // Mark the inotify fd nonblocking to avoid deadlock in this test's failure case.
  int flags;
  ASSERT_GE(flags = fcntl(fd().get(), F_GETFL), 0, "%s", strerror(errno));
  ASSERT_SUCCESS(fcntl(fd().get(), F_SETFL, flags | O_NONBLOCK));

  std::unique_ptr<struct inotify_event> event = std::make_unique<inotify_event>();
  ASSERT_EQ(read(fd().get(), event.get(), sizeof(*event)), sizeof(*event), "%s", strerror(errno));
  ASSERT_EQ(event->mask, kEvent.mask);
  ASSERT_EQ(event->wd, kEvent.wd);
  ASSERT_EQ(event->cookie, kEvent.cookie);
}

TEST_F(InotifyAddFilter, AddWatchWithTooLongFilePath) {
  std::string long_filepath(PATH_MAX + 1, 'x');
  ASSERT_EQ(inotify_add_watch(fd().get(), long_filepath.c_str(), 0), -1);
  ASSERT_ERRNO(ENAMETOOLONG);
}

TEST_F(InotifyAddFilter, AddMultipleFilters) {
  mutable_server().SetAddInotifyFilterAsync();

  // Use multiple filters in the same add_watch.
  ASSERT_GE(inotify_add_watch(fd().get(), kTmpfsPath, IN_OPEN | IN_CREATE), 0);
  ASSERT_EQ(open(((std::string(kTmpfsPath) + "/" + __FUNCTION__).c_str()), O_CREAT, 0644), -1);

  std::unique_ptr<struct inotify_event> event = std::make_unique<inotify_event>();
  ASSERT_EQ(read(fd().get(), event.get(), sizeof(*event)), sizeof(*event), "%s", strerror(errno));
  ASSERT_EQ(event->mask, kEvent.mask);
  ASSERT_EQ(event->wd, kEvent.wd);
  ASSERT_EQ(event->cookie, kEvent.cookie);
}

TEST_F(InotifyAddFilter, DatagramPayloadNoShortReads) {
  mutable_server().SetAddInotifyFilterAsync();

  ASSERT_GE(inotify_add_watch(fd().get(), kTmpfsPath, IN_OPEN), 0);

  // Call open multiple times and see if we receive the inotify_event event in the form of
  // of a single unit, without short reads/writes.
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(open(((std::string(kTmpfsPath) + "/" + __FUNCTION__).c_str()), O_CREAT, 0644), -1);
    std::vector<uint8_t> buffer(sizeof(inotify_event) + PATH_MAX + 1);
    // Try to read events. Make sure we always receive inotify_event as a whole structure, i.e
    // no short reads.
    // Try to read 1 more byte than the actual datagram size.
    ASSERT_EQ(read(fd().get(), buffer.data(), sizeof(inotify_event) + 1), sizeof(inotify_event),
              "%s", strerror(errno));
    struct inotify_event* event = reinterpret_cast<inotify_event*>(buffer.data());
    ASSERT_EQ(event->mask, kEvent.mask);
    ASSERT_EQ(event->wd, kEvent.wd);
    ASSERT_EQ(event->cookie, kEvent.cookie);
  }
}

class InotifyRemove : public InotifyAddFilter {
 protected:
  void SetUp() override {
    InotifyAddFilter::SetUp();
    ASSERT_GE(wd_ = inotify_add_watch(fd().get(), kTmpfsPath, IN_OPEN), 0, "%s", strerror(errno));
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
