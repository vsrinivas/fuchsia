// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/time.h>

#include <vector>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

TEST(WatcherTest, WatchInvalidDirFD) {
  ASSERT_STATUS(fdio_watch_directory(-1, nullptr, ZX_TIME_INFINITE, nullptr), ZX_ERR_INVALID_ARGS);
}

template <typename F>
class Server final : public fuchsia_io::testing::Directory_TestBase {
 public:
  explicit Server(F onWatch) : onWatch_(onWatch) {}

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
    completer.Close(ZX_OK);
  }

  void Close2(Close2RequestView request, Close2Completer::Sync& completer) override {
    completer.Reply({});
    completer.Close(ZX_OK);
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    fuchsia_io::wire::DirectoryObject directory;
    completer.Reply(fuchsia_io::wire::NodeInfo::WithDirectory(std::move(directory)));
  }

  void Watch(WatchRequestView request, WatchCompleter::Sync& completer) override {
    onWatch_(request->mask, request->options, std::move(request->watcher), completer);
  }

 private:
  F onWatch_;
};

TEST(WatcherTest, Smoke) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(endpoints.status_value());

  Server server([](uint32_t mask, uint32_t options, zx::channel watcher,
                   fidl::WireServer<fuchsia_io::Directory>::WatchCompleter::Sync& completer) {
    uint8_t bytes[fuchsia_io::wire::kMaxBuf];
    auto it = std::begin(bytes);

    {
      constexpr char name[] = "unsupported";
      *it++ = fuchsia_io::wire::kWatchEventIdle + 1;
      *it++ = sizeof(name);
      it = std::copy(std::cbegin(name), std::cend(name), it);
    }
    {
      constexpr char name[] = "valid";
      *it++ = fuchsia_io::wire::kWatchEventAdded;
      *it++ = sizeof(name);
      it = std::copy(std::cbegin(name), std::cend(name), it);
    }
    {
      // Incomplete; event without name.
      *it++ = fuchsia_io::wire::kWatchEventAdded;
      *it++ = 1;
    }

    completer.Reply(watcher.write(
        0, bytes, static_cast<uint32_t>(std::distance(std::begin(bytes), it)), nullptr, 0));
  });
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(endpoints->server), &server));
  ASSERT_OK(loop.StartThread("fake-directory-server"));

  fbl::unique_fd directory;
  ASSERT_OK(
      fdio_fd_create(endpoints->client.channel().release(), directory.reset_and_get_address()));

  std::vector<std::pair<int, std::string>> events;
  ASSERT_STATUS(fdio_watch_directory(
                    directory.get(),
                    [](int dirfd, int event, const char* name, void* cookie) {
                      auto events_cookie = reinterpret_cast<decltype(events)*>(cookie);
                      events_cookie->emplace_back(event, std::string(name));
                      return ZX_OK;
                    },
                    zx::time::infinite().get(), &events),
                ZX_ERR_PEER_CLOSED);
  decltype(events) expected_events = {{WATCH_EVENT_ADD_FILE, "valid"}};
  ASSERT_EQ(events, expected_events);
}
