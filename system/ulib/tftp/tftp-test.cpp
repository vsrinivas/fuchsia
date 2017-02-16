// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tftp/tftp.h>

#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mxtl/unique_ptr.h>
#include <unittest/unittest.h>

// For inspecting session state
#include "internal.h"

constexpr char kFilename[] = "filename";

struct test_state {
    // Return value used by ulib/unittest
    bool reset(size_t ssize, size_t msize, size_t osize) {
        sess_size = ssize;
        msg_size = msize;
        out_size = osize;
        sess_buf.reset(new uint8_t[sess_size]);
        msg_data.reset(new uint8_t[msg_size]);
        out_scratch.reset(new uint8_t[out_size]);
        auto init_status = tftp_init(&session, sess_buf.get(), sess_size);
        ASSERT_EQ(TFTP_NO_ERROR, init_status, "could not initialize tftp_session");
        data = reinterpret_cast<void*>(msg_data.get());
        out = reinterpret_cast<void*>(out_scratch.get());
        outlen = out_size;
        return true;
    }

    tftp_session* session = nullptr;
    size_t sess_size = 0;
    size_t msg_size = 0;
    size_t out_size = 0;
    mxtl::unique_ptr<uint8_t[]> sess_buf;
    mxtl::unique_ptr<uint8_t[]> msg_data;
    mxtl::unique_ptr<uint8_t[]> out_scratch;
    void* data = nullptr;
    void* out = nullptr;
    size_t outlen = 0;
    uint32_t timeout = 0;
};

static bool test_tftp_init(void) {
    BEGIN_TEST;

    uint8_t buf[1024];
    tftp_session* session;
    int status = tftp_init(&session, nullptr, 4096);
    EXPECT_LT(status, 0, "tftp_init should fail for NULL buffer");
    status = tftp_init(&session, buf, 4);
    EXPECT_LT(status, 0, "tftp_init should fail for too small buffer");
    status = tftp_init(&session, buf, sizeof(buf));
    EXPECT_EQ(status, TFTP_NO_ERROR, "error creating tftp session");

    END_TEST;
}

static bool test_tftp_session_options(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto open_fn = [](const char* filename, size_t size, void** data, void* cookie) -> tftp_status {
        return 0;
    };
    auto status = tftp_session_set_open_cb(ts.session, open_fn);
    EXPECT_EQ(TFTP_NO_ERROR, status, "could not set open callback");
    EXPECT_EQ((tftp_open_file)open_fn, ts.session->open_fn, "bad open function pointer");

    ts.reset(1024, 1024, 1500);

    auto send_fn = [](void* data, size_t len, void* cookie) -> tftp_status {
        return 0;
    };
    status = tftp_session_set_send_cb(ts.session, send_fn);
    EXPECT_EQ(TFTP_NO_ERROR, status, "could not set send callback");
    EXPECT_EQ((tftp_send_message)send_fn, ts.session->send_fn, "bad send function pointer");

    END_TEST;
}

tftp_status verify_write_request(void* data, size_t length, void* cookie) {
    auto msg = reinterpret_cast<tftp_msg*>(data);
    EXPECT_EQ(msg->opcode, htons(OPCODE_WRQ), "opcode should be 2 (WRQ)");
    EXPECT_STR_EQ("filename", msg->data, length, "bad filename");
    return static_cast<tftp_status>(length);
}

static bool test_tftp_generate_write_request_default(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    // Test TFTP state, but not internal session state
    EXPECT_EQ(DEFAULT_MODE, ts.session->options.mode, "bad session options: mode");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->options.block_size, "bad session options: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->options.timeout, "bad session options: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->options.window_size, "bad session options: window size");

    EXPECT_EQ(WRITE_REQUESTED, ts.session->state, "bad session: state");
    EXPECT_EQ(ts.msg_size, ts.session->file_size, "bad session: file size");
    EXPECT_EQ(ts.data, reinterpret_cast<void*>(ts.session->data), "bad session: data pointer");
    EXPECT_EQ(0, ts.session->offset, "bad session: offset");
    EXPECT_EQ(0, ts.session->block_number, "bad session: block number");
    EXPECT_EQ(DEFAULT_TIMEOUT * 1000, ts.timeout, "timeout not set correctly");

    END_TEST;
}

