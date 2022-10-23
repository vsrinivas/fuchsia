// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/fs-pty/service.h>
#include <lib/fs-pty/tty-connection-internal.h>
#include <lib/sync/completion.h>
#include <zircon/rights.h>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace {

struct TestConsoleState {
  fit::function<zx_status_t(uint8_t* data, size_t len, size_t* out_actual)> read;
  fit::function<zx_status_t(const uint8_t* data, size_t len, size_t* out_actual)> write;
  fit::function<zx_status_t(zx::eventpair* event)> get_event;

  std::atomic<uint64_t> last_seen_ordinal = 0;
};

struct TestConsoleOps {
  static zx_status_t Read(TestConsoleState* const& state, void* data, size_t len,
                          size_t* out_actual) {
    return state->read(reinterpret_cast<uint8_t*>(data), len, out_actual);
  }
  static zx_status_t Write(TestConsoleState* const& state, const void* data, size_t len,
                           size_t* out_actual) {
    return state->write(reinterpret_cast<const uint8_t*>(data), len, out_actual);
  }
  static zx_status_t GetEvent(TestConsoleState* const& state, zx::eventpair* event) {
    return state->get_event(event);
  }
};

class TestService : public fs_pty::TtyService<TestConsoleOps, TestConsoleState*> {
 public:
  explicit TestService(TestConsoleState* state) : Service(state), state_(state) {}

  ~TestService() override = default;

  // From fs_pty::Service
  void HandleFsSpecificMessage(fidl::IncomingHeaderAndMessage& msg,
                               fidl::Transaction* txn) override {
    auto* hdr = msg.header();
    state_->last_seen_ordinal.store(hdr->ordinal);

    fs_pty::TtyService<TestConsoleOps, TestConsoleState*>::HandleFsSpecificMessage(msg, txn);
  }

 private:
  TestConsoleState* state_;
};

class PtyTestCase : public zxtest::Test {
 public:
  ~PtyTestCase() override = default;

 protected:
  void SetUp() override {
    ASSERT_OK(loop_.StartThread("pty-test-case-async-loop"));
    svc_ = fbl::MakeRefCounted<TestService>(&state_);
  }

  void TearDown() override {
    sync_completion_t completion;
    auto cb = [&completion](zx_status_t status) { sync_completion_signal(&completion); };
    vfs_.Shutdown(cb);
    ASSERT_OK(sync_completion_wait(&completion, ZX_TIME_INFINITE));
  }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  TestConsoleState* state() { return &state_; }

  // Return a connection to the pty service
  void Connect(fidl::WireSyncClient<fuchsia_hardware_pty::Device>* client) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_OK(
        vfs_.Serve(svc_, endpoints->server.TakeChannel(), fs::VnodeConnectionOptions::ReadWrite()));
    *client = fidl::WireSyncClient(std::move(endpoints->client));
  }

 private:
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  fs::ManagedVfs vfs_{loop_.dispatcher()};

  TestConsoleState state_;
  fbl::RefPtr<fs::Vnode> svc_;
};

// Verify describe returns the correct event handle and uses the tty tag
TEST_F(PtyTestCase, Describe) {
  zx::eventpair local, remote;
  ASSERT_OK(zx::eventpair::create(0, &local, &remote));
  state()->get_event = [&remote](zx::eventpair* event) mutable {
    return remote.duplicate(ZX_RIGHT_SAME_RIGHTS, event);
  };

  fidl::WireSyncClient<fuchsia_hardware_pty::Device> client;
  ASSERT_NO_FATAL_FAILURE(Connect(&client));

  auto result = client->Describe2();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().has_event());

  // Check that we got back the handle we expected.
  zx_info_handle_basic_t local_info = {};
  zx_info_handle_basic_t remote_info = {};
  ASSERT_OK(
      local.get_info(ZX_INFO_HANDLE_BASIC, &local_info, sizeof(local_info), nullptr, nullptr));
  ASSERT_OK(result.value().event().get_info(ZX_INFO_HANDLE_BASIC, &remote_info, sizeof(remote_info),
                                            nullptr, nullptr));
  ASSERT_EQ(local_info.related_koid, remote_info.koid);

  // We should have seen an ordinal dispatch
  ASSERT_NE(state()->last_seen_ordinal.load(), 0);
}

// Verify that the Read plumbing works fine
TEST_F(PtyTestCase, Read) {
  constexpr const uint8_t kResponse[] = "test string";
  state()->read = [kResponse](uint8_t* data, size_t len, size_t* out_actual) {
    if (len != sizeof(kResponse)) {
      return ZX_ERR_BAD_STATE;
    }
    memcpy(data, kResponse, len);
    *out_actual = len;
    return ZX_OK;
  };

  fidl::WireSyncClient<fuchsia_hardware_pty::Device> client;
  ASSERT_NO_FATAL_FAILURE(Connect(&client));
  const fidl::WireResult result = client->Read(sizeof(kResponse));
  ASSERT_OK(result.status());
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
  const fidl::VectorView data = response.value()->data;
  ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
            std::string_view(reinterpret_cast<const char*>(kResponse), sizeof(kResponse)));

  // We should not have seen an ordinal dispatch
  ASSERT_EQ(state()->last_seen_ordinal.load(), 0);
}

// Verify that the Write plumbing works fine
TEST_F(PtyTestCase, Write) {
  uint8_t kWrittenData[] = "test string";
  uint8_t buf[sizeof(kWrittenData)];
  state()->write = [&buf](const uint8_t* data, size_t len, size_t* out_actual) {
    if (len != sizeof(kWrittenData)) {
      return ZX_ERR_BAD_STATE;
    }
    memcpy(buf, data, len);
    *out_actual = len;
    return ZX_OK;
  };

  fidl::WireSyncClient<fuchsia_hardware_pty::Device> client;
  ASSERT_NO_FATAL_FAILURE(Connect(&client));
  const fidl::WireResult result =
      client->Write(fidl::VectorView<uint8_t>::FromExternal(kWrittenData));
  ASSERT_OK(result.status());
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
  ASSERT_EQ(response.value()->actual_count, sizeof(kWrittenData));

  // We should not have seen an ordinal dispatch
  ASSERT_EQ(state()->last_seen_ordinal.load(), 0);
}

// Verify that the TTY operations get dispatched
TEST_F(PtyTestCase, TtyOp) {
  fidl::WireSyncClient<fuchsia_hardware_pty::Device> client;
  ASSERT_NO_FATAL_FAILURE(Connect(&client));
  auto result = client->GetWindowSize();
  ASSERT_OK(result.status());
  // We should have seen an ordinal dispatch
  ASSERT_NE(state()->last_seen_ordinal.load(), 0);
}

}  // namespace
