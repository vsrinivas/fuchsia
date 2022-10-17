// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// inotify is a Linux extension that is also supported on Fuchsia.
#if defined(__Fuchsia__) || defined(__linux__)

#include <fcntl.h>
#include <limits.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

#if defined(__Fuchsia__)
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-async/cpp/bind.h>
#endif  // __Fuchsia__

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

namespace {

// Reading from an inotify object produces a buffer with a number of
// variable-length records containing an optional field. This format
// is annoying. InotifyEventIterator provides a C++ friendly
class InotifyEventIterator {
 public:
  // |buf| should contain at least |buf_len| bytes of data and must outlive this object.
  InotifyEventIterator(char* buf, size_t buf_len) : buf_(buf), offset_(0u), buf_len_(buf_len) {}

  struct Entry {
    int wd;
    uint32_t mask;
    uint32_t cookie;
    std::string_view name;
  };

  // Returns the next entry in the buffer, or std::nullopt if there are no more entries.
  std::optional<Entry> Next();

  bool Done() { return offset_ == buf_len_; }

  size_t offset() const { return offset_; }

 private:
  char* buf_;
  size_t offset_;
  size_t buf_len_;
};

std::optional<InotifyEventIterator::Entry> InotifyEventIterator::Next() {
  if (offset_ + sizeof(struct inotify_event) > buf_len_) {
    return std::nullopt;
  }
  struct inotify_event* event = reinterpret_cast<struct inotify_event*>(buf_ + offset_);
  offset_ += sizeof(struct inotify_event);
  if (offset_ + static_cast<size_t>(event->len) > buf_len_) {
    return std::nullopt;
  }
  offset_ += event->len;
  std::string_view name;
  if (event->len > 0) {
    name = std::string_view(event->name, event->len);
  }
  return Entry{
      .wd = event->wd,
      .mask = event->mask,
      .cookie = event->cookie,
      .name = name,
  };
}

constexpr struct inotify_event kEvent {
  .wd = 1, .mask = IN_OPEN, .cookie = 0,
};

// The inotify read interface can return a variable amount of data since it encodes
// a path name. This type allocates enough space to read at least one inotify event.
using EventBuf = std::array<char, sizeof(inotify_event) + NAME_MAX + 1>;

#if defined(__Fuchsia__)
namespace fio = fuchsia_io;
constexpr char kTmpfsPath[] = "/tmp-inotify";

class Server final : public fidl::testing::WireTestBase<fuchsia_io::Directory> {
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
    // First, figure out how large the event will be including the path.
    const fidl::StringView& path = request->path;
    size_t name_len = path.size();
    size_t event_len = sizeof(kEvent) + name_len +
                       1;  // The serialized event record must include a null terminator.
    std::vector<char> event_buf(event_len);
    struct inotify_event* event = reinterpret_cast<struct inotify_event*>(event_buf.data());
    // Copy in the fixed fields to the event buffer.
    memcpy(event, &kEvent, sizeof(kEvent));
    event->len =
        static_cast<uint32_t>(name_len + 1);  // The |len| field includes the null terminator.
    // Copy in the name.
    memcpy(event->name, path.data(), name_len);
    event->name[name_len] = '\0';

    // Send over the inotify socket.
    size_t actual = 0u;
    ASSERT_OK(inotify_socket_.value().write(0, event_buf.data(), event_buf.size(), &actual));
    ASSERT_EQ(actual, event_buf.size());
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
  fio::wire::InotifyWatchMask filter_;
};

#endif  // defined(__Fuchsia__)

// InotifyTestDirectory creates and tears down a directory suitable for inotify tests.
class InotifyTestDirectory {
 public:
  virtual ~InotifyTestDirectory() = default;

  virtual void SetUp() = 0;

  virtual void TearDown() = 0;

  virtual const char* Path() = 0;

#if defined(__Fuchsia__)
  virtual Server& mutable_server() = 0;
#endif  // defined(__Fuchsia__)
};

class InotifyTestDirectoryHost : public InotifyTestDirectory {
 public:
  void SetUp() final {
    const char* temp_env_var = getenv("TMPDIR");
    temp_dir_name_ = temp_env_var ? temp_env_var : "/tmp";
    temp_dir_name_ += "/inotify-test-XXXXXX";
    mkdtemp(temp_dir_name_.data());
  }

  void TearDown() final { ASSERT_SUCCESS(rmdir(temp_dir_name_.c_str())); }

  const char* Path() final { return temp_dir_name_.c_str(); }

 private:
  std::string temp_dir_name_;
};

#if defined(__Fuchsia__)
class InotifyTestDirectoryFuchsia : public InotifyTestDirectory {
 public:
  InotifyTestDirectoryFuchsia()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), server_(Server(loop_.dispatcher())) {}