static bool test_tftp_generate_write_request_blocksize(void) {
    constexpr size_t kBlockSize = 1000;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, kBlockSize, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_EQ(DEFAULT_MODE, ts.session->options.mode, "bad session options: mode");
    EXPECT_EQ(kBlockSize, ts.session->options.block_size, "bad session options: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->options.timeout, "bad session options: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->options.window_size, "bad session options: window size");

    END_TEST;
}

static bool test_tftp_generate_write_request_timeout(void) {
    constexpr uint8_t kTimeout = 60;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, kTimeout, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_EQ(DEFAULT_MODE, ts.session->options.mode, "bad session options: mode");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->options.block_size, "bad session options: block size");
    EXPECT_EQ(kTimeout, ts.session->options.timeout, "bad session options: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->options.window_size, "bad session options: window size");
    // We still have to negotiate the timeout, so we use the default here.
    EXPECT_EQ(DEFAULT_TIMEOUT * 1000, ts.timeout, "timeout not set correctly");

    END_TEST;
}

static bool test_tftp_generate_write_request_windowsize(void) {
    constexpr uint8_t kWindowSize = 32;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, 0, kWindowSize, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_EQ(DEFAULT_MODE, ts.session->options.mode, "bad session options: mode");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->options.block_size, "bad session options: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->options.timeout, "bad session options: timeout");
    EXPECT_EQ(kWindowSize, ts.session->options.window_size, "bad session options: window size");

    END_TEST;
}

static bool test_tftp_generate_write_request_send_err(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                return 0;
            });

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
            ts.data, ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_ERR_IO, status, "error in send fn should be an error in tftp");
    EXPECT_EQ(ERROR, ts.session->state, "state should be ERROR");

    END_TEST;
}

tftp_status verify_error_response(void* data, size_t length, void* cookie) {
    auto msg = reinterpret_cast<tftp_msg*>(data);
    EXPECT_EQ(msg->opcode, htons(OPCODE_ERROR), "opcode should be 5 (ERROR)");
    return 0;
}

static bool test_tftp_receive_write_request_unexpected(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);
    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
            ts.data, ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "could not generate write request");

    ASSERT_LE(ts.outlen, 1500, "outlen too large");
    uint8_t buf[1500];
    memcpy(buf, ts.out, ts.outlen);
    tftp_session_set_send_cb(ts.session, verify_error_response);

    status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_ERR_BAD_STATE, status, "receive should fail");

    END_TEST;
}

static bool test_tftp_receive_write_request_too_large(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_error_response);

    uint8_t buf[1024] = { 0, 2, };
    auto status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_LT(status, 0, "receive should fail");

    END_TEST;
}

static bool test_tftp_receive_write_request_no_tsize(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "msg should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_ERROR), "opcode should be 8 (OERROR)");
                return static_cast<tftp_status>(len);
            });

    uint8_t buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
    };
    auto status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_ERR_BAD_STATE, status, "tftp session should fail");
    EXPECT_EQ(ERROR, ts.session->state, "tftp session in wrong state");
    EXPECT_EQ(0, ts.session->file_size, "tftp session bad file size");

    END_TEST;
}

static bool test_tftp_receive_write_request_send_oack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_open_cb(ts.session,
            [](const char* filename, size_t size, void** data, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            });
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
    };
    auto status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");
    EXPECT_EQ(1024, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");

    END_TEST;
}

static tftp_status dummy_open(const char* filename, size_t size, void** data, void* cookie) {
    return 0;
}

