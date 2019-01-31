// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <cstdlib>
#include <fidl/test/llcpp/dirent/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl-utils/bind.h>
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

// Interface under test.
#include "generated/fidl_llcpp_dirent.h"

// Namespace shorthand for bindings generated code
namespace gen = fidl::test::llcpp::dirent;

// Toy test data
namespace {

static_assert(gen::SMALL_DIR_VECTOR_SIZE == 3);

gen::DirEnt golden_dirents_array[gen::SMALL_DIR_VECTOR_SIZE] = {
    gen::DirEnt {
        .is_dir = false,
        .name = fidl::StringView { 2, const_cast<char*>("ab") },
        .some_flags = 0,
    },
    gen::DirEnt {
        .is_dir = true,
        .name = fidl::StringView { 3, const_cast<char*>("cde") },
        .some_flags = 1,
    },
    gen::DirEnt {
        .is_dir = false,
        .name = fidl::StringView { 4, const_cast<char*>("fghi") },
        .some_flags = 2,
    },
};

fidl::VectorView golden_dirents = fidl::VectorView { 3, golden_dirents_array };

}

// Manual server implementation, since the C binding does not support
// types with more than one level of indirection.
// The server is an async loop that reads messages from the channel.
// It uses the llcpp raw API to decode the message, then calls one of the handlers.
namespace internal_server {

class ManualServer {
public:
    ManualServer(zx::channel chan)
        : chan_(std::move(chan)), loop_(&kAsyncLoopConfigNoAttachToThread) {}

    zx_status_t Start() {
        zx_status_t status = loop_.StartThread("llcpp_manual_server");
        if (status != ZX_OK) {
            return status;
        }
        return fidl_bind(loop_.dispatcher(),
                         chan_.get(),
                         ManualServer::FidlDispatch,
                         this,
                         nullptr);
    }

    uint64_t CountNumDirectoriesNumCalls() const { return count_num_directories_num_calls_.load(); }

    uint64_t ReadDirNumCalls() const { return read_dir_num_calls_.load(); }

    uint64_t ConsumeDirectoriesNumCalls() const { return consume_directories_num_calls_.load(); }

    uint64_t OneWayDirentsNumCalls() const { return one_way_dirents_num_calls_.load(); }

private:
    template <typename FidlType>
    zx_status_t Reply(fidl_txn_t* txn, fidl::DecodedMessage<FidlType> response_message) {
        auto encode_result = fidl::Encode(std::move(response_message));
        if (encode_result.status != ZX_OK) {
            return encode_result.status;
        }
        auto& message = encode_result.message;
        fidl_msg_t msg = {
            .bytes = message.bytes().data(),
            .handles = message.handles().data(),
            .num_bytes = message.bytes().actual(),
            .num_handles = message.handles().actual()
        };
        zx_status_t status = txn->reply(txn, &msg);
        message.ReleaseBytesAndHandles();
        return status;
    }

    zx_status_t DoCountNumDirectories(
        fidl_txn_t* txn,
        fidl::DecodedMessage<gen::DirEntTestInterface::CountNumDirectoriesRequest> decoded
    ) {
        count_num_directories_num_calls_.fetch_add(1);
        const auto& request = *decoded.message();
        int64_t count = 0;
        for (const auto& dirent : request.dirents) {
            if (dirent.is_dir) {
                count++;
            }
        }
        gen::DirEntTestInterface::CountNumDirectoriesResponse response = {};
        response.num_dir = count;
        response._hdr.txid = request._hdr.txid;
        fidl::DecodedMessage<gen::DirEntTestInterface::CountNumDirectoriesResponse> response_msg;
        response_msg.Reset(fidl::BytePart(reinterpret_cast<uint8_t*>(&response),
                                          sizeof(response),
                                          sizeof(response)));
        return Reply(txn, std::move(response_msg));
    }

    zx_status_t DoReadDir(
        fidl_txn_t* txn,
        fidl::DecodedMessage<gen::DirEntTestInterface::ReadDirRequest> decoded
    ) {
        read_dir_num_calls_.fetch_add(1);
        gen::DirEntTestInterface::ReadDirResponse response = {};
        response._hdr.txid = decoded.message()->_hdr.txid;
        response.dirents = golden_dirents;
        uint8_t storage[256];
        auto result = fidl::Linearize(&response, fidl::BytePart(storage, sizeof(storage)));
        if (result.status != ZX_OK) {
            return result.status;
        }
        return Reply(txn, std::move(result.message));
    }

