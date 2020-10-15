// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/fs-pty/service.h>
#include <lib/fs-pty/tty-connection-internal.h>
#include <lib/sync/completion.h>

#include <fs/managed_vfs.h>
#include <fs/vfs_types.h>
#include <zxtest/zxtest.h>

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

class TestService : public fs_pty::Service<fs_pty::internal::NullPtyDevice<TestConsoleState*>,
                                           TestConsoleOps, TestConsoleState*> {
 public:
  TestService(TestConsoleState* state) : Service(state), state_(state) {}

  ~TestService() override = default;

  // From fs_pty::Service
  void HandleFsSpecificMessage(fidl_incoming_msg_t* msg, fidl::Transaction* txn) override {
    auto hdr = static_cast<fidl_message_header_t*>(msg->bytes);
    state_->last_seen_ordinal.store(hdr->ordinal);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
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
  void Connect(::llcpp::fuchsia::hardware::pty::Device::SyncClient* client) {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    ASSERT_OK(vfs_.Serve(svc_, std::move(remote), fs::VnodeConnectionOptions::ReadWrite()));
    *client = ::llcpp::fuchsia::hardware::pty::Device::SyncClient{std::move(local)};
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
  state()->get_event = [ev = std::move(remote)](zx::eventpair* event) mutable {
    *event = std::move(ev);
    return ZX_OK;
  };

  ::llcpp::fuchsia::hardware::pty::Device::SyncClient client{zx::channel()};
  ASSERT_NO_FATAL_FAILURES(Connect(&client));
  auto result = client.Describe();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->info.is_tty());

  // Check that we got back the handle we expected.
  zx_info_handle_basic_t local_info = {};
  zx_info_handle_basic_t remote_info = {};
  ASSERT_OK(
      local.get_info(ZX_INFO_HANDLE_BASIC, &local_info, sizeof(local_info), nullptr, nullptr));
  ASSERT_OK(result->info.tty().event.get_info(ZX_INFO_HANDLE_BASIC, &remote_info,
                                              sizeof(remote_info), nullptr, nullptr));
  ASSERT_EQ(local_info.related_koid, remote_info.koid);

  // We should not have seen an ordinal dispatch
  ASSERT_EQ(state()->last_seen_ordinal.load(), 0);
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

  ::llcpp::fuchsia::hardware::pty::Device::SyncClient client{zx::channel()};
  ASSERT_NO_FATAL_FAILURES(Connect(&client));
  auto result = client.Read(sizeof(kResponse));
  ASSERT_OK(result.status());
  ASSERT_EQ(result->data.count(), sizeof(kResponse));
  ASSERT_BYTES_EQ(result->data.data(), kResponse, sizeof(kResponse));

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

  ::llcpp::fuchsia::hardware::pty::Device::SyncClient client{zx::channel()};
  ASSERT_NO_FATAL_FAILURES(Connect(&client));
  auto result = client.Write(fidl::unowned_vec(kWrittenData));
  ASSERT_OK(result.status());
  ASSERT_EQ(result->actual, sizeof(kWrittenData));

  // We should not have seen an ordinal dispatch
  ASSERT_EQ(state()->last_seen_ordinal.load(), 0);
}

// Verify that the TTY operations get dispatched
TEST_F(PtyTestCase, TtyOp) {
  ::llcpp::fuchsia::hardware::pty::Device::SyncClient client{zx::channel()};
  ASSERT_NO_FATAL_FAILURES(Connect(&client));
  auto result = client.GetWindowSize();
  // Get peer closed, since our HandleFsSpecificMessage returned an error.
  ASSERT_STATUS(result.status(), ZX_ERR_PEER_CLOSED);
  // We should have seen an ordinal dispatch
  ASSERT_NE(state()->last_seen_ordinal.load(), 0);
}

}  // namespace