  void SetUp() final {
    ASSERT_OK(loop_.StartThread("fake-filesystem"));

    // client-server channel logic
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_OK(endpoints.status_value());
    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &server_);

    // install namespace for local-filesystem.
    ASSERT_OK(fdio_ns_get_installed(&namespace_));
    ASSERT_OK(fdio_ns_bind(namespace_, kTmpfsPath, endpoints->client.channel().release()));
  }

  void TearDown() final { ASSERT_OK(fdio_ns_unbind(namespace_, kTmpfsPath)); }

  const char* Path() final { return "/tmp-inotify"; }

  Server& mutable_server() { return server_; }

 private:
  async::Loop loop_;
  Server server_;
  fdio_ns_t* namespace_;
};
#endif  // defined(__Fuchsia__)

class InotifyAddFilter : public zxtest::Test {
 protected:
  InotifyAddFilter() {
#if defined(__Fuchsia__)
    test_dir_ = std::make_unique<InotifyTestDirectoryFuchsia>();
#else
    test_dir_ = std::make_unique<InotifyTestDirectoryHost>();
#endif  // defined(__Fuchsia__)
  }

  void SetUp() override {
    test_dir_->SetUp();
    ASSERT_TRUE(fd_ = fbl::unique_fd(inotify_init1(0)), "%s", strerror(errno));
  }

  void TearDown() override { test_dir_->TearDown(); }

  const char* TempFsPath() { return test_dir_->Path(); }
  const fbl::unique_fd& fd() { return fd_; }

#if defined(__Fuchsia__)
  Server& mutable_server() { return test_dir_->mutable_server(); }
#endif  // defined(__Fuchsia__)

 private:
  std::unique_ptr<InotifyTestDirectory> test_dir_;
  fbl::unique_fd fd_;
};

TEST(InotifyTest, InitBadFlags) {
  ASSERT_EQ(inotify_init1(5), -1);
  ASSERT_ERRNO(EINVAL);
}

TEST_F(InotifyAddFilter, AddWatchWithNullFilePath) {
  ASSERT_EQ(inotify_add_watch(fd().get(), nullptr, IN_OPEN), -1);
  ASSERT_ERRNO(EFAULT);
}

TEST_F(InotifyAddFilter, AddWatchWithZeroLengthFilePath) {
  ASSERT_EQ(inotify_add_watch(fd().get(), "", IN_OPEN), -1);
  ASSERT_ERRNO(ENOENT);
}

TEST_F(InotifyAddFilter, AddWatch) {
  ASSERT_GE(inotify_add_watch(fd().get(), TempFsPath(), IN_OPEN), 0);
}

TEST_F(InotifyAddFilter, AddWatchOpenRace) {
#if defined(__Fuchsia__)
  mutable_server().SetAddInotifyFilterAsync();
#endif  // __Fuchsia__

  ASSERT_GE(inotify_add_watch(fd().get(), TempFsPath(), IN_OPEN), 0);

  // Expected to fail on Fuchsia since Server::Open drops the request on the
  // floor. That's OK, we don't really need to open, just to trigger an inotify
  // event.
  std::string temp_file_path = std::string(TempFsPath()) + "/" + __FUNCTION__;
  int temp_fd = open(temp_file_path.c_str(), O_CREAT, 0644);
#if defined(__Fuchsia__)
  EXPECT_EQ(temp_fd, -1);
#else
  ASSERT_GT(temp_fd, 0);
  fbl::unique_fd temp_file(temp_fd);
  ASSERT_SUCCESS(unlink(temp_file_path.c_str()));
#endif  // defined(__Fuchsia__)

  // Mark the inotify fd nonblocking to avoid deadlock in this test's failure case.
  int flags;
  ASSERT_GE(flags = fcntl(fd().get(), F_GETFL), 0, "%s", strerror(errno));
  ASSERT_SUCCESS(fcntl(fd().get(), F_SETFL, flags | O_NONBLOCK));

  EventBuf event_buf;
  ASSERT_GE(read(fd().get(), event_buf.data(), event_buf.size()), sizeof(struct inotify_event),
            "%s", strerror(errno));
  struct inotify_event* event = reinterpret_cast<struct inotify_event*>(event_buf.data());
  ASSERT_EQ(event->mask, IN_OPEN);
  ASSERT_EQ(event->wd, kEvent.wd);
  ASSERT_EQ(event->cookie, kEvent.cookie);
}