    zx_status_t DoConsumeDirectories(
        fidl_txn_t* txn,
        fidl::DecodedMessage<gen::DirEntTestInterface::ConsumeDirectoriesRequest> decoded
    ) {
        consume_directories_num_calls_.fetch_add(1);
        EXPECT_EQ(decoded.message()->dirents.count(), 3);
        gen::DirEntTestInterface::ConsumeDirectoriesResponse response = {};
        response._hdr.ordinal = decoded.message()->_hdr.ordinal;
        fidl::DecodedMessage<gen::DirEntTestInterface::ConsumeDirectoriesResponse> response_msg;
        response_msg.Reset(fidl::BytePart(reinterpret_cast<uint8_t*>(&response),
                                          sizeof(response),
                                          sizeof(response)));
        return Reply(txn, std::move(response_msg));
    }

    zx_status_t DoOneWayDirents(
        fidl_txn_t* txn,
        fidl::DecodedMessage<gen::DirEntTestInterface::OneWayDirentsRequest> decoded
    ) {
        one_way_dirents_num_calls_.fetch_add(1);
        EXPECT_EQ(decoded.message()->dirents.count(), 3);
        EXPECT_EQ(decoded.message()->ep.signal_peer(0, ZX_EVENTPAIR_SIGNALED), ZX_OK);
        // No response required for one-way calls.
        return ZX_OK;
    }

    template <typename FidlType>
    static fidl::DecodeResult<FidlType> DecodeAs(fidl_msg_t* msg) {
        fidl::EncodedMessage<FidlType> message;
        if (msg->num_handles > message.handles().capacity()) {
            zx_handle_close_many(msg->handles, msg->num_handles);
            return fidl::DecodeResult<FidlType>(ZX_ERR_INVALID_ARGS, "too many handles");
        }
        message.Initialize([&] (fidl::BytePart& bytes, fidl::HandlePart& handles) {
            bytes = fidl::BytePart(static_cast<uint8_t*>(msg->bytes),
                                   msg->num_bytes,
                                   msg->num_bytes);
            memcpy(handles.data(), msg->handles, sizeof(zx_handle_t) * msg->num_handles);
            handles.set_actual(msg->num_handles);
        });
        return fidl::Decode(std::move(message));
    }

    static zx_status_t FidlDispatch(void* ctx,
                                    fidl_txn_t* txn,
                                    fidl_msg_t* msg,
                                    const void* ops) {
        if (msg->num_bytes < sizeof(fidl_message_header_t)) {
            zx_handle_close_many(msg->handles, msg->num_handles);
            return ZX_ERR_INVALID_ARGS;
        }
        fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
        ManualServer* server = reinterpret_cast<ManualServer*>(ctx);
        switch (hdr->ordinal) {
            case fidl_test_llcpp_dirent_DirEntTestInterfaceCountNumDirectoriesOrdinal: {
                auto result = DecodeAs<gen::DirEntTestInterface::CountNumDirectoriesRequest>(msg);
                if (result.status != ZX_OK) {
                    return result.status;
                }
                return server->DoCountNumDirectories(txn, std::move(result.message));
            }
            case fidl_test_llcpp_dirent_DirEntTestInterfaceReadDirOrdinal: {
                auto result = DecodeAs<gen::DirEntTestInterface::ReadDirRequest>(msg);
                if (result.status != ZX_OK) {
                    return result.status;
                }
                return server->DoReadDir(txn, std::move(result.message));
            }
            case fidl_test_llcpp_dirent_DirEntTestInterfaceConsumeDirectoriesOrdinal: {
                auto result = DecodeAs<gen::DirEntTestInterface::ConsumeDirectoriesRequest>(msg);
                if (result.status != ZX_OK) {
                    return result.status;
                }
                return server->DoConsumeDirectories(txn, std::move(result.message));
            }
            case fidl_test_llcpp_dirent_DirEntTestInterfaceOneWayDirentsOrdinal: {
                auto result = DecodeAs<gen::DirEntTestInterface::OneWayDirentsRequest>(msg);
                if (result.status != ZX_OK) {
                    return result.status;
                }
                return server->DoOneWayDirents(txn, std::move(result.message));
            }
            default:
                return ZX_ERR_NOT_SUPPORTED;
        }
    }

    zx::channel chan_;
    async::Loop loop_;

