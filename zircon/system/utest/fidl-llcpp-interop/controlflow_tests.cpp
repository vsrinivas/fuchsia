// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <fbl/vector.h>
#include <fidl/test/llcpp/controlflow/c/fidl.h>
#include <lib/async/wait.h>
#include <lib/async/cpp/task.h>
#include <lib/async-loop/loop.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <memory>
#include <string.h>
#include <unittest/unittest.h>
#include <utility>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

// Interface under test
#include "generated/fidl_llcpp_controlflow.h"

// Control flow tests: manually interact with a server and test for epitaph and shutdown.
namespace {

namespace gen = fidl::test::llcpp::controlflow;

class Server : public gen::ControlFlow::Interface {
public:
    void Shutdown(ShutdownCompleter::Sync txn) final {
        txn.Close(ZX_OK);
    }

    void NoReplyMustSendAccessDeniedEpitaph(
        NoReplyMustSendAccessDeniedEpitaphCompleter::Sync txn) final {
        txn.Close(ZX_ERR_ACCESS_DENIED);
    }

    void MustSendAccessDeniedEpitaph(MustSendAccessDeniedEpitaphCompleter::Sync txn) final {
        txn.Close(ZX_ERR_ACCESS_DENIED);
    }
};

bool SpinUp(zx::channel server, Server* impl, async::Loop* loop) {
    BEGIN_HELPER;

    zx_status_t status = fidl::Bind(loop->dispatcher(),
                                    std::move(server),
                                    impl);
    ASSERT_EQ(status, ZX_OK);

    END_HELPER;
}

// Block until the next dispatcher iteration.
// Due to an |async::Loop| dispatcher being used, once this task is handled,
// the server must have processed the return value from the handler.
bool WaitUntilNextIteration(async_dispatcher_t* dispatcher) {
    BEGIN_HELPER;

    zx::eventpair ep0, ep1;
    ASSERT_EQ(zx::eventpair::create(0, &ep0, &ep1), ZX_OK);
    async::PostTask(dispatcher, [ep = std::move(ep1), &current_test_info] () {
        EXPECT_EQ(ep.signal_peer(0, ZX_EVENTPAIR_SIGNALED), ZX_OK);
    });

    zx_signals_t signals = 0;
    ep0.wait_one(ZX_EVENTPAIR_SIGNALED, zx::time::infinite(), &signals);
    ASSERT_EQ(signals & ZX_EVENTPAIR_SIGNALED, ZX_EVENTPAIR_SIGNALED);

    END_HELPER;
}

bool ServerShutdownTest() {
    BEGIN_TEST;

    auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToThread);
    ASSERT_EQ(loop->StartThread("test_llcpp_controlflow_server"), ZX_OK);
    Server server_impl;