TEST_F(InotifyAddFilter, AddWatchWithTooLongFilePath) {
  std::string long_filepath(PATH_MAX + 1, 'x');
  ASSERT_EQ(inotify_add_watch(fd().get(), long_filepath.c_str(), IN_OPEN), -1);
  ASSERT_ERRNO(ENAMETOOLONG);
}

TEST_F(InotifyAddFilter, AddMultipleFilters) {
#if defined(__Fuchsia__)
  mutable_server().SetAddInotifyFilterAsync();
#endif  // defined(__Fuchsia__)

  // Use multiple filters in the same add_watch.
  ASSERT_GE(inotify_add_watch(fd().get(), TempFsPath(), IN_OPEN | IN_CREATE), 0);
  constexpr char kTestFileName[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  std::string filepath = std::string(TempFsPath()) + "/" + std::string(kTestFileName);
  int temp_fd = open(filepath.c_str(), O_CREAT, 0644);
#if defined(__Fuchsia__)
  EXPECT_EQ(temp_fd, -1);
#else
  ASSERT_GT(temp_fd, 0);
  fbl::unique_fd temp_file(temp_fd);
  ASSERT_SUCCESS(unlink(filepath.c_str()));
#endif  // defined(__Fuchsia__)

  constexpr size_t kBufSize = 2 * sizeof(struct inotify_event) + 2 * NAME_MAX + 2;
  std::array<char, kBufSize> event_buf;
  ssize_t bytes_read = read(fd().get(), event_buf.data(), event_buf.size());
  // This should read two events, one for IN_CREATE and one for IN_OPEN.
  InotifyEventIterator iter(event_buf.data(), static_cast<size_t>(bytes_read));
#if !defined(__Fuchsia__)
  // TODO(https://fxbug.dev/60109): On Fuchsia, currently we do not get an IN_CREATE event in this
  // case.
  std::optional create_event = iter.Next();
  ASSERT_TRUE(create_event.has_value());
  EXPECT_EQ(create_event->mask, IN_CREATE);
  EXPECT_EQ(create_event->wd, kEvent.wd);
  EXPECT_EQ(create_event->cookie, kEvent.cookie);
  EXPECT_STREQ(std::string(create_event->name), kTestFileName);
#endif  // !defined(__Fuchsia__)

  std::optional open_event = iter.Next();
  ASSERT_TRUE(open_event.has_value());
  EXPECT_EQ(open_event->mask, IN_OPEN);
  EXPECT_EQ(open_event->wd, kEvent.wd);
  EXPECT_EQ(open_event->cookie, kEvent.cookie);
  EXPECT_STREQ(std::string(open_event->name), kTestFileName);
  EXPECT_TRUE(iter.Done(), "consumed %ld of %ld bytes", iter.offset(), bytes_read);
}

TEST_F(InotifyAddFilter, DatagramPayloadNoShortReads) {
#if defined(__Fuchsia__)
  mutable_server().SetAddInotifyFilterAsync();
#endif  // defined(__Fuchsia__)

  ASSERT_GE(inotify_add_watch(fd().get(), TempFsPath(), IN_OPEN), 0);

  // Call open multiple times and see if we receive the inotify_event event in the form of
  // of a single unit, without short reads/writes.
  for (int i = 0; i < 3; i++) {
    std::string temp_file_path = std::string(TempFsPath()) + "/" + __FUNCTION__;
    int temp_fd = open(temp_file_path.c_str(), O_CREAT, 0644);
#if defined(__Fuchsia__)
    EXPECT_EQ(temp_fd, -1);
#else
    ASSERT_GT(temp_fd, 0);
    fbl::unique_fd temp_file(temp_fd);
    ASSERT_SUCCESS(unlink(temp_file_path.c_str()));
#endif  // defined(__Fuchsia__)

    // Try to read events. Make sure we always receive inotify_event as a whole structure, i.e
    // no short reads.
    EventBuf event_buf;
    ASSERT_GE(read(fd().get(), event_buf.data(), event_buf.size()), sizeof(inotify_event), "%s",
              strerror(errno));
    struct inotify_event* event = reinterpret_cast<struct inotify_event*>(event_buf.data());
    EXPECT_EQ(event->mask, kEvent.mask, "iteration %d", i);
    EXPECT_EQ(event->wd, kEvent.wd, "iteration %d", i);
    EXPECT_EQ(event->cookie, kEvent.cookie, "iteration %d", i);
  }
}

class InotifyRemove : public InotifyAddFilter {
 protected:
  void SetUp() override {
    InotifyAddFilter::SetUp();
    ASSERT_GE(wd_ = inotify_add_watch(fd().get(), TempFsPath(), IN_OPEN), 0, "%s", strerror(errno));
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

#endif  // defined(__Fuchsia__) || defined(__linux__)