static bool test_tftp_receive_write_request_blocksize(void) {
    constexpr size_t kBlocksize = 1024;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_open_cb(ts.session, dummy_open);
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
        'B', 'L', 'K', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '1', '0', '2', '4', 0x00,                     // BLKSIZE value
    };
    auto status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_EQ(kBlocksize, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");

    END_TEST;
}

static bool test_tftp_receive_write_request_timeout(void) {
    constexpr uint8_t kTimeout = 5;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_open_cb(ts.session, dummy_open);
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
        'T', 'I', 'M', 'E', 'O', 'U', 'T', 0x00,      // Option
        '5', 0x00,                                    // TIMEOUT value
    };
    auto status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(kTimeout, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");

    END_TEST;
}

static bool test_tftp_receive_write_request_windowsize(void) {
    constexpr uint8_t kWindowsize = 32;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_open_cb(ts.session, dummy_open);
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t buf[] = {
        0x00, 0x02,                                                  // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00,                // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                               // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                               // Option
        '1', '0', '2', '4', 0x00,                                    // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '3', '2', 0x00,                                              // WINDOWSIZE value
    };
    auto status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(kWindowsize, ts.session->window_size, "bad session: window size");

    END_TEST;
}

struct tx_expect {
    size_t len;
    uint16_t block;
};

tftp_status verify_transmit(void* data, size_t len, void* cookie) {
    auto msg = reinterpret_cast<tftp_data_msg*>(data);
    auto exp = reinterpret_cast<tx_expect*>(cookie);
    EXPECT_EQ(len, exp->len, "transmit length incorrect");
    EXPECT_EQ(msg->block, exp->block, "transmit block incorrect");
    exp->block++;  // increment for successive verifications
    return static_cast<tftp_status>(len);
}

static bool test_tftp_receive_oack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '1', '0', '2', '4', 0x00,       // TSIZE value
    };
    tx_expect exp = { .len = 516, .block = 1 };
    tftp_session_set_send_cb(ts.session, verify_transmit);
    status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(1024, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");
    END_TEST;
}

static bool test_tftp_receive_oack_blocksize(void) {
    constexpr size_t kBlockSize = 1024;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, kBlockSize, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t buf[] = {
        0x00, 0x06,                                   // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '2', '0', '4', '8', 0x00,                     // TSIZE value
        'B', 'L', 'K', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '1', '0', '2', '4', 0x00,                     // BLKSIZE value
    };
    tx_expect exp = { .len = 1028, .block = 1 };
    tftp_session_set_send_cb(ts.session, verify_transmit);
    status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(2048, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(kBlockSize, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");
    END_TEST;
}

static bool test_tftp_receive_oack_timeout(void) {
    constexpr uint8_t kTimeout = 5;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, kTimeout, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t buf[] = {
        0x00, 0x06,                                   // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
        'T', 'I', 'M', 'E', 'O', 'U', 'T', 0x00,      // Option
        '5', 0x00,                                    // TIMEOUT value
    };
    tx_expect exp = { .len = 516, .block = 1 };
    tftp_session_set_send_cb(ts.session, verify_transmit);
    status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(1024, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(kTimeout, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");
    END_TEST;
}

static bool test_tftp_receive_oack_windowsize(void) {
    constexpr uint8_t kWindowSize = 2;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 4096, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, 0, kWindowSize, ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t buf[] = {
        0x00, 0x06,                                   // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '4', '0', '9', '6', 0x00,                     // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '2', 0x00,                                              // WINDOWSIZE value
    };
    tx_expect exp = { .len = 516, .block = 1 };
    tftp_session_set_send_cb(ts.session, verify_transmit);
    status = tftp_handle_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(4096, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(kWindowSize, ts.session->window_size, "bad session: window size");
    EXPECT_EQ(3, exp.block, "transmit should be called twice before waiting for ack");
    END_TEST;
}

static bool test_tftp_receive_data(void) {
    BEGIN_TEST;

    uint8_t file[1024] = {0};

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_open_cb(ts.session,
            [](const char* filename, size_t size, void** data, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                *data = cookie;
                return 0;
            });
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
    };
    auto status = tftp_handle_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_EQ(len, 4, "ack should be 4 bytes");
                auto msg = reinterpret_cast<tftp_data_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "opcode should be 4 (ACK)");
                EXPECT_EQ(msg->block, 1, "block should be 1");
                return static_cast<tftp_status>(len);
            });

    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_BYTES_EQ(data_buf + 4, file, sizeof(data_buf) - 4, "receive data does not match");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_final_block(void) {
    BEGIN_TEST;

    uint8_t file[1024] = {0};

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_open_cb(ts.session,
            [](const char* filename, size_t size, void** data, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                *data = cookie;
                return 0;
            });
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
    };
    auto status = tftp_handle_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_EQ(len, 4, "ack should be 4 bytes");
                auto msg = reinterpret_cast<tftp_data_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "opcode should be 4 (ACK)");
                EXPECT_EQ(msg->block, 1, "block should be 1");
                return static_cast<tftp_status>(len);
            });

    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive data failed");

    // Update block number and first/last bytes of the data packet
    data_buf[2]++;
    data_buf[4]++;
    data_buf[515]++;
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_EQ(len, 4, "ack should be 4 bytes");
                auto msg = reinterpret_cast<tftp_data_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "opcode should be 4 (ACK)");
                EXPECT_EQ(msg->block, 2, "block should be 1");
                return static_cast<tftp_status>(len);
            });

    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_TRANSFER_COMPLETED, status, "receive data failed");
    EXPECT_BYTES_EQ(data_buf + 4, file + 512, sizeof(data_buf) - 4, "receive data does not match");

    END_TEST;
}


