// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/txn_header.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <memory>
#include <utility>

#include <fbl/vector.h>
#include <fidl/test/llcpp/controlflow/c/fidl.h>
#include <zxtest/zxtest.h>

// Interface under test
#include <fidl/test/llcpp/controlflow/llcpp/fidl.h>

// Control flow tests: manually interact with a server and test for epitaph and shutdown.
namespace {

namespace gen = ::llcpp::fidl::test::llcpp::controlflow;

class Server : public gen::ControlFlow::Interface {
 public:
  void Shutdown(ShutdownCompleter::Sync& txn) final { txn.Close(ZX_OK); }

  void NoReplyMustSendAccessDeniedEpitaph(
      NoReplyMustSendAccessDeniedEpitaphCompleter::Sync& txn) final {
    txn.Close(ZX_ERR_ACCESS_DENIED);
  }

  void MustSendAccessDeniedEpitaph(MustSendAccessDeniedEpitaphCompleter::Sync& txn) final {
    txn.Close(ZX_ERR_ACCESS_DENIED);
  }
};

void SpinUp(zx::channel server, Server* impl, async::Loop* loop) {
  zx_status_t status = fidl::BindSingleInFlightOnly(loop->dispatcher(), std::move(server), impl);
  ASSERT_OK(status);
}

// Block until the next dispatcher iteration.
// Due to an |async::Loop| dispatcher being used, once this task is handled,
// the server must have processed the return value from the handler.
void WaitUntilNextIteration(async_dispatcher_t* dispatcher) {
  zx::eventpair ep0, ep1;
  ASSERT_OK(zx::eventpair::create(0, &ep0, &ep1));
  async::PostTask(dispatcher,
                  [ep = std::move(ep1)]() { EXPECT_OK(ep.signal_peer(0, ZX_EVENTPAIR_SIGNALED)); });

  zx_signals_t signals = 0;
  ep0.wait_one(ZX_EVENTPAIR_SIGNALED, zx::time::infinite(), &signals);
  ASSERT_EQ(signals & ZX_EVENTPAIR_SIGNALED, ZX_EVENTPAIR_SIGNALED);
}

}  // namespace

TEST(ControlFlowTest, ServerShutdown) {
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_OK(loop->StartThread("test_llcpp_controlflow_server"));
  Server server_impl;

  constexpr uint32_t kNumIterations = 50;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    zx::channel client_chan, server_chan;
    ASSERT_OK(zx::channel::create(0, &client_chan, &server_chan));
    ASSERT_NO_FATAL_FAILURES(SpinUp(std::move(server_chan), &server_impl, loop.get()));

    // Send the shutdown message
    ASSERT_OK(fidl_test_llcpp_controlflow_ControlFlowShutdown(client_chan.get()));

    ASSERT_NO_FATAL_FAILURES(WaitUntilNextIteration(loop->dispatcher()));

    // Read-out the epitaph and check that epitaph error code is ZX_OK
    {
      fidl_epitaph_t epitaph = {};
      zx_handle_t tmp_handles[1] = {};
      uint32_t out_bytes = 0;
      uint32_t out_handles = 0;
      ASSERT_EQ(
          client_chan.read(0, &epitaph, tmp_handles, sizeof(epitaph), 1, &out_bytes, &out_handles),
          ZX_OK);
      ASSERT_EQ(out_bytes, sizeof(epitaph));
      ASSERT_EQ(out_handles, 0);
      ASSERT_OK(static_cast<zx_status_t>(epitaph.error));
    }

    // Verify that the remote end of |client_chan| has been closed
    {
      uint8_t tmp_bytes[1] = {};
      zx_handle_t tmp_handles[1] = {};
      uint32_t out_bytes = 0;
      uint32_t out_handles = 0;
      ASSERT_EQ(client_chan.read(0, tmp_bytes, tmp_handles, 1, 1, &out_bytes, &out_handles),
                ZX_ERR_PEER_CLOSED);
      ASSERT_EQ(out_bytes, 0);
      ASSERT_EQ(out_handles, 0);
    }
  }
}

