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
    EXPECT_EQ(sizeof(tftp_session), tftp_sizeof_session(), "");
    ASSERT_GE(sizeof(buf), tftp_sizeof_session(),
            "need to update test for larger tftp_session size");
    status = tftp_init(&session, buf, tftp_sizeof_session());
    EXPECT_EQ(status, TFTP_NO_ERROR, "tftp_init failed on correctly sized buffer");

    END_TEST;
}

static bool test_tftp_session_options(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto open_read_fn = [](const char* filename, void* cookie) -> ssize_t {
        return 0;
    };
    auto open_write_fn = [](const char* filename, size_t size, void* cookie) -> tftp_status {
        return 0;
    };
    auto read_fn = [](void* data, size_t* len, off_t offset, void* cookie) -> tftp_status {
        return 0;
    };
    auto write_fn = [](const void* data, size_t* len, off_t offset, void* cookie) -> tftp_status {
        return 0;
    };
    auto close_fn = [](void* cookie) {
        return;
    };
    tftp_file_interface ifc = { open_read_fn, open_write_fn, read_fn, write_fn, close_fn };
    auto status = tftp_session_set_file_interface(ts.session, &ifc);
    EXPECT_EQ(TFTP_NO_ERROR, status, "could not set file callbacks");
    EXPECT_EQ((tftp_file_open_read_cb)open_read_fn, ts.session->file_interface.open_read,
              "bad open (read) function pointer");
    EXPECT_EQ((tftp_file_open_write_cb)open_write_fn, ts.session->file_interface.open_write,
              "bad open (write) function pointer");
    EXPECT_EQ((tftp_file_read_cb)read_fn, ts.session->file_interface.read, "bad read function pointer");
    EXPECT_EQ((tftp_file_write_cb)write_fn, ts.session->file_interface.write, "bad write function pointer");
    EXPECT_EQ((tftp_file_close_cb)close_fn, ts.session->file_interface.close, "bad write function pointer");

    END_TEST;
}

bool verify_write_request(const test_state& ts) {
    auto msg = reinterpret_cast<tftp_msg*>(ts.out);
    EXPECT_EQ(msg->opcode, htons(OPCODE_WRQ), "opcode should be 2 (WRQ)");
    EXPECT_STR_EQ("filename", msg->data, ts.outlen, "bad filename");
    return true;
}

static bool test_tftp_generate_write_request_default(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");

    // Test TFTP state, but not internal session state
    EXPECT_EQ(DEFAULT_MODE, ts.session->options.mode, "bad session options: mode");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->options.block_size, "bad session options: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->options.timeout, "bad session options: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->options.window_size, "bad session options: window size");

    EXPECT_EQ(WRITE_REQUESTED, ts.session->state, "bad session: state");
    EXPECT_EQ(ts.msg_size, ts.session->file_size, "bad session: file size");
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

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, kBlockSize, 0, 0, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");
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

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, kTimeout, 0, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");
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

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, 0, kWindowSize, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");
    EXPECT_EQ(DEFAULT_MODE, ts.session->options.mode, "bad session options: mode");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->options.block_size, "bad session options: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->options.timeout, "bad session options: timeout");
    EXPECT_EQ(kWindowSize, ts.session->options.window_size, "bad session options: window size");

    END_TEST;
}

bool verify_response_opcode(const test_state& ts, uint16_t opcode) {
    ASSERT_GT(ts.outlen, 0, "outlen must not be zero");
    auto msg = reinterpret_cast<tftp_msg*>(ts.out);
    EXPECT_EQ(msg->opcode, htons(opcode), "bad opcode");
    return true;
}

static bool test_tftp_receive_write_request_unexpected(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
            ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "could not generate write request");
    ASSERT_TRUE(verify_write_request(ts), "bad write request");

    ASSERT_LE(ts.outlen, 1500, "outlen too large");
    uint8_t buf[1500];
    memcpy(buf, ts.out, ts.outlen);

    status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_ERR_BAD_STATE, status, "receive should fail");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ERROR), "bad error response");

    END_TEST;
}

static bool test_tftp_receive_write_request_too_large(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    uint8_t buf[1024] = { 0, 2, };
    auto status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_LT(status, 0, "receive should fail");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ERROR), "bad error response");

    END_TEST;
}

static bool test_tftp_receive_write_request_no_tsize(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    uint8_t buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
    };
    auto status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_ERR_BAD_STATE, status, "tftp session should fail");
    EXPECT_EQ(ERROR, ts.session->state, "tftp session in wrong state");
    EXPECT_EQ(0, ts.session->file_size, "tftp session bad file size");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ERROR), "bad error response");

    END_TEST;
}