static bool test_tftp_receive_data_blocksize(void) {
    BEGIN_TEST;

    uint8_t file[2048] = {0};

    test_state ts;
    ts.reset(1024, 2048, 1500);
    tftp_session_set_open_cb(ts.session,
            [](const char* filename, size_t size, void** data, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 2048, "bad file size");
                *data = cookie;
                return 0;
            });
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '2', '0', '4', '8', 0x00,                     // TSIZE value
        'B', 'L', 'K', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '1', '0', '2', '4', 0x00,                     // BLKSIZE value
    };
    auto status = tftp_handle_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");

    uint8_t data_buf[1028] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[1027] = 0x79;  // set the last byte to make sure it all gets copied
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_EQ(len, 4, "ack should be 4 bytes");
                auto msg = reinterpret_cast<tftp_data_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "opcode should be 4 (ACK)");
                EXPECT_EQ(msg->block, 1, "block should be 1");
                return static_cast<tftp_status>(len);
            });

    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_BYTES_EQ(data_buf + 4, file, sizeof(data_buf) - 4, "receive data does not match");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_windowsize(void) {
    BEGIN_TEST;

    uint8_t file[1025] = {0};

    test_state ts;
    ts.reset(1024, 1025, 1500);
    tftp_session_set_open_cb(ts.session,
            [](const char* filename, size_t size, void** data, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1025, "bad file size");
                *data = cookie;
                return 0;
            });
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '5', 0x00,                     // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '2', 0x00,                                              // WINDOWSIZE value
    };
    auto status = tftp_handle_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_EQ(len, 4, "ack should be 4 bytes");
                auto msg = reinterpret_cast<tftp_data_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "opcode should be 4 (ACK)");
                EXPECT_EQ(msg->block, 2, "block should be 2");
                return static_cast<tftp_status>(len);
            });

    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_BYTES_EQ(data_buf + 4, file, sizeof(data_buf) - 4, "receive data does not match");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(1, ts.session->window_index, "tftp session window index mismatch");

    // Update block number and first/last bytes of the data packet
    data_buf[2]++;
    data_buf[4]++;
    data_buf[515]++;
    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_BYTES_EQ(data_buf + 4, file + 512, sizeof(data_buf) - 4, "receive data does not match");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_skipped_block(void) {
    BEGIN_TEST;

    uint8_t file[1024] = {0};

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_open_cb(ts.session,
            [](const char* filename, size_t size, void** data, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                *data = cookie;
                return 0;
            });
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
    };
    auto status = tftp_handle_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");

    // This is block 2, meaning we missed block 1 somehow.
    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x02, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_EQ(len, 4, "ack should be 4 bytes");
                auto msg = reinterpret_cast<tftp_data_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "opcode should be 4 (ACK)");
                EXPECT_EQ(msg->block, 0, "block should be 0");
                return static_cast<tftp_status>(len);
            });

    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_EQ(0, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_windowsize_skipped_block(void) {
    BEGIN_TEST;

    uint8_t file[2048] = {0};

    test_state ts;
    ts.reset(1024, 2048, 1500);
    tftp_session_set_open_cb(ts.session,
            [](const char* filename, size_t size, void** data, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 2048, "bad file size");
                *data = cookie;
                return 0;
            });
    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_GE(len, 2, "oack should be at least 2 bytes");
                auto msg = reinterpret_cast<tftp_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_OACK), "opcode should be 6 (OACK)");
                return static_cast<tftp_status>(len);
            });

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '2', '0', '4', '8', 0x00,                     // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '3', 0x00,                                              // WINDOWSIZE value
    };
    auto status = tftp_handle_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied

    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_BYTES_EQ(data_buf + 4, file, sizeof(data_buf) - 4, "receive data does not match");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(1, ts.session->window_index, "tftp session window index mismatch");

    // Update block number and first/last bytes of the data packet
    data_buf[2]++;
    data_buf[4]++;
    data_buf[515]++;
    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_BYTES_EQ(data_buf + 4, file + 512, sizeof(data_buf) - 4, "receive data does not match");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(2, ts.session->window_index, "tftp session window index mismatch");

    tftp_session_set_send_cb(ts.session,
            [](void* data, size_t len, void* cookie) -> tftp_status {
                ASSERT_EQ(len, 4, "ack should be 4 bytes");
                auto msg = reinterpret_cast<tftp_data_msg*>(data);
                EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "opcode should be 4 (ACK)");
                EXPECT_EQ(msg->block, 2, "block should be 2");
                return static_cast<tftp_status>(len);
            });

    // Update block number and first/last bytes of the data packet. Block number
    // goes up by 2 to indicate a skipped block.
    data_buf[2] = 4u;
    data_buf[4]++;
    data_buf[515]++;
    status = tftp_handle_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, (void*)file);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_EQ(0, file[1024], "block 3 should be empty");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    // Reset the window index after sending the ack with the last known block
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t oack_buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '1', '0', '2', '4', 0x00,       // TSIZE value
    };
    tx_expect exp = { .len = 516, .block = 1 };
    tftp_session_set_send_cb(ts.session, verify_transmit);
    status = tftp_handle_msg(ts.session, oack_buf, sizeof(oack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x01, 0x00,  // Block
    };
    exp = { .len = 516, .block = 2 };
    status = tftp_handle_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");

    END_TEST;
}

