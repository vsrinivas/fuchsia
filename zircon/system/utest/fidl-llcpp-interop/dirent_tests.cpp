// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <cstdlib>
#include <fidl/test/llcpp/dirent/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl-async/cpp/bind.h>
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
        .name = fidl::StringView { 2, "ab" },
        .some_flags = 0,
    },
    gen::DirEnt {
        .is_dir = true,
        .name = fidl::StringView { 3, "cde" },
        .some_flags = 1,
    },
    gen::DirEnt {
        .is_dir = false,
        .name = fidl::StringView { 4, "fghi" },
        .some_flags = 2,
    },
};

fidl::VectorView golden_dirents = fidl::VectorView { 3, golden_dirents_array };

}

// Manual server implementation, since the C binding does not support
// types with more than one level of indirection.
// The server is an async loop that reads messages from the channel.
// It uses the llcpp raw API to decode the message, then calls one of the handlers.
namespace manual_server {

class Server {
public:
    Server(zx::channel chan)
        : chan_(std::move(chan)), loop_(&kAsyncLoopConfigNoAttachToThread) {}

    zx_status_t Start() {
        zx_status_t status = loop_.StartThread("llcpp_manual_server");
        if (status != ZX_OK) {
            return status;
        }
        return fidl_bind(loop_.dispatcher(),
                         chan_.get(),
                         Server::FidlDispatch,
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
        if (msg->num_handles > fidl::EncodedMessage<FidlType>::kResolvedMaxHandles) {
            zx_handle_close_many(msg->handles, msg->num_handles);
            return fidl::DecodeResult<FidlType>(ZX_ERR_INVALID_ARGS, "too many handles");
        }
        return fidl::Decode(fidl::EncodedMessage<FidlType>(msg));
    }

    static zx_status_t FidlDispatch(void* ctx,
                                    fidl_txn_t* txn,
                                    fidl_msg_t* msg,
                                    const void* ops) {
        if (msg->num_bytes < sizeof(fidl_message_header_t)) {
            zx_handle_close_many(msg->handles, msg->num_handles);
            return ZX_ERR_INVALID_ARGS;
        }
        fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
        Server* server = reinterpret_cast<Server*>(ctx);
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

// Server implemented with low-level C++ FIDL bindings
namespace llcpp_server {

class ServerBase : public gen::DirEntTestInterface::Interface {
public:
    ServerBase(zx::channel chan)
        : chan_(std::move(chan)), loop_(&kAsyncLoopConfigNoAttachToThread) {}

    zx_status_t Start() {
        zx_status_t status = loop_.StartThread("llcpp_bindings_server");
        if (status != ZX_OK) {
            return status;
        }
        return fidl::Bind(loop_.dispatcher(),
                          std::move(chan_),
                          this);
    }

    uint64_t CountNumDirectoriesNumCalls() const { return count_num_directories_num_calls_.load(); }

    uint64_t ReadDirNumCalls() const { return read_dir_num_calls_.load(); }

    uint64_t ConsumeDirectoriesNumCalls() const { return consume_directories_num_calls_.load(); }

    uint64_t OneWayDirentsNumCalls() const { return one_way_dirents_num_calls_.load(); }

protected:
    async_dispatcher_t* dispatcher() const { return loop_.dispatcher(); }

    std::atomic<uint64_t> count_num_directories_num_calls_ = 0;
    std::atomic<uint64_t> read_dir_num_calls_ = 0;
    std::atomic<uint64_t> consume_directories_num_calls_ = 0;
    std::atomic<uint64_t> one_way_dirents_num_calls_ = 0;

private:
    zx::channel chan_;
    async::Loop loop_;
};

// There are three implementations each exercising a different flavor of the reply API:
// C-style, caller-allocating, in-place, and async.

class CFlavorServer : public ServerBase {
public:
    CFlavorServer(zx::channel chan) : ServerBase(std::move(chan)) {}

    void CountNumDirectories(fidl::VectorView<gen::DirEnt> dirents,
                             CountNumDirectoriesCompleter::Sync txn) override {
        count_num_directories_num_calls_.fetch_add(1);
        int64_t count = 0;
        for (const auto& dirent : dirents) {
            if (dirent.is_dir) {
                count++;
            }
        }
        txn.Reply(count);
    }

    void ReadDir(ReadDirCompleter::Sync txn) override {
        read_dir_num_calls_.fetch_add(1);
        txn.Reply(golden_dirents);
    }

    // |ConsumeDirectories| has zero number of arguments in its return value, hence only the
    // C-flavor reply API is generated.
    void ConsumeDirectories(fidl::VectorView<gen::DirEnt> dirents,
                            ConsumeDirectoriesCompleter::Sync txn) override {
        consume_directories_num_calls_.fetch_add(1);
        EXPECT_EQ(dirents.count(), 3);
        txn.Reply();
    }

    // |OneWayDirents| has no return value, hence there is no reply API generated
    void OneWayDirents(fidl::VectorView<gen::DirEnt> dirents,
                              zx::eventpair ep,
                              OneWayDirentsCompleter::Sync txn) override {
        one_way_dirents_num_calls_.fetch_add(1);
        EXPECT_EQ(dirents.count(), 3);
        EXPECT_EQ(ep.signal_peer(0, ZX_EVENTPAIR_SIGNALED), ZX_OK);
        // No response required for one-way calls.
    }
};

class CallerAllocateServer : public ServerBase {
public:
    CallerAllocateServer(zx::channel chan) : ServerBase(std::move(chan)) {}

    void CountNumDirectories(fidl::VectorView<gen::DirEnt> dirents,
                             CountNumDirectoriesCompleter::Sync txn) override {
        count_num_directories_num_calls_.fetch_add(1);
        int64_t count = 0;
        for (const auto& dirent : dirents) {
            if (dirent.is_dir) {
                count++;
            }
        }
        uint8_t storage[256];
        txn.Reply(::fidl::BytePart(storage, sizeof(storage)), count);
    }

    void ReadDir(ReadDirCompleter::Sync txn) override {
        read_dir_num_calls_.fetch_add(1);
        uint8_t storage[256];
        txn.Reply(fidl::BytePart(storage, sizeof(storage)), golden_dirents);
    }

    // |ConsumeDirectories| has zero number of arguments in its return value, hence only the
    // C-flavor reply API is applicable.
    void ConsumeDirectories(fidl::VectorView<gen::DirEnt> dirents,
                            ConsumeDirectoriesCompleter::Sync txn) override {
        ZX_ASSERT_MSG(false, "Never used by unit tests");
    }

    // |OneWayDirents| has no return value, hence there is no reply API generated
    void OneWayDirents(fidl::VectorView<gen::DirEnt> dirents,
                              zx::eventpair ep,
                              OneWayDirentsCompleter::Sync) override {
        ZX_ASSERT_MSG(false, "Never used by unit tests");
    }
};

class InPlaceServer : public ServerBase {
public:
    InPlaceServer(zx::channel chan) : ServerBase(std::move(chan)) {}

    void CountNumDirectories(fidl::VectorView<gen::DirEnt> dirents,
                             CountNumDirectoriesCompleter::Sync txn) override {
        count_num_directories_num_calls_.fetch_add(1);
        int64_t count = 0;
        for (const auto& dirent : dirents) {
            if (dirent.is_dir) {
                count++;
            }
        }
        gen::DirEntTestInterface::CountNumDirectoriesResponse response = {};
        response.num_dir = count;
        fidl::DecodedMessage<gen::DirEntTestInterface::CountNumDirectoriesResponse> response_msg;
        response_msg.Reset(fidl::BytePart(reinterpret_cast<uint8_t*>(&response),
                                          sizeof(response),
                                          sizeof(response)));
        txn.Reply(std::move(response_msg));
    }

    void ReadDir(ReadDirCompleter::Sync txn) override {
        read_dir_num_calls_.fetch_add(1);
        gen::DirEntTestInterface::ReadDirResponse response = {};
        response.dirents = golden_dirents;
        uint8_t storage[256];
        auto result = fidl::Linearize(&response, fidl::BytePart(storage, sizeof(storage)));
        if (result.status != ZX_OK) {
            txn.Close(result.status);
            return;
        }
        txn.Reply(std::move(result.message));
    }

    // |ConsumeDirectories| has zero number of arguments in its return value, hence only the
    // C-flavor reply API is applicable.
    void ConsumeDirectories(fidl::VectorView<gen::DirEnt> dirents,
                            ConsumeDirectoriesCompleter::Sync txn) override {
        ZX_ASSERT_MSG(false, "Never used by unit tests");
    }

    // |OneWayDirents| has no return value, hence there is no reply API generated
    void OneWayDirents(fidl::VectorView<gen::DirEnt> dirents,
                       zx::eventpair ep,
                       OneWayDirentsCompleter::Sync) override {
        ZX_ASSERT_MSG(false, "Never used by unit tests");
    }
};

// Every reply is delayed using async::PostTask
class AsyncReplyServer : public ServerBase {
public:
    AsyncReplyServer(zx::channel chan) : ServerBase(std::move(chan)) {}

    void CountNumDirectories(fidl::VectorView<gen::DirEnt> dirents,
                             CountNumDirectoriesCompleter::Sync txn) override {
        count_num_directories_num_calls_.fetch_add(1);
        int64_t count = 0;
        for (const auto& dirent : dirents) {
            if (dirent.is_dir) {
                count++;
            }
        }
        async::PostTask(dispatcher(), [txn = txn.ToAsync(), count] () mutable {
            txn.Reply(count);
        });
    }

    void ReadDir(ReadDirCompleter::Sync txn) override {
        read_dir_num_calls_.fetch_add(1);
        async::PostTask(dispatcher(), [txn = txn.ToAsync()] () mutable {
            txn.Reply(golden_dirents);
        });
    }

    void ConsumeDirectories(fidl::VectorView<gen::DirEnt> dirents,
                            ConsumeDirectoriesCompleter::Sync txn) override {
        consume_directories_num_calls_.fetch_add(1);
        EXPECT_EQ(dirents.count(), 3);
        async::PostTask(dispatcher(), [txn = txn.ToAsync()] () mutable {
            txn.Reply();
        });
    }

    // |OneWayDirents| has no return value, hence there is no reply API generated
    void OneWayDirents(fidl::VectorView<gen::DirEnt> dirents,
                       zx::eventpair ep,
                       OneWayDirentsCompleter::Sync) override {
        ZX_ASSERT_MSG(false, "Never used by unit tests");
    }
};

} // namespace llcpp_server

// Parametric tests allowing choosing a custom server implementation
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
fidl::Array<gen::DirEnt, kNumDirents> RandomlyFillDirEnt(char* name,
                                                         char* seed_description) {
    Random random;
    sprintf(seed_description, "Seed: %d", random.seed());
    fidl::Array<gen::DirEnt, kNumDirents> dirents;
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

template <typename Server>
bool SimpleCountNumDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
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

template <typename Server>
bool CallerAllocateCountNumDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
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

template <typename Server>
bool CallerAllocateReadDir() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
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

template <typename Server>
bool InPlaceReadDir() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
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

template <typename Server>
bool SimpleConsumeDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    ASSERT_EQ(server.ConsumeDirectoriesNumCalls(), 0);
    ASSERT_EQ(client.ConsumeDirectories(golden_dirents), ZX_OK);
    ASSERT_EQ(server.ConsumeDirectoriesNumCalls(), 1);

    END_TEST;
}

template <typename Server>
bool CallerAllocateConsumeDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
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

template <typename Server>
bool InPlaceConsumeDirectories() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
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

template <typename Server>
bool SimpleOneWayDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
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

template <typename Server>
bool CallerAllocateOneWayDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
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

template <typename Server>
bool InPlaceOneWayDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);
    Server server(std::move(server_chan));
    ASSERT_EQ(server.Start(), ZX_OK);
    gen::DirEntTestInterface::SyncClient client(std::move(client_chan));

    constexpr uint64_t kNumIterations = 100;
    for (uint64_t iter = 0; iter < kNumIterations; iter++) {
        zx::eventpair client_ep, server_ep;
        ASSERT_EQ(zx::eventpair::create(0, &client_ep, &server_ep), ZX_OK);
        ASSERT_EQ(server.OneWayDirentsNumCalls(), iter);
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
        ASSERT_EQ(server.OneWayDirentsNumCalls(), iter + 1);
    }

    END_TEST;
}

template <typename DirentArray>
bool AssertReadOnDirentsEvent(const zx::channel& chan, const DirentArray& expected_dirents) {
    BEGIN_HELPER;

    auto buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
    uint32_t actual = 0;
    ASSERT_EQ(chan.read(0, buffer.get(), nullptr, ZX_CHANNEL_MAX_MSG_BYTES, 0, &actual, nullptr),
              ZX_OK);
    ASSERT_GE(actual, sizeof(fidl_message_header_t));
    ASSERT_EQ(reinterpret_cast<fidl_message_header_t*>(buffer.get())->ordinal,
              fidl_test_llcpp_dirent_DirEntTestInterfaceOnDirentsOrdinal);
    ASSERT_EQ(reinterpret_cast<fidl_message_header_t*>(buffer.get())->txid, 0);
    auto encoded_message = ::fidl::EncodedMessage<gen::DirEntTestInterface::OnDirentsResponse>(
        ::fidl::BytePart(buffer.get(), ZX_CHANNEL_MAX_MSG_BYTES, actual));
    auto decode_result = ::fidl::Decode(std::move(encoded_message));
    ASSERT_EQ(decode_result.status, ZX_OK, decode_result.error);

    const auto& message = *decode_result.message.message();
    ASSERT_EQ(message.dirents.count(), expected_dirents.size());
    for (uint64_t i = 0; i < message.dirents.count(); i++) {
        ASSERT_EQ(message.dirents[i].is_dir, expected_dirents[i].is_dir);
        ASSERT_EQ(message.dirents[i].some_flags, expected_dirents[i].some_flags);
        ASSERT_EQ(message.dirents[i].name.size(), expected_dirents[i].name.size());
        ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(message.dirents[i].name.data()),
                        reinterpret_cast<const uint8_t*>(expected_dirents[i].name.data()),
                        message.dirents[i].name.size(),
                        "dirent name mismatch");
    }