    constexpr uint32_t kNumIterations = 50;
    for (uint32_t i = 0; i < kNumIterations; i++) {
        zx::channel client_chan, server_chan;
        ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
        ASSERT_TRUE(SpinUp(std::move(server_chan), &server_impl, loop.get()));

        // Send the shutdown message
        ASSERT_EQ(fidl_test_llcpp_controlflow_ControlFlowShutdown(client_chan.get()), ZX_OK);

        ASSERT_TRUE(WaitUntilNextIteration(loop->dispatcher()));

        // Read-out the epitaph and check that epitaph error code is ZX_OK
        {
            fidl_epitaph_t epitaph = {};
            zx_handle_t tmp_handles[1] = {};
            uint32_t out_bytes = 0;
            uint32_t out_handles = 0;
            ASSERT_EQ(client_chan.read(0,
                                       &epitaph, tmp_handles, sizeof(epitaph), 1,
                                       &out_bytes, &out_handles),
                      ZX_OK);
            ASSERT_EQ(out_bytes, sizeof(epitaph));
            ASSERT_EQ(out_handles, 0);
            ASSERT_EQ(static_cast<zx_status_t>(epitaph.hdr.reserved0), ZX_OK);
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

    END_TEST;
}

bool NoReplyMustSendEpitaphTest() {
    BEGIN_TEST;

    auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToThread);
    ASSERT_EQ(loop->StartThread("test_llcpp_controlflow_server"), ZX_OK);
    Server server_impl;

    constexpr uint32_t kNumIterations = 50;
    for (uint32_t i = 0; i < kNumIterations; i++) {
        zx::channel client_chan, server_chan;
        ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
        ASSERT_TRUE(SpinUp(std::move(server_chan), &server_impl, loop.get()));

        // Send the epitaph request message
        ASSERT_EQ(fidl_test_llcpp_controlflow_ControlFlowNoReplyMustSendAccessDeniedEpitaph(
            client_chan.get()), ZX_OK);

        ASSERT_TRUE(WaitUntilNextIteration(loop->dispatcher()));

        // Read-out the epitaph and check the error code
        {
            fidl_epitaph_t epitaph = {};
            zx_handle_t tmp_handles[1] = {};
            uint32_t out_bytes = 0;
            uint32_t out_handles = 0;
            ASSERT_EQ(client_chan.read(0,
                                       &epitaph, tmp_handles, sizeof(epitaph), 1,
                                       &out_bytes, &out_handles),
                      ZX_OK);
            ASSERT_EQ(out_bytes, sizeof(epitaph));
            ASSERT_EQ(out_handles, 0);
            ASSERT_EQ(static_cast<zx_status_t>(epitaph.hdr.reserved0), ZX_ERR_ACCESS_DENIED);
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

    END_TEST;
}

bool MustSendEpitaphTest() {
    BEGIN_TEST;

    auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToThread);
    ASSERT_EQ(loop->StartThread("test_llcpp_controlflow_server"), ZX_OK);
    Server server_impl;

    constexpr uint32_t kNumIterations = 50;
    for (uint32_t i = 0; i < kNumIterations; i++) {
        zx::channel client_chan, server_chan;
        ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
        ASSERT_TRUE(SpinUp(std::move(server_chan), &server_impl, loop.get()));

        // Manually write the epitaph request message, since the epitaph will cause the C bindings
        // to fail.
        fidl_test_llcpp_controlflow_ControlFlowMustSendAccessDeniedEpitaphRequest request = {};
        request.hdr.ordinal =
            fidl_test_llcpp_controlflow_ControlFlowMustSendAccessDeniedEpitaphOrdinal;
        ASSERT_EQ(client_chan.write(0, &request, sizeof(request), nullptr, 0), ZX_OK);

        ASSERT_TRUE(WaitUntilNextIteration(loop->dispatcher()));

        // Read-out the epitaph and check the error code
        {
            fidl_epitaph_t epitaph = {};
            zx_handle_t tmp_handles[1] = {};
            uint32_t out_bytes = 0;
            uint32_t out_handles = 0;
            ASSERT_EQ(client_chan.read(0,
                                       &epitaph, tmp_handles, sizeof(epitaph), 1,
                                       &out_bytes, &out_handles),
                      ZX_OK);
            ASSERT_EQ(out_bytes, sizeof(epitaph));
            ASSERT_EQ(out_handles, 0);
            ASSERT_EQ(static_cast<zx_status_t>(epitaph.hdr.reserved0), ZX_ERR_ACCESS_DENIED);
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

    END_TEST;
}

}

BEGIN_TEST_CASE(llcpp_controlflow_tests)
RUN_NAMED_TEST_SMALL("shutdown", ServerShutdownTest)
RUN_NAMED_TEST_SMALL("send epitaph from a call with no reply",
                     NoReplyMustSendEpitaphTest)
RUN_NAMED_TEST_SMALL("send epitaph from a call with reply",
                     MustSendEpitaphTest)
END_TEST_CASE(llcpp_controlflow_tests)
