// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/time.h>
#include <lib/zxio/watcher.h>
#include <lib/zxio/zxio.h>

#include <vector>

#include <zxtest/zxtest.h>

TEST(WatcherTest, WatchInvalidObject) {
  ASSERT_STATUS(zxio_watch_directory(nullptr, nullptr, ZX_TIME_INFINITE, nullptr),
                ZX_ERR_BAD_HANDLE);
}

template <typename F>
class Server final : public fidl::testing::WireTestBase<fuchsia_io::Directory> {
 public:
  explicit Server(F on_watch) : on_watch_(std::move(on_watch)) {}

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    ADD_FAILURE() << "method should not be called: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) override {
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }

  void Query(QueryCompleter::Sync& completer) final {
    const std::string_view kProtocol = fuchsia_io::wire::kDirectoryProtocolName;
    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
    completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
  }

  void Watch(WatchRequestView request, WatchCompleter::Sync& completer) override {
    on_watch_(request->mask, request->options, std::move(request->watcher), completer);
  }

 private:
  F on_watch_;
};

TEST(WatcherTest, WatchInvalidCallback) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(endpoints.status_value());

  Server server([](fuchsia_io::wire::WatchMask mask, uint32_t options,
                   fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher,
                   fidl::WireServer<fuchsia_io::Directory>::WatchCompleter::Sync& completer) {});

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(endpoints->server), &server));
  ASSERT_OK(loop.StartThread("fake-directory-server"));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(endpoints->client.channel().release(), &storage));
  zxio_t* io = &storage.io;

  ASSERT_STATUS(zxio_watch_directory(io, nullptr, ZX_TIME_INFINITE, nullptr), ZX_ERR_INVALID_ARGS);

  ASSERT_OK(zxio_close(io));
}

TEST(WatcherTest, Smoke) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(endpoints.status_value());

  Server server([](fuchsia_io::wire::WatchMask mask, uint32_t options,
                   fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher,
                   fidl::WireServer<fuchsia_io::Directory>::WatchCompleter::Sync& completer) {
    uint8_t bytes[fuchsia_io::wire::kMaxBuf];
    auto it = std::begin(bytes);

    {
      constexpr char name[] = "unsupported";
      *it++ = static_cast<uint8_t>(fuchsia_io::wire::WatchEvent::kIdle) + 1;
      *it++ = sizeof(name);
      it = std::copy(std::cbegin(name), std::cend(name), it);
    }
    {
      constexpr char name[] = "valid";
      *it++ = static_cast<uint8_t>(fuchsia_io::wire::WatchEvent::kAdded);
      *it++ = sizeof(name);
      it = std::copy(std::cbegin(name), std::cend(name), it);
    }
    {
      // Incomplete; event without name.
      *it++ = static_cast<uint8_t>(fuchsia_io::wire::WatchEvent::kAdded);
      *it++ = 1;
    }

    completer.Reply(watcher.channel().write(
        0, bytes, static_cast<uint32_t>(std::distance(std::begin(bytes), it)), nullptr, 0));
  });
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(endpoints->server), &server));
  ASSERT_OK(loop.StartThread("fake-directory-server"));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(endpoints->client.channel().release(), &storage));
  zxio_t* io = &storage.io;

  std::vector<std::pair<zxio_watch_directory_event_t, std::string>> events;
  ASSERT_STATUS(zxio_watch_directory(
                    io,
                    [](zxio_watch_directory_event_t event, const char* name, void* cookie) {
                      auto events_cookie = reinterpret_cast<decltype(events)*>(cookie);
                      events_cookie->emplace_back(event, std::string(name));
                      return ZX_OK;
                    },
                    zx::time::infinite().get(), &events),
                ZX_ERR_PEER_CLOSED);
  decltype(events) expected_events = {{ZXIO_WATCH_EVENT_ADD_FILE, "valid"}};
  ASSERT_EQ(events, expected_events);

  ASSERT_OK(zxio_close(io));
}