TEST(ControlFlowTest, NoReplyMustSendEpitaph) {
  // Send epitaph from a call with no reply.
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_OK(loop->StartThread("test_llcpp_controlflow_server"));
  Server server_impl;

  constexpr uint32_t kNumIterations = 50;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    zx::channel client_chan, server_chan;
    ASSERT_OK(zx::channel::create(0, &client_chan, &server_chan));
    ASSERT_NO_FATAL_FAILURES(SpinUp(std::move(server_chan), &server_impl, loop.get()));

    // Send the epitaph request message
    ASSERT_EQ(fidl_test_llcpp_controlflow_ControlFlowNoReplyMustSendAccessDeniedEpitaph(
                  client_chan.get()),
              ZX_OK);

    ASSERT_NO_FATAL_FAILURES(WaitUntilNextIteration(loop->dispatcher()));

    // Read-out the epitaph and check the error code
    {
      fidl_epitaph_t epitaph = {};
      zx_handle_t tmp_handles[1] = {};
      uint32_t out_bytes = 0;
      uint32_t out_handles = 0;
      ASSERT_EQ(
          client_chan.read(0, &epitaph, tmp_handles, sizeof(epitaph), 1, &out_bytes, &out_handles),
          ZX_OK);
      ASSERT_EQ(out_bytes, sizeof(epitaph));
      ASSERT_EQ(out_handles, 0);
      ASSERT_EQ(static_cast<zx_status_t>(epitaph.error), ZX_ERR_ACCESS_DENIED);
    }

    // Verify that the remote end of |client_chan| has been closed
    {
      uint8_t tmp_bytes[1] = {};
      zx_handle_t tmp_handles[1] = {};
      uint32_t out_bytes = 0;
      uint32_t out_handles = 0;
      ASSERT_EQ(client_chan.read(0, tmp_bytes, tmp_handles, 1, 1, &out_bytes, &out_handles),
                ZX_ERR_PEER_CLOSED);
      ASSERT_EQ(out_bytes, 0);
      ASSERT_EQ(out_handles, 0);
    }
  }
}

TEST(ControlFlowTest, MustSendEpitaph) {
  // Send epitaph from a call with reply.
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_OK(loop->StartThread("test_llcpp_controlflow_server"));
  Server server_impl;

  constexpr uint32_t kNumIterations = 50;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    zx::channel client_chan, server_chan;
    ASSERT_OK(zx::channel::create(0, &client_chan, &server_chan));
    ASSERT_NO_FATAL_FAILURES(SpinUp(std::move(server_chan), &server_impl, loop.get()));

    // Manually write the epitaph request message, since the epitaph will cause the C bindings
    // to fail.
    fidl_test_llcpp_controlflow_ControlFlowMustSendAccessDeniedEpitaphRequest request = {};
    fidl_init_txn_header(&request.hdr, 0,
                         fidl_test_llcpp_controlflow_ControlFlowMustSendAccessDeniedEpitaphOrdinal);
    ASSERT_OK(client_chan.write(0, &request, sizeof(request), nullptr, 0));

    ASSERT_NO_FATAL_FAILURES(WaitUntilNextIteration(loop->dispatcher()));

    // Read-out the epitaph and check the error code
    {
      fidl_epitaph_t epitaph = {};
      zx_handle_t tmp_handles[1] = {};
      uint32_t out_bytes = 0;
      uint32_t out_handles = 0;
      ASSERT_EQ(
          client_chan.read(0, &epitaph, tmp_handles, sizeof(epitaph), 1, &out_bytes, &out_handles),
          ZX_OK);
      ASSERT_EQ(out_bytes, sizeof(epitaph));
      ASSERT_EQ(out_handles, 0);
      ASSERT_EQ(static_cast<zx_status_t>(epitaph.error), ZX_ERR_ACCESS_DENIED);
    }

    // Verify that the remote end of |client_chan| has been closed
    {
      uint8_t tmp_bytes[1] = {};
      zx_handle_t tmp_handles[1] = {};
      uint32_t out_bytes = 0;
      uint32_t out_handles = 0;
      ASSERT_EQ(client_chan.read(0, tmp_bytes, tmp_handles, 1, 1, &out_bytes, &out_handles),
                ZX_ERR_PEER_CLOSED);
      ASSERT_EQ(out_bytes, 0);
      ASSERT_EQ(out_handles, 0);
    }
  }
}