    END_HELPER;
}

bool CFlavorSendOnDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);

    constexpr size_t kNumDirents = 80;
    std::unique_ptr<char[]> name(new char[gen::TEST_MAX_PATH]);
    for (uint32_t i = 0; i < gen::TEST_MAX_PATH; i++) {
        name[i] = 'A';
    }
    char seed_description[100] = {};
    auto dirents = RandomlyFillDirEnt<kNumDirents>(name.get(), seed_description);
    auto status = gen::DirEntTestInterface::SendOnDirentsEvent(zx::unowned_channel(server_chan),
                                                               fidl::VectorView<gen::DirEnt> {
                                                                   static_cast<uint64_t>(
                                                                       dirents.size()),
                                                                   dirents.data()
                                                               });
    ASSERT_EQ(status, ZX_OK, seed_description);
    ASSERT_TRUE(AssertReadOnDirentsEvent(client_chan, dirents), seed_description);

    END_TEST;
}

bool CallerAllocateSendOnDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);

    constexpr size_t kNumDirents = 80;
    std::unique_ptr<char[]> name(new char[gen::TEST_MAX_PATH]);
    for (uint32_t i = 0; i < gen::TEST_MAX_PATH; i++) {
        name[i] = 'B';
    }
    char seed_description[100] = {};
    auto dirents = RandomlyFillDirEnt<kNumDirents>(name.get(), seed_description);
    auto storage = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
    ::fidl::BytePart bytes(storage.get(), ZX_CHANNEL_MAX_MSG_BYTES);
    auto status = gen::DirEntTestInterface::SendOnDirentsEvent(zx::unowned_channel(server_chan),
                                                               std::move(bytes),
                                                               fidl::VectorView<gen::DirEnt> {
                                                                   static_cast<uint64_t>(
                                                                       dirents.size()),
                                                                   dirents.data()
                                                               });
    ASSERT_EQ(status, ZX_OK, seed_description);
    ASSERT_TRUE(AssertReadOnDirentsEvent(client_chan, dirents), seed_description);

    END_TEST;
}