static bool test_tftp_send_data_receive_final_ack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t oack_buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '1', '0', '2', '4', 0x00,       // TSIZE value
    };
    tx_expect exp = { .len = 516, .block = 1 };
    tftp_session_set_send_cb(ts.session, verify_transmit);
    status = tftp_handle_msg(ts.session, oack_buf, sizeof(oack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x01, 0x00,  // Block
    };
    exp = { .len = 516, .block = 2 };
    status = tftp_handle_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");

    ack_buf[2]++;
    // Do not expect any more sends.
    exp = { .len = 0, .block = 0};
    status = tftp_handle_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    EXPECT_EQ(TFTP_TRANSFER_COMPLETED, status, "tftp transfer should be complete");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack_skipped_block(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t oack_buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '1', '0', '2', '4', 0x00,       // TSIZE value
    };
    tx_expect exp = { .len = 516, .block = 1 };
    tftp_session_set_send_cb(ts.session, verify_transmit);
    status = tftp_handle_msg(ts.session, oack_buf, sizeof(oack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x00, 0x00,  // Block
    };
    exp = { .len = 516, .block = 1 };
    status = tftp_handle_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack_window_size(void) {
    constexpr uint8_t kWindowSize = 2;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);
    tftp_session_set_send_cb(ts.session, verify_write_request);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.data, ts.msg_size, 0, 0, kWindowSize, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t oack_buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '2', '0', '4', '8', 0x00,       // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '2', 0x00,                                              // WINDOWSIZE value
    };
    tx_expect exp = { .len = 516, .block = 1 };
    tftp_session_set_send_cb(ts.session, verify_transmit);
    status = tftp_handle_msg(ts.session, oack_buf, sizeof(oack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x02, 0x00,  // Block
    };
    exp = { .len = 516, .block = 3 };
    status = tftp_handle_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &exp);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(4, ts.session->block_number, "tftp session block number mismatch");

    END_TEST;
}