    std::atomic<uint64_t> count_num_directories_num_calls_ = 0;
    std::atomic<uint64_t> read_dir_num_calls_ = 0;
    std::atomic<uint64_t> consume_directories_num_calls_ = 0;
    std::atomic<uint64_t> one_way_dirents_num_calls_ = 0;
};

} // namespace internal_server

namespace {

class Random {
public:
    Random(unsigned int seed = static_cast<unsigned int>(zx_ticks_get()))
        : seed_(seed) {}

    unsigned int seed() const { return seed_; }

    unsigned int UpTo(unsigned int limit) {
        unsigned int next = rand_r(&seed_);
        return next % limit;
    }

private:
    unsigned int seed_;
};

template <size_t kNumDirents>
fidl::ArrayWrapper<gen::DirEnt, kNumDirents> RandomlyFillDirEnt(char* name,
                                                                char* seed_description) {
    Random random;
    sprintf(seed_description, "Seed: %d", random.seed());
    fidl::ArrayWrapper<gen::DirEnt, kNumDirents> dirents;
    for (size_t i = 0; i < kNumDirents; i++) {
        int str_len = random.UpTo(gen::TEST_MAX_PATH) + 1;
        bool is_dir = random.UpTo(2) == 0;
        int32_t flags = static_cast<int32_t>(random.UpTo(1000));
        dirents[i] = gen::DirEnt {
            .is_dir = is_dir,
            .name = fidl::StringView { static_cast<uint64_t>(str_len), name },
            .some_flags = flags
        };
    }
    return dirents;
}

bool SimpleCountNumDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    constexpr size_t kNumDirents = 80;
    std::unique_ptr<char[]> name(new char[gen::TEST_MAX_PATH]);
    for (uint32_t i = 0; i < gen::TEST_MAX_PATH; i++) {
        name[i] = 'A';
    }
    ASSERT_EQ(server.CountNumDirectoriesNumCalls(), 0);
    constexpr uint64_t kNumIterations = 100;
    // Stress test linearizing dirents
    for (uint64_t iter = 0; iter < kNumIterations; iter++) {
        char seed_description[100] = {};
        auto dirents = RandomlyFillDirEnt<kNumDirents>(name.get(), seed_description);
        int64_t num_dir;
        zx_status_t status = client.CountNumDirectories(fidl::VectorView<gen::DirEnt> {
                                                            static_cast<uint64_t>(dirents.size()),
                                                            dirents.data()
                                                        },
                                                        &num_dir);
        int64_t expected_num_dir = 0;
        for (const auto& dirent : dirents) {
            if (dirent.is_dir) {
                expected_num_dir++;
            }
        }
        ASSERT_EQ(status, ZX_OK, seed_description);
        ASSERT_EQ(expected_num_dir, num_dir, seed_description);
    }
    ASSERT_EQ(server.CountNumDirectoriesNumCalls(), kNumIterations);

    END_TEST;
}

bool CallerAllocateCountNumDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    Random random;
    constexpr size_t kNumDirents = 80;
    std::unique_ptr<char[]> name(new char[gen::TEST_MAX_PATH]);
    for (uint32_t i = 0; i < gen::TEST_MAX_PATH; i++) {
        name[i] = 'B';
    }
    ASSERT_EQ(server.CountNumDirectoriesNumCalls(), 0);
    constexpr uint64_t kNumIterations = 100;
    // Stress test linearizing dirents
    for (uint64_t iter = 0; iter < kNumIterations; iter++) {
        char seed_description[100] = {};
        auto dirents = RandomlyFillDirEnt<kNumDirents>(name.get(), seed_description);
        int64_t num_dir;
        std::unique_ptr<uint8_t[]> request_buf(new uint8_t[ZX_CHANNEL_MAX_MSG_BYTES]);
        FIDL_ALIGNDECL uint8_t response_buf[128];
        zx_status_t status = client.CountNumDirectories(fidl::BytePart(request_buf.get(),
                                                                       ZX_CHANNEL_MAX_MSG_BYTES),
                                                        fidl::VectorView<gen::DirEnt> {
                                                            static_cast<uint64_t>(dirents.size()),
                                                            dirents.data()
                                                        },
                                                        fidl::BytePart(response_buf,
                                                                       sizeof(response_buf)),
                                                        &num_dir);
        int64_t expected_num_dir = 0;
        for (const auto& dirent : dirents) {
            if (dirent.is_dir) {
                expected_num_dir++;
            }
        }
        ASSERT_EQ(status, ZX_OK, seed_description);
        ASSERT_EQ(expected_num_dir, num_dir, seed_description);
    }
    ASSERT_EQ(server.CountNumDirectoriesNumCalls(), kNumIterations);

    END_TEST;
}

