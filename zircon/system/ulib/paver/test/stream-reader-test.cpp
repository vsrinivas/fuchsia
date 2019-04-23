// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream-reader.h"

#include <fcntl.h>

#include <fuchsia/paver/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-utils/bind.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kFileData[] = "lalalala";

TEST(StreamReaderTest, InvalidChannel) {
    std::unique_ptr<paver::StreamReader> reader;
    ASSERT_NE(paver::StreamReader::Create(zx::channel(), &reader), ZX_OK);
}

class StreamReaderTest : public zxtest::Test {
public:
    StreamReaderTest()
        : loop_(&kAsyncLoopConfigAttachToThread) {
        zx::channel server;
        ASSERT_OK(zx::channel::create(0, &client_, &server));
        fidl_bind(loop_.dispatcher(), server.release(),
                  reinterpret_cast<fidl_dispatch_t*>(fuchsia_paver_PayloadStream_dispatch),
                  this, &ops_);
        loop_.StartThread("payload-stream-test-loop");
    }

    zx_status_t ReadSuccess(fidl_txn_t* txn) {
        fuchsia_paver_ReadResult result = {};
        vmo_.write(kFileData, 0, sizeof(kFileData));
        result.tag = fuchsia_paver_ReadResultTag_info;
        result.info.offset = 0;
        result.info.size = sizeof(kFileData);

        return fuchsia_paver_PayloadStreamReadData_reply(txn, &result);
    }

    zx_status_t ReadError(fidl_txn_t* txn) {
        fuchsia_paver_ReadResult result = {};
        result.tag = fuchsia_paver_ReadResultTag_err;
        result.err = ZX_ERR_INTERNAL;

        return fuchsia_paver_PayloadStreamReadData_reply(txn, &result);
    }

    zx_status_t ReadEof(fidl_txn_t* txn) {
        fuchsia_paver_ReadResult result = {};
        result.tag = fuchsia_paver_ReadResultTag_eof;
        result.eof = true;

        return fuchsia_paver_PayloadStreamReadData_reply(txn, &result);
    }

    zx_status_t ReadData(fidl_txn_t* txn) {
        if (!vmo_) {
            return ZX_ERR_BAD_STATE;
        }

        if (return_err_) {
            return ReadError(txn);
        } else if (return_eof_) {
            return ReadEof(txn);
        } else {
            return ReadSuccess(txn);
        }
    }

    zx_status_t RegisterVmo(zx_handle_t vmo_handle, fidl_txn_t* txn) {
        vmo_ = zx::vmo(vmo_handle);
        return fuchsia_paver_PayloadStreamRegisterVmo_reply(txn, ZX_OK);
    }

protected:
    using Binder = fidl::Binder<StreamReaderTest>;

    zx::channel client_;
    async::Loop loop_;
    static constexpr fuchsia_paver_PayloadStream_ops_t ops_ = {
        .RegisterVmo = Binder::BindMember<&StreamReaderTest::RegisterVmo>,
        .ReadData = Binder::BindMember<&StreamReaderTest::ReadData>,
    };
    zx::vmo vmo_;

    bool return_err_ = false;
    bool return_eof_ = false;
};

TEST_F(StreamReaderTest, Create) {
    std::unique_ptr<paver::StreamReader> reader;
    ASSERT_OK(paver::StreamReader::Create(std::move(client_), &reader));
}

TEST_F(StreamReaderTest, ReadError) {
    std::unique_ptr<paver::StreamReader> reader;
    ASSERT_OK(paver::StreamReader::Create(std::move(client_), &reader));

    return_err_ = true;

    char buffer[sizeof(kFileData)] = {};
    size_t actual;
    ASSERT_NE(reader->Read(buffer, sizeof(buffer), &actual), ZX_OK);
}

TEST_F(StreamReaderTest, ReadEof) {
    std::unique_ptr<paver::StreamReader> reader;
    ASSERT_OK(paver::StreamReader::Create(std::move(client_), &reader));

    return_eof_ = true;

    char buffer[sizeof(kFileData)] = {};
    size_t actual;
    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, 0);
}

TEST_F(StreamReaderTest, ReadSingle) {
    std::unique_ptr<paver::StreamReader> reader;
    ASSERT_OK(paver::StreamReader::Create(std::move(client_), &reader));

    char buffer[sizeof(kFileData)] = {};
    size_t actual;
    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, sizeof(buffer));
    ASSERT_EQ(memcmp(buffer, kFileData, sizeof(buffer)), 0);

    return_eof_ = true;

    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, 0);
}

TEST_F(StreamReaderTest, ReadMultiple) {
    std::unique_ptr<paver::StreamReader> reader;
    ASSERT_OK(paver::StreamReader::Create(std::move(client_), &reader));

    char buffer[sizeof(kFileData)] = {};
    size_t actual;
    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, sizeof(buffer));
    ASSERT_EQ(memcmp(buffer, kFileData, sizeof(buffer)), 0);

    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, sizeof(buffer));
    ASSERT_EQ(memcmp(buffer, kFileData, sizeof(buffer)), 0);

    return_eof_ = true;

    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, 0);
}

TEST_F(StreamReaderTest, ReadPartial) {
    std::unique_ptr<paver::StreamReader> reader;
    ASSERT_OK(paver::StreamReader::Create(std::move(client_), &reader));

    constexpr size_t kBufferSize = sizeof(kFileData) - 3;
    char buffer[kBufferSize] = {};
    size_t actual;
    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, sizeof(buffer));
    ASSERT_EQ(memcmp(buffer, kFileData, sizeof(buffer)), 0);

    return_eof_ = true;

    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, 3);
    ASSERT_EQ(memcmp(buffer, kFileData + kBufferSize, 3), 0);

    ASSERT_OK(reader->Read(buffer, sizeof(buffer), &actual));
    ASSERT_EQ(actual, 0);
}

} // namespace