bool InPlaceSendOnDirents() {
    BEGIN_TEST;

    zx::channel client_chan, server_chan;
    ASSERT_EQ(zx::channel::create(0, &client_chan, &server_chan), ZX_OK);

    constexpr size_t kNumDirents = 80;
    std::unique_ptr<char[]> name(new char[gen::TEST_MAX_PATH]);
    for (uint32_t i = 0; i < gen::TEST_MAX_PATH; i++) {
        name[i] = 'C';
    }
    char seed_description[100] = {};
    auto dirents = RandomlyFillDirEnt<kNumDirents>(name.get(), seed_description);
    auto storage = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
    ::gen::DirEntTestInterface::OnDirentsResponse event = {};
    event.dirents = fidl::VectorView<gen::DirEnt> {
        static_cast<uint64_t>(
            dirents.size()),
        dirents.data()
    };
    auto linearize_result = ::fidl::Linearize(&event,
                                              ::fidl::BytePart(storage.get(),
                                                               ZX_CHANNEL_MAX_MSG_BYTES));
    ASSERT_EQ(linearize_result.status, ZX_OK, linearize_result.error);
    auto status = gen::DirEntTestInterface::SendOnDirentsEvent(zx::unowned_channel(server_chan),
                                                               std::move(linearize_result.message));
    ASSERT_EQ(status, ZX_OK, seed_description);
    ASSERT_TRUE(AssertReadOnDirentsEvent(client_chan, dirents), seed_description);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(llcpp_interface_dirent_tests)
RUN_NAMED_TEST_SMALL("client: CountNumDirectories, C-flavor",
                     SimpleCountNumDirectories<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: CountNumDirectories, caller-allocating",
                     CallerAllocateCountNumDirectories<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: ReadDir, caller-allocating",
                     CallerAllocateReadDir<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: ReadDir, in-place",
                     InPlaceReadDir<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: ConsumeDirectories, C-flavor",
                     SimpleConsumeDirectories<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: ConsumeDirectories, caller-allocating",
                     CallerAllocateConsumeDirectories<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: ConsumeDirectories, in-place",
                     InPlaceConsumeDirectories<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: OneWayDirents, C-flavor",
                     SimpleOneWayDirents<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: OneWayDirents, caller-allocating",
                     CallerAllocateOneWayDirents<manual_server::Server>)
RUN_NAMED_TEST_SMALL("client: OneWayDirents, in-place",
                     InPlaceOneWayDirents<manual_server::Server>)

RUN_NAMED_TEST_SMALL("server: CountNumDirectories, C-flavor",
                     SimpleCountNumDirectories<llcpp_server::CFlavorServer>)
RUN_NAMED_TEST_SMALL("server: CountNumDirectories, caller-allocating",
                     SimpleCountNumDirectories<llcpp_server::CallerAllocateServer>)
RUN_NAMED_TEST_SMALL("server: CountNumDirectories, in-place",
                     SimpleCountNumDirectories<llcpp_server::InPlaceServer>)
RUN_NAMED_TEST_SMALL("server: CountNumDirectories, async",
                     SimpleCountNumDirectories<llcpp_server::AsyncReplyServer>)
RUN_NAMED_TEST_SMALL("server: ReadDir, C-flavor",
                     InPlaceReadDir<llcpp_server::CFlavorServer>)
RUN_NAMED_TEST_SMALL("server: ReadDir, caller-allocating",
                     InPlaceReadDir<llcpp_server::CallerAllocateServer>)
RUN_NAMED_TEST_SMALL("server: ReadDir, in-place",
                     InPlaceReadDir<llcpp_server::InPlaceServer>)
RUN_NAMED_TEST_SMALL("server: ReadDir, async",
                     InPlaceReadDir<llcpp_server::AsyncReplyServer>)
RUN_NAMED_TEST_SMALL("server: ConsumeDirectories, C-flavor",
                     SimpleConsumeDirectories<llcpp_server::CFlavorServer>)
RUN_NAMED_TEST_SMALL("server: ConsumeDirectories, async",
                     SimpleConsumeDirectories<llcpp_server::AsyncReplyServer>)
RUN_NAMED_TEST_SMALL("server: OneWayDirents, C-flavor",
                     SimpleOneWayDirents<llcpp_server::CFlavorServer>)

RUN_NAMED_TEST_SMALL("server: Send OnDirents event, C-flavor",
                     CFlavorSendOnDirents)
RUN_NAMED_TEST_SMALL("server: Send OnDirents event, caller-allocating",
                     CallerAllocateSendOnDirents)
RUN_NAMED_TEST_SMALL("server: Send OnDirents event, in-place",
                     InPlaceSendOnDirents)
END_TEST_CASE(llcpp_interface_dirent_tests)