bool CallerAllocateReadDir() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    ASSERT_EQ(server.ReadDirNumCalls(), 0);
    constexpr uint64_t kNumIterations = 100;
    // Stress test server-linearizing dirents
    for (uint64_t iter = 0; iter < kNumIterations; iter++) {
        std::unique_ptr<uint8_t[]> response_buf(new uint8_t[ZX_CHANNEL_MAX_MSG_BYTES]);
        fidl::VectorView<gen::DirEnt> dirents;
        ASSERT_EQ(client.ReadDir(fidl::BytePart(response_buf.get(), ZX_CHANNEL_MAX_MSG_BYTES),
                                 &dirents),
                  ZX_OK);
        ASSERT_EQ(dirents.count(), golden_dirents.count());
        for (uint64_t i = 0; i < dirents.count(); i++) {
            auto actual = dirents[i];
            auto expected = golden_dirents[i];
            EXPECT_EQ(actual.is_dir, expected.is_dir);
            EXPECT_EQ(actual.some_flags, expected.some_flags);
            ASSERT_EQ(actual.name.size(), expected.name.size());
            EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(actual.name.data()),
                            reinterpret_cast<const uint8_t*>(expected.name.data()),
                            actual.name.size(),
                            "dirent name mismatch");
        }
    }
    ASSERT_EQ(server.ReadDirNumCalls(), kNumIterations);

    END_TEST;
}

bool InPlaceReadDir() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    ASSERT_EQ(server.ReadDirNumCalls(), 0);
    constexpr uint64_t kNumIterations = 100;
    // Stress test server-linearizing dirents
    for (uint64_t iter = 0; iter < kNumIterations; iter++) {
        std::unique_ptr<uint8_t[]> response_buf(new uint8_t[ZX_CHANNEL_MAX_MSG_BYTES]);
        auto result = client.ReadDir(fidl::BytePart(response_buf.get(), ZX_CHANNEL_MAX_MSG_BYTES));
        ASSERT_EQ(result.status, ZX_OK);
        const auto& dirents = result.message.message()->dirents;
        ASSERT_EQ(dirents.count(), golden_dirents.count());
        for (uint64_t i = 0; i < dirents.count(); i++) {
            auto actual = dirents[i];
            auto expected = golden_dirents[i];
            EXPECT_EQ(actual.is_dir, expected.is_dir);
            EXPECT_EQ(actual.some_flags, expected.some_flags);
            ASSERT_EQ(actual.name.size(), expected.name.size());
            EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(actual.name.data()),
                            reinterpret_cast<const uint8_t*>(expected.name.data()),
                            actual.name.size(),
                            "dirent name mismatch");
        }
    }
    ASSERT_EQ(server.ReadDirNumCalls(), kNumIterations);

    END_TEST;
}

bool SimpleConsumeDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    ASSERT_EQ(server.ConsumeDirectoriesNumCalls(), 0);
    ASSERT_EQ(client.ConsumeDirectories(golden_dirents), ZX_OK);
    ASSERT_EQ(server.ConsumeDirectoriesNumCalls(), 1);

    END_TEST;
}

bool CallerAllocateConsumeDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    ASSERT_EQ(server.ConsumeDirectoriesNumCalls(), 0);
    std::unique_ptr<uint8_t[]> request_buf(new uint8_t[ZX_CHANNEL_MAX_MSG_BYTES]);
    ASSERT_EQ(client.ConsumeDirectories(fidl::BytePart(request_buf.get(), ZX_CHANNEL_MAX_MSG_BYTES),
                                        golden_dirents),
              ZX_OK);
    ASSERT_EQ(server.ConsumeDirectoriesNumCalls(), 1);

    END_TEST;
}

bool InPlaceConsumeDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    ASSERT_EQ(server.ConsumeDirectoriesNumCalls(), 0);
    std::unique_ptr<uint8_t[]> request_buf(new uint8_t[ZX_CHANNEL_MAX_MSG_BYTES]);
    gen::DirEntTestInterface::ConsumeDirectoriesRequest request = {};
    request.dirents = golden_dirents;
    auto linearize_result = fidl::Linearize(&request,
                                            fidl::BytePart(request_buf.get(),
                                                           ZX_CHANNEL_MAX_MSG_BYTES));
    ASSERT_EQ(linearize_result.status, ZX_OK);
    ASSERT_EQ(client.ConsumeDirectories(std::move(linearize_result.message)), ZX_OK);
    ASSERT_EQ(server.ConsumeDirectoriesNumCalls(), 1);

    END_TEST;
}