static bool test_tftp_receive_write_request_send_oack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_open_write_cb open_write_cb =
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            };
    tftp_file_interface ifc = {NULL, open_write_cb, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
    };
    auto status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");
    EXPECT_EQ(1024, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    END_TEST;
}

static tftp_status dummy_open_write(const char* filename, size_t size, void* cookie) {
    return 0;
}

static bool test_tftp_receive_write_request_blocksize(void) {
    constexpr size_t kBlocksize = 1024;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {NULL, dummy_open_write, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
        'B', 'L', 'K', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '1', '0', '2', '4', 0x00,                     // BLKSIZE value
    };
    auto status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_EQ(kBlocksize, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    END_TEST;
}

static bool test_tftp_receive_write_request_timeout(void) {
    constexpr uint8_t kTimeout = 5;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {NULL, dummy_open_write, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
        'T', 'I', 'M', 'E', 'O', 'U', 'T', 0x00,      // Option
        '5', 0x00,                                    // TIMEOUT value
    };
    auto status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(kTimeout, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    END_TEST;
}

static bool test_tftp_receive_write_request_windowsize(void) {
    constexpr uint8_t kWindowsize = 32;

    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {NULL, dummy_open_write, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t buf[] = {
        0x00, 0x02,                                                  // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00,                // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                               // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                               // Option
        '1', '0', '2', '4', 0x00,                                    // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '3', '2', 0x00,                                              // WINDOWSIZE value
    };
    auto status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(kWindowsize, ts.session->window_size, "bad session: window size");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    END_TEST;
}

struct tx_test_data {
    struct {
        uint16_t block;
        off_t offset;
        size_t len;
        uint8_t data[2048];  // for reads
    } expected;

    struct {
        // block is in the outgoing message buffer
        off_t offset;
        size_t len;
        uint8_t data[2048];  // for writes
    } actual;

    tx_test_data() {
        expected.block = 1;
        expected.offset = 0;
        expected.len = DEFAULT_BLOCKSIZE;
        expected.data[0] = 'F';
        expected.data[DEFAULT_BLOCKSIZE - 1] = 'X';
        actual.offset = -1;
        actual.len = -1;
        memset(actual.data, 0, sizeof(actual.data));
    }
};

bool verify_read_data(const test_state& ts, const tx_test_data& td) {
    BEGIN_HELPER;
    EXPECT_EQ(td.expected.offset, td.actual.offset, "read offset mismatch");
    EXPECT_EQ(td.expected.len, td.actual.len, "read length mismatch");
    auto msg = static_cast<tftp_data_msg*>(ts.out);
    EXPECT_EQ(htons(OPCODE_DATA), msg->opcode, "bad opcode");
    EXPECT_EQ(td.expected.block, msg->block, "bad block number");
    EXPECT_BYTES_EQ(td.expected.data, msg->data, td.actual.len, "read data mismatch");
    END_HELPER;
}

tftp_status mock_read(void* data, size_t* len, off_t offset, void* cookie) {
    tx_test_data* td = static_cast<tx_test_data*>(cookie);
    td->actual.len = *len;
    td->actual.offset = offset;
    memcpy(data, td->expected.data, *len);
    return static_cast<tftp_status>(*len);
}

static bool test_tftp_receive_oack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    ASSERT_TRUE(verify_write_request(ts), "bad write request");

    uint8_t buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '1', '0', '2', '4', 0x00,       // TSIZE value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_FALSE(tftp_session_has_pending(ts.session), "session should not have pending data");
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(1024, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");

    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");

    END_TEST;
}

static bool test_tftp_receive_oack_blocksize(void) {
    constexpr size_t kBlockSize = 1024;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, kBlockSize, 0, 0, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t buf[] = {
        0x00, 0x06,                                   // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '2', '0', '4', '8', 0x00,                     // TSIZE value
        'B', 'L', 'K', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '1', '0', '2', '4', 0x00,                     // BLKSIZE value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    td.expected.len = kBlockSize;
    td.expected.data[kBlockSize - 1] = 'X';
    status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(2048, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(kBlockSize, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");

    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + kBlockSize, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");
    END_TEST;
}

static bool test_tftp_receive_oack_timeout(void) {
    constexpr uint8_t kTimeout = 5;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, kTimeout, 0, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t buf[] = {
        0x00, 0x06,                                   // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
        'T', 'I', 'M', 'E', 'O', 'U', 'T', 0x00,      // Option
        '5', 0x00,                                    // TIMEOUT value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(1024, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(kTimeout, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");

    EXPECT_EQ(ts.timeout, kTimeout * 1000, "timeout should be set");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");
    END_TEST;
}

static bool test_tftp_receive_oack_windowsize(void) {
    constexpr uint8_t kWindowSize = 2;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 4096, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, 0, kWindowSize, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t buf[] = {
        0x00, 0x06,                                   // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '4', '0', '9', '6', 0x00,                     // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '2', 0x00,                                              // WINDOWSIZE value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(4096, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(kWindowSize, ts.session->window_size, "bad session: window size");

    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");
    ASSERT_TRUE(tftp_session_has_pending(ts.session), "session should have pending");

    // Since pending is true, call for a second data packet to transmit
    // Updated the read offset and change a few bytes
    td.expected.block = 2;
    td.expected.offset = DEFAULT_BLOCKSIZE;
    td.expected.data[1] = 'X';
    td.expected.data[DEFAULT_BLOCKSIZE - 2] = 'F';

    status = tftp_prepare_data(ts.session, ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");
    EXPECT_FALSE(tftp_session_has_pending(ts.session), "session should not have pending");

    END_TEST;
}

tftp_status mock_write(const void* data, size_t* len, off_t offset, void* cookie) {
    tx_test_data* td = static_cast<tx_test_data*>(cookie);
    td->actual.len = *len;
    td->actual.offset = offset;
    memcpy(td->actual.data + offset, data, *len);
    return static_cast<tftp_status>(*len);
}

bool verify_write_data(const uint8_t* expected, const tx_test_data& td) {
    BEGIN_HELPER;
    EXPECT_EQ(td.expected.offset, td.actual.offset, "write offset mismatch");
    EXPECT_EQ(td.expected.len, td.actual.len, "write length mismatch");
    EXPECT_BYTES_EQ(expected, td.actual.data + td.actual.offset, td.actual.len, "write data mismatch");
    END_HELPER;
}

static bool test_tftp_receive_data(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
    };
    auto status = tftp_process_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied

    ifc.write = mock_write;
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ACK), "bad response");
    EXPECT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_final_block(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
    };
    auto status = tftp_process_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied

    ifc.write = mock_write;
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_ACK), "bad response");
    ASSERT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");

    // Update block number and first/last bytes of the data packet
    data_buf[2]++;
    data_buf[4]++;
    data_buf[515]++;
    td.expected.block++;
    td.expected.offset = DEFAULT_BLOCKSIZE;

    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_TRANSFER_COMPLETED, status, "receive data failed");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_ACK), "bad response");
    EXPECT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");

    END_TEST;
}

static bool test_tftp_receive_data_blocksize(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 2048, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '2', '0', '4', '8', 0x00,                     // TSIZE value
        'B', 'L', 'K', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '1', '0', '2', '4', 0x00,                     // BLKSIZE value
    };
    auto status = tftp_process_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[1028] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[1027] = 0x79;  // set the last byte to make sure it all gets copied

    ifc.write = mock_write;
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    td.expected.len = 1024;
    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ACK), "bad response");
    EXPECT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_windowsize(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1025, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1025, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '5', 0x00,                     // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '2', 0x00,                                              // WINDOWSIZE value
    };
    auto status = tftp_process_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied

    ifc.write = mock_write;
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_EQ(0, ts.outlen, "no response expected");
    EXPECT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(1, ts.session->window_index, "tftp session window index mismatch");

    // Update block number and first/last bytes of the data packet
    data_buf[2]++;
    data_buf[4]++;
    data_buf[515]++;
    td.expected.block++;
    td.expected.offset += DEFAULT_BLOCKSIZE;

    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ACK), "bad response");
    EXPECT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_skipped_block(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '1', '0', '2', '4', 0x00,                     // TSIZE value
    };
    auto status = tftp_process_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    // This is block 2, meaning we missed block 1 somehow.
    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x02, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied

    ifc.write = mock_write;
    tftp_session_set_file_interface(ts.session, &ifc);

    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    ASSERT_GT(ts.outlen, 0, "outlen must not be zero");
    auto msg = reinterpret_cast<tftp_data_msg*>(ts.out);
    EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "bad opcode");
    EXPECT_EQ(msg->block, 0, "bad block number");
    EXPECT_EQ(0, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_windowsize_skipped_block(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kFilename, strlen(kFilename), "bad filename");
                EXPECT_EQ(size, 2048, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    uint8_t req_buf[] = {
        0x00, 0x02,                                   // Opcode (WRQ)
        'f', 'i', 'l', 'e', 'n', 'a', 'm', 'e', 0x00, // Filename
        'O', 'C', 'T', 'E', 'T', 0x00,                // Mode
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '2', '0', '4', '8', 0x00,                     // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '3', 0x00,                                              // WINDOWSIZE value
    };
    auto status = tftp_process_msg(ts.session, req_buf, sizeof(req_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(WRITE_REQUESTED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x01, 0x00,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied

    ifc.write = mock_write;
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(1, ts.session->window_index, "tftp session window index mismatch");

    // Update block number and first/last bytes of the data packet
    data_buf[2]++;
    data_buf[4]++;
    data_buf[515]++;
    td.expected.block++;
    td.expected.offset += DEFAULT_BLOCKSIZE;

    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    EXPECT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(2, ts.session->window_index, "tftp session window index mismatch");


    // Update block number and first/last bytes of the data packet. Block number
    // goes up by 2 to indicate a skipped block.
    data_buf[2] = 4u;
    data_buf[4]++;
    data_buf[515]++;
    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    ASSERT_GT(ts.outlen, 0, "outlen must not be zero");
    auto msg = reinterpret_cast<tftp_data_msg*>(ts.out);
    EXPECT_EQ(msg->opcode, htons(OPCODE_ACK), "bad opcode");
    EXPECT_EQ(msg->block, 2, "bad block number");
    EXPECT_EQ(0, td.actual.data[1024], "block 3 should be empty");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    // Reset the window index after sending the ack with the last known block
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");

    uint8_t oack_buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '1', '0', '2', '4', 0x00,       // TSIZE value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, oack_buf, sizeof(oack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x01, 0x00,  // Block
    };

    td.expected.block = 2;
    td.expected.offset += DEFAULT_BLOCKSIZE;
    td.expected.data[1] = 'f';
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");

    END_TEST;
}

static bool test_tftp_send_data_receive_final_ack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");

    uint8_t oack_buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '1', '0', '2', '4', 0x00,       // TSIZE value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, oack_buf, sizeof(oack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");
    ASSERT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    ASSERT_TRUE(verify_read_data(ts, td), "bad test data");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x01, 0x00,  // Block
    };

    td.expected.block = 2;
    td.expected.offset += DEFAULT_BLOCKSIZE;
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");

    ack_buf[2]++;
    // Do not expect any more sends.
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_TRANSFER_COMPLETED, status, "tftp transfer should be complete");
    EXPECT_EQ(ts.outlen, 0, "no outgoing message expected");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack_skipped_block(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, 0, 0, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");

    uint8_t oack_buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '1', '0', '2', '4', 0x00,       // TSIZE value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, oack_buf, sizeof(oack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");
    ASSERT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    ASSERT_TRUE(verify_read_data(ts, td), "bad test data");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x00, 0x00,  // Block
    };

    // next data packet should have same offset since it is a resend
    tx_test_data td2;
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td2);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td2), "bad test data");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack_window_size(void) {
    constexpr uint8_t kWindowSize = 2;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);

    auto status = tftp_generate_write_request(ts.session, kFilename, MODE_OCTET,
        ts.msg_size, 0, 0, kWindowSize, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");

    uint8_t oack_buf[] = {
        0x00, 0x06,                     // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,  // Option
        '2', '0', '4', '8', 0x00,       // TSIZE value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '2', 0x00,                                              // WINDOWSIZE value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, oack_buf, sizeof(oack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");
    ASSERT_EQ(0, ts.session->block_number, "tftp session block number mismatch");
    ASSERT_EQ(1, ts.session->window_index, "tftp session window index mismatch");
    ASSERT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    ASSERT_TRUE(verify_read_data(ts, td), "bad test data");
    ASSERT_TRUE(tftp_session_has_pending(ts.session), "expected pending data to transmit");

    td.expected.block++;
    td.expected.offset += DEFAULT_BLOCKSIZE;
    td.expected.data[0]++;
    status = tftp_prepare_data(ts.session, ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");
    ASSERT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    ASSERT_EQ(0, ts.session->window_index, "tftp session window index mismatch");
    ASSERT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    ASSERT_TRUE(verify_read_data(ts, td), "bad test data");
    ASSERT_FALSE(tftp_session_has_pending(ts.session), "expected to wait for ack");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x02, 0x00,  // Block
    };

    td.expected.block++;
    td.expected.offset += DEFAULT_BLOCKSIZE;
    td.expected.data[1]++;
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(TRANSMITTING, ts.session->state, "session should be TRANSMITTING");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(1, ts.session->window_index, "tftp session window index mismatch");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");
    EXPECT_TRUE(tftp_session_has_pending(ts.session), "expected pending data to transmit");

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
RUN_TEST(test_tftp_receive_data_final_block)
RUN_TEST(test_tftp_receive_data_blocksize)
RUN_TEST(test_tftp_receive_data_windowsize)
RUN_TEST(test_tftp_receive_data_skipped_block)
RUN_TEST(test_tftp_receive_data_windowsize_skipped_block)
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