BEGIN_TEST_CASE(tftp_setup)
RUN_TEST(test_tftp_init)
RUN_TEST(test_tftp_session_options)
END_TEST_CASE(tftp_setup)

BEGIN_TEST_CASE(tftp_generate_write_request)
RUN_TEST(test_tftp_generate_write_request_default)
RUN_TEST(test_tftp_generate_write_request_blocksize)
RUN_TEST(test_tftp_generate_write_request_timeout)
RUN_TEST(test_tftp_generate_write_request_windowsize)
RUN_TEST(test_tftp_generate_write_request_send_err)
END_TEST_CASE(tftp_generate_write_request)

BEGIN_TEST_CASE(tftp_receive_write_request)
RUN_TEST(test_tftp_receive_write_request_unexpected)
RUN_TEST(test_tftp_receive_write_request_too_large)
RUN_TEST(test_tftp_receive_write_request_no_tsize)
RUN_TEST(test_tftp_receive_write_request_send_oack)
RUN_TEST(test_tftp_receive_write_request_blocksize)
RUN_TEST(test_tftp_receive_write_request_timeout)
RUN_TEST(test_tftp_receive_write_request_windowsize)
END_TEST_CASE(tftp_receive_write_request)

BEGIN_TEST_CASE(tftp_receive_oack)
RUN_TEST(test_tftp_receive_oack)
RUN_TEST(test_tftp_receive_oack_blocksize)
RUN_TEST(test_tftp_receive_oack_timeout)
RUN_TEST(test_tftp_receive_oack_windowsize)
END_TEST_CASE(tftp_receive_oack)

BEGIN_TEST_CASE(tftp_receive_data)
RUN_TEST(test_tftp_receive_data)
RUN_TEST(test_tftp_receive_data_blocksize)
RUN_TEST(test_tftp_receive_data_windowsize)
RUN_TEST(test_tftp_receive_data_skipped_block)
RUN_TEST(test_tftp_receive_data_windowsize_skipped_block)
RUN_TEST(test_tftp_receive_data_final_block)
END_TEST_CASE(tftp_receive_data)

BEGIN_TEST_CASE(tftp_send_data)
RUN_TEST(test_tftp_send_data_receive_ack)
RUN_TEST(test_tftp_send_data_receive_final_ack)
RUN_TEST(test_tftp_send_data_receive_ack_skipped_block)
RUN_TEST(test_tftp_send_data_receive_ack_window_size)
END_TEST_CASE(tftp_send_data)

int main(int argc, char* argv[]) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