bool SimpleOneWayDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    zx::eventpair client_ep, server_ep;
    ASSERT_EQ(zx::eventpair::create(0, &client_ep, &server_ep), ZX_OK);
    ASSERT_EQ(server.OneWayDirentsNumCalls(), 0);
    ASSERT_EQ(client.OneWayDirents(golden_dirents, std::move(server_ep)), ZX_OK);
    zx_signals_t signals = 0;
    client_ep.wait_one(ZX_EVENTPAIR_SIGNALED, zx::time::infinite(), &signals);
    ASSERT_EQ(signals & ZX_EVENTPAIR_SIGNALED, ZX_EVENTPAIR_SIGNALED);
    ASSERT_EQ(server.OneWayDirentsNumCalls(), 1);

    END_TEST;
}

bool CallerAllocateOneWayDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    zx::eventpair client_ep, server_ep;
    ASSERT_EQ(zx::eventpair::create(0, &client_ep, &server_ep), ZX_OK);
    ASSERT_EQ(server.OneWayDirentsNumCalls(), 0);
    uint8_t request_buf[512];
    ASSERT_EQ(client.OneWayDirents(fidl::BytePart(request_buf, sizeof(request_buf)),
                                   golden_dirents, std::move(server_ep)),
              ZX_OK);
    zx_signals_t signals = 0;
    client_ep.wait_one(ZX_EVENTPAIR_SIGNALED, zx::time::infinite(), &signals);
    ASSERT_EQ(signals & ZX_EVENTPAIR_SIGNALED, ZX_EVENTPAIR_SIGNALED);
    ASSERT_EQ(server.OneWayDirentsNumCalls(), 1);

    END_TEST;
}

bool InPlaceOneWayDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    internal_server::ManualServer server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    zx::eventpair client_ep, server_ep;
    ASSERT_EQ(zx::eventpair::create(0, &client_ep, &server_ep), ZX_OK);
    ASSERT_EQ(server.OneWayDirentsNumCalls(), 0);
    std::unique_ptr<uint8_t[]> request_buf(new uint8_t[ZX_CHANNEL_MAX_MSG_BYTES]);
    gen::DirEntTestInterface::OneWayDirentsRequest request = {};
    request.dirents = golden_dirents;
    request.ep = std::move(server_ep);
    auto linearize_result = fidl::Linearize(&request,
                                            fidl::BytePart(request_buf.get(),
                                                           ZX_CHANNEL_MAX_MSG_BYTES));
    ASSERT_EQ(linearize_result.status, ZX_OK);
    ASSERT_EQ(client.OneWayDirents(std::move(linearize_result.message)), ZX_OK);
    zx_signals_t signals = 0;
    client_ep.wait_one(ZX_EVENTPAIR_SIGNALED, zx::time::infinite(), &signals);
    ASSERT_EQ(signals & ZX_EVENTPAIR_SIGNALED, ZX_EVENTPAIR_SIGNALED);
    ASSERT_EQ(server.OneWayDirentsNumCalls(), 1);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(llcpp_interface_dirent_tests)
RUN_NAMED_TEST_SMALL("CountNumDirectories, C-flavor", SimpleCountNumDirectories)
RUN_NAMED_TEST_SMALL("CountNumDirectories, caller-allocating", CallerAllocateCountNumDirectories)
RUN_NAMED_TEST_SMALL("ReadDir, caller-allocating", CallerAllocateReadDir)
RUN_NAMED_TEST_SMALL("ReadDir, in-place", InPlaceReadDir)
RUN_NAMED_TEST_SMALL("ConsumeDirectories, C-flavor", SimpleConsumeDirectories)
RUN_NAMED_TEST_SMALL("ConsumeDirectories, caller-allocating", CallerAllocateConsumeDirectories)
RUN_NAMED_TEST_SMALL("ConsumeDirectories, in-place", InPlaceConsumeDirectories)
RUN_NAMED_TEST_SMALL("OneWayDirents, C-flavor", SimpleOneWayDirents)
RUN_NAMED_TEST_SMALL("OneWayDirents, caller-allocating", CallerAllocateOneWayDirents)
RUN_NAMED_TEST_SMALL("OneWayDirents, in-place", InPlaceOneWayDirents)
END_TEST_CASE(llcpp_interface_dirent_tests);

