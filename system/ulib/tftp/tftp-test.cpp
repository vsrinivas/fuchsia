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

#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

// For inspecting session state
#include "internal.h"

constexpr char kLocalFilename[] = "local-filename";
constexpr char kRemoteFilename[] = "remote-filename";

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
    fbl::unique_ptr<uint8_t[]> sess_buf;
    fbl::unique_ptr<uint8_t[]> msg_data;
    fbl::unique_ptr<uint8_t[]> out_scratch;
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
    EXPECT_STR_EQ(kRemoteFilename, msg->data, "bad filename");
    return true;
}

bool verify_read_request(const test_state& ts) {
    auto msg = reinterpret_cast<tftp_msg*>(ts.out);
    EXPECT_EQ(msg->opcode, htons(OPCODE_RRQ), "opcode should be 1 (RRQ)");
    EXPECT_STR_EQ(kRemoteFilename, msg->data, "bad filename");
    return true;
}

// Find a string (which may include NULL characters) inside a memory region.
static bool find_str_in_mem(const char* str, size_t str_len, const char* mem, size_t mem_len) {
    while (str_len <= mem_len) {
        if (!memcmp(str, mem, str_len)) {
            return true;
        }
        mem_len--;
        mem++;
    }
    return false;
}

static bool test_tftp_generate_request(tftp_file_direction dir,
                                       size_t file_size,
                                       const uint16_t* block_size,
                                       const uint8_t* timeout,
                                       const uint16_t* window_size,
                                       const uint16_t* block_size_override,
                                       const uint8_t* timeout_override,
                                       const uint16_t* window_size_override) {
    BEGIN_TEST;

    tftp_status status;
    test_state ts;
    ts.reset(1024, file_size, 1500);

    // Test TFTP state, but not internal session state
    status = tftp_set_options(ts.session, block_size, timeout, window_size);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error setting session options");
    if (block_size) {
        EXPECT_EQ(*block_size, ts.session->options.block_size, "bad session options: block size");
    }
    if (timeout) {
        EXPECT_EQ(*timeout, ts.session->options.timeout, "bad session options: timeout");
    }
    if (window_size) {
        EXPECT_EQ(*window_size, ts.session->options.window_size,
                  "bad session options: window size");
    }

    size_t request_file_size = (dir == SEND_FILE) ? file_size : 0;
    status = tftp_generate_request(ts.session, dir, kLocalFilename, kRemoteFilename,
                                   MODE_OCTET, request_file_size, block_size_override,
                                   timeout_override, window_size_override, ts.out, &ts.outlen,
                                   &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating request");
    if (dir == SEND_FILE) {
        EXPECT_TRUE(verify_write_request(ts), "bad write request");
    } else {
        EXPECT_TRUE(verify_read_request(ts), "bad read request");
    }

    EXPECT_EQ(REQ_SENT, ts.session->state, "bad session: state");
    if (dir == SEND_FILE) {
        EXPECT_EQ(file_size, ts.session->file_size, "bad session: file size");
    }
    EXPECT_EQ(DEFAULT_MODE, ts.session->mode, "bad session: mode");
    EXPECT_EQ(0, ts.session->offset, "bad session: offset");
    EXPECT_EQ(0, ts.session->block_number, "bad session: block number");

    // Verify the options that were set in the request
    const char* msg = static_cast<const char*>(ts.out);
    char str[256];
    size_t str_sz;

    if (block_size_override) {
        str_sz = snprintf(str, sizeof(str), "BLKSIZE!%c%d", '\0', *block_size_override) + 1;
        EXPECT_TRUE(find_str_in_mem(str, str_sz, msg, ts.outlen), "block size not overridden");
    } else if (block_size) {
        str_sz = snprintf(str, sizeof(str), "BLKSIZE%c%d", '\0', *block_size) + 1;
        EXPECT_TRUE(find_str_in_mem(str, str_sz, msg, ts.outlen),
                    "block size not properly requested");
    } else {
        str_sz = snprintf(str, sizeof(str), "BLKSIZE") + 1;
        EXPECT_FALSE(find_str_in_mem(str, str_sz, msg, ts.outlen),
                     "block size shouldn't appear in request");
    }

    if (timeout_override) {
        str_sz = snprintf(str, sizeof(str), "TIMEOUT!%c%d", '\0', *timeout_override) + 1;
        EXPECT_TRUE(find_str_in_mem(str, str_sz, msg, ts.outlen), "timeout not overridden");
    } else if (timeout) {
        str_sz = snprintf(str, sizeof(str), "TIMEOUT%c%d", '\0', *timeout) + 1;
        EXPECT_TRUE(find_str_in_mem(str, str_sz, msg, ts.outlen),
                    "timeout not properly requested");
    } else {
        str_sz = snprintf(str, sizeof(str), "TIMEOUT") + 1;
        EXPECT_FALSE(find_str_in_mem(str, str_sz, msg, ts.outlen),
                     "timeout shouldn't appear in request");
    }

    if (window_size_override) {
        str_sz = snprintf(str, sizeof(str), "WINDOWSIZE!%c%d", '\0', *window_size_override) + 1;
        EXPECT_TRUE(find_str_in_mem(str, str_sz, msg, ts.outlen), "window size not overridden");
    } else if (window_size) {
        str_sz = snprintf(str, sizeof(str), "WINDOWSIZE%c%d", '\0', *window_size) + 1;
        EXPECT_TRUE(find_str_in_mem(str, str_sz, msg, ts.outlen),
                    "window size not properly requested");
    } else {
        str_sz = snprintf(str, sizeof(str), "WINDOWSIZE") + 1;
        EXPECT_FALSE(find_str_in_mem(str, str_sz, msg, ts.outlen),
                     "window size shouldn't appear in request");
    }

    END_TEST;
}

static bool test_tftp_generate_wrq_default(void) {
    return test_tftp_generate_request(SEND_FILE, 1024, nullptr, nullptr, nullptr,
                                                       nullptr, nullptr, nullptr);
}

static bool test_tftp_generate_wrq_options(void) {
    constexpr uint16_t kBlockSize = 555;
    constexpr uint8_t kTimeout = 3;
    constexpr uint16_t kWindowSize = 44;
    return test_tftp_generate_request(SEND_FILE, 1024, &kBlockSize, &kTimeout, &kWindowSize,
                                                       nullptr, nullptr, nullptr);
}

static bool test_tftp_generate_wrq_override_blocksize(void) {
    constexpr uint16_t kBlockSize = 1000;
    return test_tftp_generate_request(SEND_FILE, 1024, nullptr, nullptr, nullptr,
                                                       &kBlockSize, nullptr, nullptr);
}

static bool test_tftp_generate_wrq_override_timeout(void) {
    uint8_t kTimeout = 60;
    return test_tftp_generate_request(SEND_FILE, 1024, nullptr, nullptr, nullptr,
                                                 nullptr, &kTimeout, nullptr);
}

static bool test_tftp_generate_wrq_override_windowsize(void) {
    uint16_t kWindowSize = 32;
    return test_tftp_generate_request(SEND_FILE, 1024, nullptr, nullptr, nullptr,
                                                 nullptr, nullptr, &kWindowSize);
}

static bool test_tftp_generate_rrq_default(void) {
    return test_tftp_generate_request(RECV_FILE, 1024, nullptr, nullptr, nullptr,
                                                       nullptr, nullptr, nullptr);
}

static bool test_tftp_generate_rrq_options(void) {
    constexpr uint16_t kBlockSize = 555;
    constexpr uint8_t kTimeout = 3;
    constexpr uint16_t kWindowSize = 44;
    return test_tftp_generate_request(RECV_FILE, 1024, &kBlockSize, &kTimeout, &kWindowSize,
                                                       nullptr, nullptr, nullptr);
}

static bool test_tftp_generate_rrq_override_blocksize(void) {
    constexpr uint16_t kBlockSize = 1000;
    return test_tftp_generate_request(RECV_FILE, 1024, nullptr, nullptr, nullptr,
                                                       &kBlockSize, nullptr, nullptr);
}

static bool test_tftp_generate_rrq_override_timeout(void) {
    uint8_t kTimeout = 60;
    return test_tftp_generate_request(RECV_FILE, 1024, nullptr, nullptr, nullptr,
                                                 nullptr, &kTimeout, nullptr);
}

static bool test_tftp_generate_rrq_override_windowsize(void) {
    uint16_t kWindowSize = 32;
    return test_tftp_generate_request(RECV_FILE, 1024, nullptr, nullptr, nullptr,
                                                 nullptr, nullptr, &kWindowSize);
}

bool verify_response_opcode(const test_state& ts, uint16_t opcode) {
    ASSERT_GT(ts.outlen, 0, "outlen must not be zero");
    auto msg = reinterpret_cast<tftp_msg*>(ts.out);
    // The upper byte of the opcode is ignored
    EXPECT_EQ(ntohs(msg->opcode) & 0xff, opcode, "bad opcode");
    return true;
}

static bool test_tftp_receive_request_unexpected(tftp_file_direction dir) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    size_t req_file_size = (dir == SEND_FILE) ? ts.msg_size : 0;
    auto status = tftp_generate_request(ts.session, dir, kLocalFilename, kRemoteFilename,
        MODE_OCTET, req_file_size, NULL, NULL, NULL, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "could not generate request");
    if (dir == SEND_FILE) {
        ASSERT_TRUE(verify_write_request(ts), "bad write request");
    } else {
        ASSERT_TRUE(verify_read_request(ts), "bad read request");
    }

    ASSERT_LE(ts.outlen, 1500, "outlen too large");
    uint8_t buf[1500];
    memcpy(buf, ts.out, ts.outlen);

    // We are unprepared to service a request after we have sent one out
    status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout,
                              nullptr);
    EXPECT_EQ(TFTP_ERR_BAD_STATE, status, "receive should fail");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ERROR), "bad error response");

    END_TEST;
}

static bool test_tftp_receive_wrq_unexpected(void) {
    return test_tftp_receive_request_unexpected(SEND_FILE);
}

static bool test_tftp_receive_rrq_unexpected(void) {
    return test_tftp_receive_request_unexpected(RECV_FILE);
}

static bool test_tftp_receive_request_too_large(tftp_file_direction dir) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    uint8_t buf[1024] = { 0 };
    buf[1] = (dir == SEND_FILE) ? OPCODE_WRQ : OPCODE_RRQ;

    auto status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout,
                                   nullptr);
    EXPECT_LT(status, 0, "receive should fail");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ERROR), "bad error response");

    END_TEST;
}

static bool test_tftp_receive_wrq_too_large(void) {
    return test_tftp_receive_request_too_large(SEND_FILE);
}

static bool test_tftp_receive_rrq_too_large(void) {
    return test_tftp_receive_request_too_large(RECV_FILE);
}

static bool test_tftp_receive_request_no_tsize(tftp_file_direction dir) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    char buf[256];
    buf[0] = 0x00;
    buf[1] = (dir == SEND_FILE) ? OPCODE_WRQ : OPCODE_RRQ;
    size_t buf_sz = 2 + snprintf(&buf[2], sizeof(buf) - 2,
                                 "%s%cOCTET",
                                 kRemoteFilename, '\0')
                      + 1;

    ASSERT_LT(buf_sz, (int)sizeof(buf), "insufficient space for message");
    auto status = tftp_process_msg(ts.session, buf, buf_sz, ts.out, &ts.outlen, &ts.timeout,
                                   nullptr);
    EXPECT_EQ(TFTP_ERR_BAD_STATE, status, "tftp session should fail");
    EXPECT_EQ(ERROR, ts.session->state, "tftp session in wrong state");
    EXPECT_EQ(0, ts.session->file_size, "tftp session bad file size");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_ERROR), "bad error response");

    END_TEST;
}

static bool test_tftp_receive_wrq_no_tsize(void) {
    return test_tftp_receive_request_no_tsize(SEND_FILE);
}

static bool test_tftp_receive_rrq_no_tsize(void) {
    return test_tftp_receive_request_no_tsize(RECV_FILE);
}

static bool test_tftp_receive_request_send_oack(tftp_file_direction dir) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_open_write_cb open_write_cb =
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kRemoteFilename,
                              "bad remote filename in open_write callback");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            };
    tftp_file_open_read_cb open_read_cb =
            [](const char* filename, void* cookie) -> ssize_t {
                EXPECT_STR_EQ(filename, kRemoteFilename,
                              "bad remote filename in open_read callback");
                return 0;
            };
    tftp_file_interface ifc = {open_read_cb, open_write_cb, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);
    uint16_t default_block_size = 13;
    uint8_t default_timeout = 2;
    uint16_t default_window_size = 42;
    tftp_set_options(ts.session, &default_block_size, &default_timeout, &default_window_size);

    size_t req_file_size = (dir == SEND_FILE) ? 1024 : 0;
    char buf[256];
    buf[0] = 0x00;
    buf[1] = (dir == SEND_FILE) ? OPCODE_WRQ : OPCODE_RRQ;
    size_t buf_sz = 2 + snprintf(&buf[2], sizeof(buf) - 2,
                                 "%s%cOCTET%cTSIZE%c%zu",
                                 kRemoteFilename, '\0', '\0', '\0', req_file_size)
                      + 1;

    ASSERT_LT(buf_sz, (int)sizeof(buf), "insufficient space for WRQ message");
    auto status = tftp_process_msg(ts.session, buf, buf_sz, ts.out, &ts.outlen, &ts.timeout,
                                   nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive request failed");
    EXPECT_EQ(REQ_RECEIVED, ts.session->state, "tftp session in wrong state");
    EXPECT_EQ(req_file_size, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    // The request was made from the client's perspective, so our state is the inverse
    tftp_file_direction our_direction = (dir == SEND_FILE) ? RECV_FILE : SEND_FILE;
    EXPECT_EQ(our_direction, ts.session->direction, "tftp session has bad direction");

    const char* msg = static_cast<const char*>(ts.out);
    const char win_sz_str[] = "WINDOWSIZE";
    EXPECT_FALSE(find_str_in_mem(win_sz_str, sizeof(win_sz_str), msg, ts.outlen),
                 "window size in oack, but not in request");
    const char timeout_str[] = "TIMEOUT";
    EXPECT_FALSE(find_str_in_mem(timeout_str, sizeof(timeout_str), msg, ts.outlen),
                 "timeout in oack, but not in request");
    const char block_sz_str[] = "BLKSIZE";
    EXPECT_FALSE(find_str_in_mem(block_sz_str, sizeof(block_sz_str), msg, ts.outlen),
                 "block size in oack, but not in request");

    END_TEST;
}

static bool test_tftp_receive_wrq_send_oack(void) {
    return test_tftp_receive_request_send_oack(SEND_FILE);
}

static bool test_tftp_receive_rrq_send_oack(void) {
    return test_tftp_receive_request_send_oack(RECV_FILE);
}

static ssize_t dummy_open_read(const char* filename, void* cookie) {
    return 1024;
}

static tftp_status dummy_open_write(const char* filename, size_t size, void* cookie) {
    return 0;
}

static bool test_tftp_receive_request_options(tftp_file_direction dir,
                                              const uint16_t* server_block_size,
                                              const uint8_t* server_timeout,
                                              const uint16_t* server_window_size,
                                              const uint16_t* client_block_size,
                                              const uint8_t* client_timeout,
                                              const uint16_t* client_window_size,
                                              bool force_block_size,
                                              bool force_timeout,
                                              bool force_window_size) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {dummy_open_read, dummy_open_write, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);
    tftp_status status = tftp_set_options(ts.session, server_block_size, server_timeout,
                                          server_window_size);
    ASSERT_EQ(TFTP_NO_ERROR, status, "failed to set server options");

    size_t req_file_size = (dir == SEND_FILE) ? 1024 : 0;
    char buf[256];
    buf[0] = 0x00;
    buf[1] = (dir == SEND_FILE) ? OPCODE_WRQ : OPCODE_RRQ;
    size_t buf_sz = 2;
    buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                       "%s%cOCTET%cTSIZE%c%zu",
                       kRemoteFilename, '\0', '\0', '\0', req_file_size) + 1;
    if (client_block_size) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "BLKSIZE%s%c%d",
                           force_block_size ? "!" : "", '\0', *client_block_size) + 1;
    }
    if (client_timeout) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "TIMEOUT%s%c%d",
                           force_timeout ? "!" : "", '\0', *client_timeout) + 1;
    }
    if (client_window_size) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "WINDOWSIZE%s%c%d",
                           force_window_size ? "!" : "", '\0', *client_window_size) + 1;
    }

    ASSERT_LT(buf_sz, (int)sizeof(buf), "insufficient space for request");
    status = tftp_process_msg(ts.session, buf, buf_sz, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    EXPECT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    const char* msg = static_cast<const char*>(ts.out);
    char opt_str[256];
    size_t opt_str_sz;

    if (client_block_size) {
        uint16_t negotiated_block_size = (server_block_size && !force_block_size)
                                         ? *server_block_size
                                         : *client_block_size;
        opt_str_sz = snprintf(opt_str, sizeof(opt_str),
                              "BLKSIZE%c%d",
                              '\0', negotiated_block_size) + 1;
        EXPECT_TRUE(find_str_in_mem(opt_str, opt_str_sz, msg, ts.outlen),
                    "block size not correct in oack");
        EXPECT_EQ(negotiated_block_size, ts.session->block_size, "bad session: block size");
    } else {
        opt_str_sz = snprintf(opt_str, sizeof(opt_str), "BLKSIZE") + 1;
        EXPECT_FALSE(find_str_in_mem(opt_str, opt_str_sz, msg, ts.outlen),
                     "block size appears in oack, but not in request");
        EXPECT_EQ(DEFAULT_BLOCKSIZE, ts.session->block_size, "bad session: block size");
    }

    if (client_timeout) {
        uint8_t negotiated_timeout = (server_timeout && !force_timeout)
                                     ? *server_timeout
                                     : *client_timeout;
        opt_str_sz = snprintf(opt_str, sizeof(opt_str),
                              "TIMEOUT%c%d",
                              '\0', negotiated_timeout) + 1;
        EXPECT_TRUE(find_str_in_mem(opt_str, opt_str_sz, msg, ts.outlen),
                    "timeout not correct in oack");
        EXPECT_EQ(negotiated_timeout, ts.session->timeout, "bad session: timeout");
    } else {
        opt_str_sz = snprintf(opt_str, sizeof(opt_str), "TIMEOUT") + 1;
        EXPECT_FALSE(find_str_in_mem(opt_str, opt_str_sz, msg, ts.outlen),
                     "timeout appears in oack, but not in request");
        EXPECT_EQ(DEFAULT_TIMEOUT, ts.session->timeout, "bad session: timeout");
    }

    if (client_window_size) {
        uint16_t negotiated_window_size = (server_window_size && !force_window_size)
                                          ? *server_window_size
                                          : *client_window_size;
        opt_str_sz = snprintf(opt_str, sizeof(opt_str),
                              "WINDOWSIZE%c%d",
                              '\0', negotiated_window_size) + 1;
        EXPECT_TRUE(find_str_in_mem(opt_str, opt_str_sz, msg, ts.outlen),
                    "window size not correct in oack");
        EXPECT_EQ(negotiated_window_size, ts.session->window_size, "bad session: window size");
    } else {
        opt_str_sz = snprintf(opt_str, sizeof(opt_str), "WINDOWSIZE") + 1;
        EXPECT_FALSE(find_str_in_mem(opt_str, opt_str_sz, msg, ts.outlen),
                     "window size appears in oack, but not in request");
        EXPECT_EQ(DEFAULT_WINDOWSIZE, ts.session->window_size, "bad session: window size");
    }

    END_TEST;
}

static bool test_tftp_receive_wrq_blocksize(void) {
    constexpr uint8_t kDefaultTimeout = 4;
    constexpr uint16_t kBlocksize = 1024;
    return test_tftp_receive_request_options(SEND_FILE, nullptr, &kDefaultTimeout, nullptr,
                                                        &kBlocksize, nullptr, nullptr,
                                                        false, false, false);
}

static bool test_tftp_receive_wrq_timeout(void) {
    constexpr uint16_t kDefaultBlocksize = 115;
    constexpr uint8_t kTimeout = 3;
    return test_tftp_receive_request_options(SEND_FILE, &kDefaultBlocksize, nullptr, nullptr,
                                                        nullptr, &kTimeout, nullptr,
                                                        false, false, false);
}

static bool test_tftp_receive_wrq_windowsize(void) {
    constexpr uint16_t kWindowsize = 77;
    return test_tftp_receive_request_options(SEND_FILE, nullptr, nullptr, nullptr,
                                                        nullptr, nullptr, &kWindowsize,
                                                        false, false, false);
}

// Verify that if override values are set, they supercede the values in a normal request
static bool test_tftp_receive_wrq_have_overrides(void) {
    constexpr uint16_t kServerBlockSize = 302;
    constexpr uint8_t kServerTimeout = 7;
    constexpr uint16_t kServerWindowSize = 16;
    constexpr uint16_t kClientBlockSize = 32;
    constexpr uint8_t kClientTimeout = 9;
    constexpr uint16_t kClientWindowSize = 143;
    return test_tftp_receive_request_options(
                   SEND_FILE, &kServerBlockSize, &kServerTimeout, &kServerWindowSize,
                              &kClientBlockSize, &kClientTimeout, &kClientWindowSize,
                              false, false, false);
}

// Verify that if a WRQ has a '!' following an option it is honored, even if overrides are set
static bool test_tftp_receive_force_wrq_no_overrides(void) {
    constexpr uint16_t kServerBlockSize = 302;
    constexpr uint8_t kServerTimeout = 7;
    constexpr uint16_t kServerWindowSize = 16;
    constexpr uint16_t kClientBlockSize = 32;
    constexpr uint8_t kClientTimeout = 9;
    constexpr uint16_t kClientWindowSize = 143;
    return test_tftp_receive_request_options(
                   SEND_FILE, &kServerBlockSize, &kServerTimeout, &kServerWindowSize,
                              &kClientBlockSize, &kClientTimeout, &kClientWindowSize,
                              true, true, true);
}

static bool test_tftp_receive_force_wrq_have_overrides(void) {
    constexpr uint16_t kClientBlockSize = 32;
    constexpr uint8_t kClientTimeout = 9;
    constexpr uint16_t kClientWindowSize = 143;
    return test_tftp_receive_request_options(
                   SEND_FILE, nullptr, nullptr, nullptr,
                              &kClientBlockSize, &kClientTimeout, &kClientWindowSize,
                              true, true, true);
}

static bool test_tftp_receive_rrq_blocksize(void) {
    constexpr uint8_t kDefaultTimeout = 4;
    constexpr uint16_t kBlocksize = 1024;
    return test_tftp_receive_request_options(RECV_FILE, nullptr, &kDefaultTimeout, nullptr,
                                                        &kBlocksize, nullptr, nullptr,
                                                        false, false, false);
}

static bool test_tftp_receive_rrq_timeout(void) {
    constexpr uint16_t kDefaultBlocksize = 115;
    constexpr uint8_t kTimeout = 3;
    return test_tftp_receive_request_options(RECV_FILE, &kDefaultBlocksize, nullptr, nullptr,
                                                        nullptr, &kTimeout, nullptr,
                                                        false, false, false);
}

static bool test_tftp_receive_rrq_windowsize(void) {
    constexpr uint16_t kWindowsize = 77;
    return test_tftp_receive_request_options(RECV_FILE, nullptr, nullptr, nullptr,
                                                        nullptr, nullptr, &kWindowsize,
                                                        false, false, false);
}

// Verify that if override values are set, they supercede the values in a normal request
static bool test_tftp_receive_rrq_have_overrides(void) {
    constexpr uint16_t kServerBlockSize = 302;
    constexpr uint8_t kServerTimeout = 7;
    constexpr uint16_t kServerWindowSize = 16;
    constexpr uint16_t kClientBlockSize = 32;
    constexpr uint8_t kClientTimeout = 9;
    constexpr uint16_t kClientWindowSize = 143;
    return test_tftp_receive_request_options(
                   RECV_FILE, &kServerBlockSize, &kServerTimeout, &kServerWindowSize,
                              &kClientBlockSize, &kClientTimeout, &kClientWindowSize,
                              false, false, false);
}

// Verify that if a WRQ has a '!' following an option it is honored, even if overrides are set
static bool test_tftp_receive_force_rrq_no_overrides(void) {
    constexpr uint16_t kServerBlockSize = 302;
    constexpr uint8_t kServerTimeout = 7;
    constexpr uint16_t kServerWindowSize = 16;
    constexpr uint16_t kClientBlockSize = 32;
    constexpr uint8_t kClientTimeout = 9;
    constexpr uint16_t kClientWindowSize = 143;
    return test_tftp_receive_request_options(
                   RECV_FILE, &kServerBlockSize, &kServerTimeout, &kServerWindowSize,
                              &kClientBlockSize, &kClientTimeout, &kClientWindowSize,
                              true, true, true);
}

static bool test_tftp_receive_force_rrq_have_overrides(void) {
    constexpr uint16_t kClientBlockSize = 32;
    constexpr uint8_t kClientTimeout = 9;
    constexpr uint16_t kClientWindowSize = 143;
    return test_tftp_receive_request_options(
                   RECV_FILE, nullptr, nullptr, nullptr,
                              &kClientBlockSize, &kClientTimeout, &kClientWindowSize,
                              true, true, true);
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
    // The upper byte of the opcode is ignored
    EXPECT_EQ(OPCODE_DATA, ntohs(msg->opcode) & 0xff, "bad opcode");
    // Don't continue if we have a bad block number or we risk OOB reads
    ASSERT_EQ(td.expected.block, ntohs(msg->block), "bad block number");
    EXPECT_BYTES_EQ(td.expected.data, msg->data, td.actual.len, "read data mismatch");
    END_HELPER;
}

tftp_status mock_open_write(const char* filename, size_t size, void* file_cookie) {
    return TFTP_NO_ERROR;
}

tftp_status mock_read(void* data, size_t* len, off_t offset, void* cookie) {
    tx_test_data* td = static_cast<tx_test_data*>(cookie);
    td->actual.len = *len;
    td->actual.offset = offset;
    memcpy(data, td->expected.data, *len);
    return static_cast<tftp_status>(*len);
}

static bool test_tftp_receive_wrq_oack(const size_t file_size,
                                       const uint16_t* block_size_ptr,
                                       const uint8_t* timeout_ptr,
                                       const uint16_t* window_size_ptr) {
    BEGIN_TEST;

    uint16_t block_size = block_size_ptr ? *block_size_ptr : DEFAULT_BLOCKSIZE;
    uint8_t timeout = timeout_ptr ? *timeout_ptr : DEFAULT_TIMEOUT;
    uint16_t window_size = window_size_ptr ? *window_size_ptr : DEFAULT_WINDOWSIZE;

    test_state ts;
    ts.reset(1024, file_size, 1500);

    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
                                        MODE_OCTET, ts.msg_size, block_size_ptr, timeout_ptr,
                                        window_size_ptr, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    ASSERT_TRUE(verify_write_request(ts), "bad write request");

    char buf[256];
    buf[0] = 0x00;
    buf[1] = 0x06; // Opcode (OACK)
    size_t buf_sz = 2;
    buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                       "%s%cOCTET%cTSIZE%c%zu",
                       kRemoteFilename, '\0', '\0', '\0', file_size) + 1;
    if (block_size_ptr) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "BLKSIZE%c%d",
                           '\0', block_size) + 1;
    }
    if (timeout_ptr) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "TIMEOUT%c%d",
                           '\0', timeout) + 1;
    }
    if (window_size_ptr) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "WINDOWSIZE%c%d",
                           '\0', window_size) + 1;
    }

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    td.expected.len = block_size;
    td.expected.data[block_size - 1] = 'X';
    status = tftp_process_msg(ts.session, buf, buf_sz, ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "tftp_process_msg failed");
    bool has_pending = (block_size < file_size) && (window_size > 1);
    EXPECT_EQ(has_pending, tftp_session_has_pending(ts.session),
              "Unexpected tftp_session_has_pending()");
    EXPECT_EQ(FIRST_DATA, ts.session->state, "session should be in state FIRST_DATA");
    EXPECT_EQ(file_size, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(block_size, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(timeout, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(window_size, ts.session->window_size, "bad session: window size");

    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + block_size, "bad outlen");
    EXPECT_EQ(ts.timeout, timeout * 1000U, "timeout should be set");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");

    if (has_pending) {
        // Since pending is true, call for a second data packet to transmit
        // Updated the read offset and change a few bytes
        td.expected.block = 2;
        size_t second_block_size = fbl::min<size_t>(file_size - block_size, block_size);
        td.expected.offset = block_size;
        td.expected.data[0] = 'X';
        if (second_block_size > 0) {
            td.expected.data[second_block_size - 1] = 'F';
        }

        status = tftp_prepare_data(ts.session, ts.out, &ts.outlen, &ts.timeout, &td);
        EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + block_size, "bad outlen");
        EXPECT_TRUE(verify_read_data(ts, td), "bad test data");
        has_pending = (block_size + second_block_size < file_size) && (window_size > 2);
        EXPECT_EQ(has_pending, tftp_session_has_pending(ts.session),
                  "Unexpected tftp_session_has_pending()");
    }
    END_TEST;
}

static bool test_tftp_receive_wrq_oack(void) {
    return test_tftp_receive_wrq_oack(1024, NULL, NULL, NULL);
}

static bool test_tftp_receive_wrq_oack_blocksize(void) {
    constexpr uint16_t kBlockSize = 1024;
    return test_tftp_receive_wrq_oack(2048, &kBlockSize, nullptr, nullptr);
}

static bool test_tftp_receive_wrq_oack_timeout(void) {
    constexpr uint8_t kTimeout = 5;
    return test_tftp_receive_wrq_oack(1024, nullptr, &kTimeout, nullptr);
}

static bool test_tftp_receive_wrq_oack_windowsize(void) {
    constexpr uint16_t kWindowSize = 2;
    return test_tftp_receive_wrq_oack(4096, nullptr, nullptr, &kWindowSize);
}

static bool test_tftp_receive_rrq_oack(size_t file_size,
                                       const uint16_t* block_size_ptr,
                                       const uint8_t* timeout_ptr,
                                       const uint16_t* window_size_ptr) {
    BEGIN_TEST;

    uint16_t block_size = block_size_ptr ? *block_size_ptr : DEFAULT_BLOCKSIZE;
    uint8_t timeout = timeout_ptr ? *timeout_ptr : DEFAULT_TIMEOUT;
    uint16_t window_size = window_size_ptr ? *window_size_ptr : DEFAULT_WINDOWSIZE;

    test_state ts;
    ts.reset(1024, file_size, 1500);

    auto status = tftp_generate_request(ts.session, RECV_FILE, kLocalFilename, kRemoteFilename,
                                        MODE_OCTET, 0, block_size_ptr, timeout_ptr,
                                        window_size_ptr, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating read request");
    ASSERT_TRUE(verify_read_request(ts), "bad read request");

    char buf[256];
    buf[0] = 0x00;
    buf[1] = 0x06; // Opcode (OACK)
    size_t buf_sz = 2;
    buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                       "TSIZE%c%zu",
                       '\0', file_size) + 1;
    if (block_size_ptr) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "BLKSIZE%c%d",
                           '\0', block_size) + 1;
    }
    if (timeout_ptr) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "TIMEOUT%c%d",
                           '\0', timeout) + 1;
    }
    if (window_size_ptr) {
        buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                           "WINDOWSIZE%c%d",
                           '\0', window_size) + 1;
    }
    tftp_file_interface ifc = {NULL, mock_open_write, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    status = tftp_process_msg(ts.session, buf, buf_sz, ts.out, &ts.outlen, &ts.timeout, NULL);
    EXPECT_FALSE(tftp_session_has_pending(ts.session), "session should not have pending data");
    EXPECT_EQ(FIRST_DATA, ts.session->state, "session should be in state FIRST_DATA");
    EXPECT_EQ(file_size, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(block_size, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(timeout, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(window_size, ts.session->window_size, "bad session: window size");

    uint8_t expected_ack[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x00, 0x00   // Block
    };
    EXPECT_EQ(sizeof(expected_ack), ts.outlen, "response size mismatch");
    EXPECT_EQ(0, memcmp(expected_ack, ts.out, sizeof(expected_ack)), "bad response");
    END_TEST;
}

static bool test_tftp_receive_rrq_oack(void) {
    return test_tftp_receive_rrq_oack(1024, nullptr, nullptr, nullptr);
}

static bool test_tftp_receive_rrq_oack_blocksize(void) {
    constexpr uint16_t kBlockSize = 1024;
    return test_tftp_receive_rrq_oack(2048, &kBlockSize, nullptr, nullptr);
}

static bool test_tftp_receive_rrq_oack_timeout(void) {
    constexpr uint8_t kTimeout = 5;
    return test_tftp_receive_rrq_oack(1024, nullptr, &kTimeout, nullptr);
}

static bool test_tftp_receive_rrq_oack_windowsize(void) {
    constexpr uint16_t kWindowSize = 412;
    return test_tftp_receive_rrq_oack(1024, nullptr, nullptr, &kWindowSize);
}

// Verify that if the server overrides our settings we use the oack'd settings it provides
static bool test_tftp_receive_oack_overrides(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 4096, 1500);

    uint16_t kBlockSize = 14;
    uint8_t kTimeout = 12;
    uint16_t kWindowSize = 6;

    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
        MODE_OCTET, ts.msg_size, &kBlockSize, &kTimeout, &kWindowSize, ts.out, &ts.outlen,
        &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating write request");

    uint8_t buf[] = {
        0x00, 0x06,                                   // Opcode (OACK)
        'T', 'S', 'I', 'Z', 'E', 0x00,                // Option
        '4', '0', '9', '6', 0x00,                     // TSIZE value
        'B', 'L', 'K', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '5', '5', 0x00,                               // BLKSIZE value
        'T', 'I', 'M', 'E', 'O', 'U', 'T', 0x00,      // Option
        '3', 0x00,                                    // TIMEOUT value
        'W', 'I', 'N', 'D', 'O', 'W', 'S', 'I', 'Z', 'E', 0x00,      // Option
        '2', '1', '4', 0x00,                          // WINDOWSIZE value
    };

    tftp_file_interface ifc = {NULL, NULL, mock_read, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    tx_test_data td;
    status = tftp_process_msg(ts.session, buf, sizeof(buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(FIRST_DATA, ts.session->state, "session should be in state FIRST_DATA");
    EXPECT_EQ(4096, ts.session->file_size, "tftp session bad file size");
    EXPECT_EQ(55, ts.session->block_size, "bad session: block size");
    EXPECT_EQ(3, ts.session->timeout, "bad session: timeout");
    EXPECT_EQ(214, ts.session->window_size, "bad session: window size");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + ts.session->block_size, "bad outlen");

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
    ASSERT_EQ(td.expected.offset, td.actual.offset, "write offset mismatch");
    ASSERT_EQ(td.expected.len, td.actual.len, "write length mismatch");
    EXPECT_BYTES_EQ(expected, td.actual.data + td.actual.offset, td.actual.len, "write data mismatch");
    END_HELPER;
}

static bool test_tftp_receive_data(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kRemoteFilename, "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    char req_buf[256];
    req_buf[0] = 0x00;
    req_buf[1] = OPCODE_WRQ;
    size_t req_buf_sz = 2 + snprintf(&req_buf[2], sizeof(req_buf) - 2,
                                     "%s%cOCTET%cTSIZE%c%d",
                                     kRemoteFilename, '\0', '\0', '\0', 1024)
                          + 1;

    ASSERT_LT(req_buf_sz, (int)sizeof(req_buf), "insufficient space for WRQ message");
    auto status = tftp_process_msg(ts.session, req_buf, req_buf_sz, ts.out, &ts.outlen,
                                   &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(REQ_RECEIVED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x00, 0x01,  // Block
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
                EXPECT_STR_EQ(filename, kRemoteFilename, "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    char req_buf[256];
    req_buf[0] = 0x00;
    req_buf[1] = OPCODE_WRQ;
    size_t req_buf_sz = 2 + snprintf(&req_buf[2], sizeof(req_buf) - 2,
                                     "%s%cOCTET%cTSIZE%c%d",
                                     kRemoteFilename, '\0', '\0', '\0', 1024)
                          + 1;

    ASSERT_LT(req_buf_sz, (int)sizeof(req_buf), "insufficient space for WRQ message");
    auto status = tftp_process_msg(ts.session, req_buf, req_buf_sz, ts.out, &ts.outlen, &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(REQ_RECEIVED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x00, 0x01,  // Block
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
    data_buf[3]++;
    data_buf[4]++;
    data_buf[515]++;
    td.expected.block++;
    td.expected.offset = DEFAULT_BLOCKSIZE;

    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_ACK), "bad response");
    EXPECT_TRUE(verify_write_data(data_buf + 4, td), "bad write data");

    // Last data packet. Empty, indicating end of data.
    data_buf[3]++;
    status = tftp_process_msg(ts.session, data_buf, 4, ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_TRANSFER_COMPLETED, status, "receive data failed");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_ACK), "bad response");
    END_TEST;
}

static bool test_tftp_receive_data_blocksize(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kRemoteFilename, "bad filename");
                EXPECT_EQ(size, 2048, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    char req_buf[256];
    req_buf[0] = 0x00;
    req_buf[1] = OPCODE_WRQ;
    size_t req_buf_sz = 2 + snprintf(&req_buf[2], sizeof(req_buf) - 2,
                                     "%s%cOCTET%cTSIZE%c%d%cBLKSIZE%c%d",
                                     kRemoteFilename, '\0', '\0', '\0', 2048, '\0', '\0', 1024)
                          + 1;

    ASSERT_LT(req_buf_sz, (int)sizeof(req_buf), "insufficient space for WRQ message");
    auto status = tftp_process_msg(ts.session, req_buf, req_buf_sz, ts.out, &ts.outlen,
                                   &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(REQ_RECEIVED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[1028] = {
        0x00, 0x03,  // Opcode (DATA)
        0x00, 0x01,  // Block
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
                EXPECT_STR_EQ(filename, kRemoteFilename, "bad filename");
                EXPECT_EQ(size, 1025, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    char req_buf[256];
    req_buf[0] = 0x00;
    req_buf[1] = OPCODE_WRQ;
    size_t req_buf_sz = 2 + snprintf(&req_buf[2], sizeof(req_buf) - 2,
                                     "%s%cOCTET%cTSIZE%c%d%cWINDOWSIZE%c%d",
                                     kRemoteFilename, '\0', '\0', '\0', 1025, '\0', '\0', 2)
                          + 1;

    ASSERT_LT(req_buf_sz, (int)sizeof(req_buf), "insufficient space for WRQ message");
    auto status = tftp_process_msg(ts.session, req_buf, req_buf_sz, ts.out, &ts.outlen,
                                   &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(REQ_RECEIVED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x00, 0x01,  // Block
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
    data_buf[3]++;
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
                EXPECT_STR_EQ(filename, kRemoteFilename, "bad filename");
                EXPECT_EQ(size, 1024, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    char req_buf[256];
    req_buf[0] = 0x00;
    req_buf[1] = OPCODE_WRQ;
    size_t req_buf_sz = 2 + snprintf(&req_buf[2], sizeof(req_buf) - 2,
                                     "%s%cOCTET%cTSIZE%c%d",
                                     kRemoteFilename, '\0', '\0', '\0', 1024)
                          + 1;

    ASSERT_LT(req_buf_sz, (int)sizeof(req_buf), "insufficient space for WRQ message");
    auto status = tftp_process_msg(ts.session, req_buf, req_buf_sz, ts.out, &ts.outlen,
                                   &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(REQ_RECEIVED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    // This is block 2, meaning we missed block 1 somehow.
    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x00, 0x02,  // Block
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // Data; compiler will fill out the rest with zeros
    };
    data_buf[515] = 0x79;  // set the last byte to make sure it all gets copied

    ifc.write = mock_write;
    tftp_session_set_file_interface(ts.session, &ifc);

    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    ASSERT_GT(ts.outlen, 0, "outlen must not be zero");
    auto msg = reinterpret_cast<tftp_data_msg*>(ts.out);
    EXPECT_EQ(ntohs(msg->opcode) & 0xff, OPCODE_ACK, "bad opcode");
    // The opcode prefix should have been advanced when we saw a dropped block
    EXPECT_EQ((ntohs(msg->opcode) & 0xff00) >> 8, 1, "bad opcode prefix");
    EXPECT_EQ(ntohs(msg->block), 0, "bad block number");
    EXPECT_EQ(0, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    // Verify with the opcode prefix disabled
    tftp_session_set_opcode_prefix_use(ts.session, false);
    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    ASSERT_GT(ts.outlen, 0, "outlen must not be zero");
    msg = reinterpret_cast<tftp_data_msg*>(ts.out);
    EXPECT_EQ(ntohs(msg->opcode) & 0xff, OPCODE_ACK, "bad opcode");
    EXPECT_EQ((ntohs(msg->opcode) & 0xff00) >> 8, 0, "bad opcode prefix");
    EXPECT_EQ(ntohs(msg->block), 0, "bad block number");

    END_TEST;
}

static bool test_tftp_receive_data_windowsize_skipped_block(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);
    tftp_file_interface ifc = {NULL,
            [](const char* filename, size_t size, void* cookie) -> tftp_status {
                EXPECT_STR_EQ(filename, kRemoteFilename, "bad filename");
                EXPECT_EQ(size, 2048, "bad file size");
                return 0;
            }, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    char req_buf[256];
    req_buf[0] = 0x00;
    req_buf[1] = OPCODE_WRQ;
    size_t req_buf_sz = 2 + snprintf(&req_buf[2], sizeof(req_buf) - 2,
                                     "%s%cOCTET%cTSIZE%c%d%cWINDOWSIZE%c%d",
                                     kRemoteFilename, '\0', '\0', '\0', 2048, '\0', '\0', 3)
                          + 1;

    ASSERT_LT(req_buf_sz, (int)sizeof(req_buf), "insufficient space for WRQ message");
    auto status = tftp_process_msg(ts.session, req_buf, req_buf_sz, ts.out, &ts.outlen,
                                   &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(REQ_RECEIVED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    uint8_t data_buf[516] = {
        0x00, 0x03,  // Opcode (DATA)
        0x00, 0x01,  // Block
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
    data_buf[3]++;
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
    data_buf[3] = 4u;
    data_buf[4]++;
    data_buf[515]++;
    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive data failed");
    ASSERT_GT(ts.outlen, 0, "outlen must not be zero");
    auto msg = reinterpret_cast<tftp_data_msg*>(ts.out);
    EXPECT_EQ(ntohs(msg->opcode) & 0xff, OPCODE_ACK, "bad opcode");
    // Opcode prefix should have been incremented when a packet was not received
    EXPECT_EQ((ntohs(msg->opcode) & 0xff00) >> 8, 1, "bad opcode prefix");
    EXPECT_EQ(ntohs(msg->block), 2, "bad block number");
    EXPECT_EQ(0, td.actual.data[1024], "block 3 should be empty");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    // Reset the window index after sending the ack with the last known block
    EXPECT_EQ(0, ts.session->window_index, "tftp session window index mismatch");

    END_TEST;
}

static bool test_tftp_receive_data_block_wrapping(void) {
    BEGIN_TEST;

    constexpr unsigned long kWrapAt = 0x3ffff;
    constexpr int kBlockSize = 8;
    constexpr unsigned long kFileSize = (kWrapAt + 2) * kBlockSize;

    test_state ts;
    static bool write_called;
    write_called = false;
    ts.reset(1024, 2048, 2048);

    tftp_file_interface ifc = {NULL, NULL, NULL, NULL, NULL};
    ifc.open_write = [](const char* filename, size_t size, void* cookie) -> tftp_status {
                         EXPECT_STR_EQ(filename, kRemoteFilename,
                             "bad filename");
                         EXPECT_EQ(size, kFileSize, "bad file size");
                         return 0;
                     };
    ifc.write = [](const void* data, size_t* length, off_t offset, void* file_cookie)
                    -> tftp_status {
                    // Remember that the block count starts at zero, which makes the offset
                    // calculation a bit counter-intuitive (one might expect that we would
                    // be writing to (kWrapAt + 1) * kBlockSize).
                    EXPECT_EQ(kWrapAt * kBlockSize, offset, "block count failed to wrap");
                    write_called = true;
                    return TFTP_NO_ERROR;
                };
    tftp_session_set_file_interface(ts.session, &ifc);

    char req_buf[1024];
    req_buf[0] = 0x00;
    req_buf[1] = OPCODE_WRQ;
    int req_buf_sz = 2 + snprintf(&req_buf[2], sizeof(req_buf) - 2,
                                  "%s%cOCTET%cTSIZE%c%lu%cBLKSIZE%c%d",
                                  kRemoteFilename, '\0', '\0', '\0', kFileSize, '\0', '\0',
                                  kBlockSize)
                       + 1;

    ASSERT_LT(req_buf_sz, (int)sizeof(req_buf), "insufficient space for WRQ message");
    auto status = tftp_process_msg(ts.session, req_buf, req_buf_sz, ts.out, &ts.outlen,
                                   &ts.timeout, nullptr);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive write request failed");
    ASSERT_EQ(REQ_RECEIVED, ts.session->state, "tftp session in wrong state");
    ASSERT_TRUE(verify_response_opcode(ts, OPCODE_OACK), "bad response");

    // Artificially advance to force block wrapping
    ts.session->block_number = kWrapAt;
    ts.session->window_index = 0;

    uint8_t data_buf[] = {
        0x00, 0x03,  // Opcode (DATA)
        0x00, 0x00,  // Block
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08  // Data
    };

    status = tftp_process_msg(ts.session, data_buf, sizeof(data_buf), ts.out, &ts.outlen,
                              &ts.timeout, NULL);
    EXPECT_EQ(TFTP_NO_ERROR, status, "failed to process data");
    EXPECT_TRUE(write_called, "no attempt to write data");
    EXPECT_EQ(kWrapAt + 1, ts.session->block_number, "failed to advance block number");

    uint8_t expected_ack[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x00, 0x00   // Block
    };
    EXPECT_EQ(sizeof(expected_ack), ts.outlen, "response size mismatch");
    EXPECT_EQ(0, memcmp(expected_ack, ts.out, sizeof(expected_ack)), "bad response");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
        MODE_OCTET, ts.msg_size, NULL, NULL, NULL, ts.out, &ts.outlen, &ts.timeout);
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
        0x00, 0x01,  // Block
    };

    td.expected.block = 2;
    td.expected.offset += DEFAULT_BLOCKSIZE;
    td.expected.data[1] = 'f';
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(SENDING_DATA, ts.session->state, "session should be in state SENDING_DATA");
    // The block number will not advance until we see an ACK for block 2
    EXPECT_EQ(1, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(1, ts.session->window_index, "tftp session window index mismatch");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");

    END_TEST;
}

static bool test_tftp_send_data_receive_final_ack(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
        MODE_OCTET, ts.msg_size, NULL, NULL, NULL, ts.out, &ts.outlen, &ts.timeout);
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
        0x00, 0x01,  // Block
    };

    td.expected.block = 2;
    td.expected.offset += DEFAULT_BLOCKSIZE;
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");

    // second block
    ack_buf[3]++;
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    ASSERT_EQ(TFTP_NO_ERROR, status, "receive block 2 error");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg), "block 3 not empty");

    // Do not expect any more sends.
    ack_buf[3]++;
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, nullptr);
    EXPECT_EQ(TFTP_TRANSFER_COMPLETED, status, "tftp transfer should be complete");
    EXPECT_EQ(ts.outlen, 0, "no outgoing message expected");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack_skipped_block(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
        MODE_OCTET, ts.msg_size, NULL, NULL, NULL, ts.out, &ts.outlen, &ts.timeout);
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

    tx_test_data td2;
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td2);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(SENDING_DATA, ts.session->state, "session should be in state SENDING_DATA");
    EXPECT_EQ(0, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(1, ts.session->window_index, "tftp window index mismatch");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td2), "bad test data");
    END_TEST;
}

static bool test_tftp_send_data_receive_ack_window_size(void) {
    uint16_t kWindowSize = 2;
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 2048, 1500);

    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
        MODE_OCTET, ts.msg_size, NULL, NULL, &kWindowSize, ts.out, &ts.outlen, &ts.timeout);
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
    // Window index doesn't roll until we receive an ACK
    ASSERT_EQ(0, ts.session->block_number, "tftp session block number mismatch");
    ASSERT_EQ(2, ts.session->window_index, "tftp session window index mismatch");
    ASSERT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    ASSERT_TRUE(verify_read_data(ts, td), "bad test data");
    ASSERT_FALSE(tftp_session_has_pending(ts.session), "expected to wait for ack");

    uint8_t ack_buf[] = {
        0x00, 0x04,  // Opcode (ACK)
        0x00, 0x02,  // Block
    };

    td.expected.block++;
    td.expected.offset += DEFAULT_BLOCKSIZE;
    td.expected.data[1]++;
    status = tftp_process_msg(ts.session, ack_buf, sizeof(ack_buf), ts.out, &ts.outlen, &ts.timeout, &td);
    EXPECT_EQ(TFTP_NO_ERROR, status, "receive error");
    EXPECT_EQ(SENDING_DATA, ts.session->state, "session should be in state SENDING_DATA");
    EXPECT_EQ(2, ts.session->block_number, "tftp session block number mismatch");
    EXPECT_EQ(1, ts.session->window_index, "tftp session window index mismatch");
    EXPECT_EQ(ts.outlen, sizeof(tftp_data_msg) + DEFAULT_BLOCKSIZE, "bad outlen");
    EXPECT_TRUE(verify_read_data(ts, td), "bad test data");
    EXPECT_TRUE(tftp_session_has_pending(ts.session), "expected pending data to transmit");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack_block_wrapping(void) {
    BEGIN_TEST;

    constexpr unsigned long kWrapAt = 0x3ffff;
    constexpr uint16_t kBlockSize = 8;
    constexpr unsigned long kFileSize = (kWrapAt + 2) * kBlockSize;

    static int reads_performed;
    reads_performed = 0;

    test_state ts;
    ts.reset(1024, 2048, 2048);

    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
        MODE_OCTET, kFileSize, &kBlockSize, NULL, NULL, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");

    char oack_buf[256];
    oack_buf[0] = 0x00;
    oack_buf[1] = 0x06;
    size_t oack_buf_sz = 2 + snprintf(&oack_buf[2], sizeof(oack_buf) - 2,
                                      "TSIZE%c%lu%cBLKSIZE%c%d",
                                      '\0', kFileSize, '\0', '\0', kBlockSize)
                           + 1;

    ASSERT_LT(oack_buf_sz, (int)sizeof(oack_buf), "insufficient space for OACK message");
    tftp_file_interface ifc = {NULL, NULL, NULL, NULL, NULL};
    ifc.read = [](void* data, size_t* length, off_t offset, void* cookie) -> tftp_status {
                   EXPECT_EQ(0, offset, "incorrect initial read");
                   reads_performed++;
                   return TFTP_NO_ERROR;
               };
    tftp_session_set_file_interface(ts.session, &ifc);

    status = tftp_process_msg(ts.session, oack_buf, oack_buf_sz, ts.out, &ts.outlen,
                              &ts.timeout, NULL);
    ASSERT_EQ(TFTP_NO_ERROR, status, "failure to process OACK");
    EXPECT_EQ(1, reads_performed, "failed to call read function");

    // Artificially advance the session to a point where wrapping will occur
    ts.session->block_number = kWrapAt;
    ts.session->window_index = 0;

    uint8_t data_buf[4 + kBlockSize];
    size_t data_buf_len = sizeof(data_buf);
    ifc.read = [](void* data, size_t* length, off_t offset, void* cookie) -> tftp_status {
                   // Keep in mind that the block index starts at 1, so the offset calculation
                   // is not necessarily intuitive
                   EXPECT_EQ(kWrapAt * kBlockSize, offset, "incorrect wrapping read");
                   reads_performed++;
                   return TFTP_NO_ERROR;
               };
    tftp_session_set_file_interface(ts.session, &ifc);
    status = tftp_prepare_data(ts.session, data_buf, &data_buf_len, &ts.timeout, NULL);
    ASSERT_EQ(TFTP_NO_ERROR, status, "failed to generate DATA packet");
    EXPECT_EQ(2, reads_performed, "failed to call read function");
    EXPECT_EQ(sizeof(data_buf), data_buf_len, "improperly formatted DATA packet");
    unsigned int opcode = data_buf[0] << 8 | data_buf[1];
    EXPECT_EQ(0x0003, opcode, "incorrect DATA packet opcode");
    unsigned int block = data_buf[2] << 8 | data_buf[3];
    EXPECT_EQ(0x0000, block, "incorrect DATA packet block");

    END_TEST;
}

static bool test_tftp_send_data_receive_ack_skip_block_wrap(void) {
    BEGIN_TEST;

    constexpr unsigned long kLastBlockSent = 0x40003;
    constexpr unsigned long kAckBlock = 0x3fffb;
    constexpr uint16_t kBlockSize = 8;
    constexpr unsigned long kFileSize = 0x50000 * kBlockSize;

    static int reads_performed;
    reads_performed = 0;

    test_state ts;
    ts.reset(1024, 2048, 2048);

    // Create a write request
    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
        MODE_OCTET, kFileSize, &kBlockSize, NULL, NULL, ts.out, &ts.outlen, &ts.timeout);
    EXPECT_EQ(TFTP_NO_ERROR, status, "error generating write request");
    EXPECT_TRUE(verify_write_request(ts), "bad write request");

    // Simulate a response (OACK)
    char oack_buf[256];
    oack_buf[0] = 0x00;
    oack_buf[1] = 0x06;
    size_t oack_buf_sz = 2 + snprintf(&oack_buf[2], sizeof(oack_buf) - 2,
                                      "TSIZE%c%lu%cBLKSIZE%c%d",
                                      '\0', kFileSize, '\0', '\0', kBlockSize)
                           + 1;

    ASSERT_LT(oack_buf_sz, (int)sizeof(oack_buf), "insufficient space for OACK message");
    tftp_file_interface ifc = {NULL, NULL, NULL, NULL, NULL};
    ifc.read = [](void* data, size_t* length, off_t offset, void* cookie) -> tftp_status {
                   EXPECT_EQ(0, offset, "incorrect initial read");
                   reads_performed++;
                   return TFTP_NO_ERROR;
               };
    tftp_session_set_file_interface(ts.session, &ifc);

    // Process OACK and generate write of first block
    status = tftp_process_msg(ts.session, oack_buf, oack_buf_sz, ts.out, &ts.outlen,
                              &ts.timeout, NULL);
    ASSERT_EQ(TFTP_NO_ERROR, status, "failure to process OACK");
    EXPECT_EQ(1, reads_performed, "failed to call read function");

    // Artificially advance the session so we can test wrapping
    ts.session->block_number = kLastBlockSent;
    ts.session->window_index = 0;

    // Create a DATA packet for block kLastBlockSent + 1
    uint8_t data_buf[4 + kBlockSize] = {0};
    size_t data_buf_len = sizeof(data_buf);
    tftp_data_msg* msg = reinterpret_cast<tftp_data_msg*>(&data_buf[0]);
    ifc.read = [](void* data, size_t* length, off_t offset, void* cookie) -> tftp_status {
                   // Keep in mind that the block index starts at 1, so the offset calculation
                   // is not necessarily intuitive
                   EXPECT_EQ(kLastBlockSent * kBlockSize, offset, "incorrect read offset");
                   reads_performed++;
                   return TFTP_NO_ERROR;
               };
    tftp_session_set_file_interface(ts.session, &ifc);
    status = tftp_prepare_data(ts.session, data_buf, &data_buf_len, &ts.timeout, NULL);
    ASSERT_EQ(TFTP_NO_ERROR, status, "failed to generate DATA packet");
    EXPECT_EQ(2, reads_performed, "failed to call read function");
    EXPECT_EQ(sizeof(data_buf), data_buf_len, "improperly formatted DATA packet");
    unsigned int opcode = htons(msg->opcode);
    EXPECT_EQ(OPCODE_DATA, opcode, "incorrect DATA packet opcode");
    uint16_t offset = ntohs(msg->block);
    ASSERT_EQ((kLastBlockSent + 1) & 0xffff, offset, "incorrect DATA packet block");

    // Simulate an ACK response that is before our last block wrap
    tftp_data_msg ack_msg;
    ack_msg.opcode = htons(OPCODE_ACK);
    ack_msg.block = htons(kAckBlock & 0xffff);
    ifc.read = [](void* data, size_t* length, off_t offset, void* cookie) -> tftp_status {
                   EXPECT_EQ(kAckBlock * kBlockSize, offset, "incorrect read offset");
                   reads_performed++;
                   return TFTP_NO_ERROR;
               };
    tftp_session_set_file_interface(ts.session, &ifc);

    // Next DATA packet should backup to proper address (before wrap)
    status = tftp_process_msg(ts.session, reinterpret_cast<void*>(&ack_msg), sizeof(ack_msg),
                              ts.out, &ts.outlen, &ts.timeout, NULL);
    ASSERT_EQ(TFTP_NO_ERROR, status, "no ACK generated");
    EXPECT_EQ(3, reads_performed, "failed to call read function");
    ASSERT_EQ(ts.outlen, sizeof(tftp_data_msg) + kBlockSize, "improper DATA packet size");
    msg = reinterpret_cast<tftp_data_msg*>(ts.out);
    EXPECT_EQ(OPCODE_DATA, ntohs(msg->opcode) & 0xff, "incorrect DATA packet opcode");
    // Opcode prefix should have been incremented when a packet was dropped
    EXPECT_EQ(1, (ntohs(msg->opcode) & 0xff00) >> 8, "incorrect opcode prefix");
    EXPECT_EQ((kAckBlock + 1) & 0xffff, ntohs(msg->block), "incorrect DATA packet block");
    EXPECT_EQ(ts.session->block_number, kAckBlock, "session offset not rewound correctly");
    EXPECT_EQ(ts.session->window_index, 1, "window index not set correctly");

    // Try again, this time disabling opcode prefixes
    ifc.read = [](void* data, size_t* length, off_t offset, void* cookie) -> tftp_status {
                   return TFTP_NO_ERROR;
               };
    tftp_session_set_file_interface(ts.session, &ifc);
    tftp_session_set_opcode_prefix_use(ts.session, false);
    ack_msg.block = htons((kAckBlock + 1) & 0xffff);
    status = tftp_process_msg(ts.session, reinterpret_cast<void*>(&ack_msg), sizeof(ack_msg),
                              ts.out, &ts.outlen, &ts.timeout, NULL);
    ASSERT_EQ(TFTP_NO_ERROR, status, "no ACK generated");
    ASSERT_EQ(ts.outlen, sizeof(tftp_data_msg) + kBlockSize, "improper DATA packet size");
    msg = reinterpret_cast<tftp_data_msg*>(ts.out);
    EXPECT_EQ(OPCODE_DATA, ntohs(msg->opcode) & 0xff, "incorrect DATA packet opcode");
    EXPECT_EQ(0, (ntohs(msg->opcode) & 0xff00) >> 8, "incorrect opcode prefix");
    EXPECT_EQ((kAckBlock + 2) & 0xffff, ntohs(msg->block), "incorrect DATA packet block");

    END_TEST;
}

static ssize_t open_read_should_wait(const char* filename, void* cookie) {
    return TFTP_ERR_SHOULD_WAIT;
}

static tftp_status open_write_should_wait(const char* filename, size_t size, void* cookie) {
    return TFTP_ERR_SHOULD_WAIT;
}

/* Verify behavior when one of our open_file interface functions returns TFTP_ERR_SHOULD_WAIT. */
static bool test_tftp_open_should_wait(tftp_file_direction dir) {
    BEGIN_TEST;

    const uint16_t block_size = 456;
    const uint8_t timeout = 3;
    const uint16_t window_size = 128;

    test_state ts;
    ts.reset(1024, 1024, 1500);
    tftp_file_interface ifc = {open_read_should_wait, open_write_should_wait, NULL, NULL, NULL};
    tftp_session_set_file_interface(ts.session, &ifc);

    // Construct a RRQ or WRQ packet
    size_t req_file_size = (dir == SEND_FILE) ? 1024 : 0;
    char buf[256];
    buf[0] = 0x00;
    buf[1] = (dir == SEND_FILE) ? OPCODE_WRQ : OPCODE_RRQ;
    size_t buf_sz = 2;
    buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz,
                       "%s%cOCTET%cTSIZE%c%zu%cBLKSIZE%c%d%cTIMEOUT%c%d%cWINDOWSIZE%c%d",
                       kRemoteFilename, '\0',
                       '\0',
                       '\0', req_file_size, '\0',
                       '\0', block_size, '\0',
                       '\0', timeout, '\0',
                       '\0', window_size) + 1;
    ASSERT_LT(buf_sz, (int)sizeof(buf), "insufficient space for request");

    tftp_status status = tftp_process_msg(ts.session, buf, buf_sz, ts.out, &ts.outlen, &ts.timeout,
                                          nullptr);

    // Check API return value
    EXPECT_EQ(TFTP_ERR_SHOULD_WAIT, status, "expected SHOULD_WAIT status");

    // tftp_process_msg should have generated an error packet response - verify its fields
    ASSERT_GT(ts.outlen, 0);
    auto msg = reinterpret_cast<tftp_err_msg*>(ts.out);
    EXPECT_EQ(msg->opcode, htons(OPCODE_ERROR));
    EXPECT_EQ(msg->err_code, htons(TFTP_ERR_CODE_BUSY));
    if (dir == SEND_FILE) {
        const char expected_err[] = "not ready to receive";
        EXPECT_STR_EQ(expected_err, msg->msg, "bad error message");
    } else {
        const char expected_err[] = "not ready to send";
        EXPECT_STR_EQ(expected_err, msg->msg, "bad error message");
    }

    END_TEST;
}

static bool test_tftp_open_read_should_wait(void) {
   // RECV is from the perspective of the client, not the server
   return test_tftp_open_should_wait(RECV_FILE);
}

static bool test_tftp_open_write_should_wait(void) {
   // SEND is from the perspective of the client, not the server
   return test_tftp_open_should_wait(SEND_FILE);
}

static bool test_tftp_recv_busy(tftp_file_direction dir) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_request(ts.session, dir, kLocalFilename, kRemoteFilename,
                                        MODE_OCTET, dir == SEND_FILE ? ts.msg_size : 0,
                                        NULL, NULL, NULL, ts.out, &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating request");
    if (dir == SEND_FILE) {
        ASSERT_TRUE(verify_write_request(ts), "bad write request");
    } else {
        ASSERT_TRUE(verify_read_request(ts), "bad read request");
    }

    // Simulate a BUSY error response
    char buf[256];
    buf[0] = 0x00;
    buf[1] = OPCODE_ERROR;
    buf[2] = (TFTP_ERR_CODE_BUSY & 0xff00) >> 8;
    buf[3] = TFTP_ERR_CODE_BUSY & 0xff;
    size_t buf_sz = 4;
    buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz, "not ready") + 1;
    ASSERT_LT(buf_sz, (int)sizeof(buf), "insufficient space for request");

    status = tftp_process_msg(ts.session, buf, buf_sz, ts.out, &ts.outlen, &ts.timeout, nullptr);

    // Check API return value
    EXPECT_EQ(TFTP_ERR_SHOULD_WAIT, status, "expected SHOULD_WAIT status");

    // tftp_process_msg should not have generated a response
    EXPECT_EQ(ts.outlen, 0);

    // Verify session state
    EXPECT_EQ(NONE, ts.session->state, "bad session: state");

    END_TEST;
}

/* Verify handling of a BUSY error packet when we send a WRQ. */
static bool test_tftp_recv_busy_from_wrq(void) {
    return test_tftp_recv_busy(SEND_FILE);
}

/* Verify handling of a BUSY error packet when we send a RRQ. */
static bool test_tftp_recv_busy_from_rrq(void) {
    return test_tftp_recv_busy(RECV_FILE);
}

/* Verify that receiving an error other than BUSY puts the session into an error state. */
static bool test_tftp_recv_other_err(void) {
    BEGIN_TEST;

    test_state ts;
    ts.reset(1024, 1024, 1500);

    auto status = tftp_generate_request(ts.session, SEND_FILE, kLocalFilename, kRemoteFilename,
                                        MODE_OCTET, ts.msg_size, NULL, NULL, NULL, ts.out,
                                        &ts.outlen, &ts.timeout);
    ASSERT_EQ(TFTP_NO_ERROR, status, "error generating request");
    ASSERT_TRUE(verify_write_request(ts), "bad write request");

    // Simulate a BUSY error response
    char buf[256];
    buf[0] = 0x00;
    buf[1] = OPCODE_ERROR;
    buf[2] = (TFTP_ERR_CODE_DISK_FULL & 0xff00) >> 8;
    buf[3] = TFTP_ERR_CODE_DISK_FULL & 0xff;
    size_t buf_sz = 4;
    buf_sz += snprintf(&buf[buf_sz], sizeof(buf) - buf_sz, "disk full") + 1;
    ASSERT_LT(buf_sz, (int)sizeof(buf), "insufficient space for request");

    status = tftp_process_msg(ts.session, buf, buf_sz, ts.out, &ts.outlen, &ts.timeout, nullptr);

    // Check API return value
    EXPECT_EQ(TFTP_ERR_INTERNAL, status, "expected TFTP_ERR_INTERNAL status");

    // tftp_process_msg should not have generated a response
    EXPECT_EQ(ts.outlen, 0);

    // Verify session state
    EXPECT_EQ(ERROR, ts.session->state, "bad session: state");

    END_TEST;
}

BEGIN_TEST_CASE(tftp_setup)
RUN_TEST(test_tftp_init)
RUN_TEST(test_tftp_session_options)
END_TEST_CASE(tftp_setup)

BEGIN_TEST_CASE(tftp_generate_wrq)
RUN_TEST(test_tftp_generate_wrq_default)
RUN_TEST(test_tftp_generate_wrq_options)
RUN_TEST(test_tftp_generate_wrq_override_blocksize)
RUN_TEST(test_tftp_generate_wrq_override_timeout)
RUN_TEST(test_tftp_generate_wrq_override_windowsize)
END_TEST_CASE(tftp_generate_wrq)

BEGIN_TEST_CASE(tftp_generate_rrq)
RUN_TEST(test_tftp_generate_rrq_default)
RUN_TEST(test_tftp_generate_rrq_options)
RUN_TEST(test_tftp_generate_rrq_override_blocksize)
RUN_TEST(test_tftp_generate_rrq_override_timeout)
RUN_TEST(test_tftp_generate_rrq_override_windowsize)
END_TEST_CASE(tftp_generate_rrq)

BEGIN_TEST_CASE(tftp_receive_wrq)
RUN_TEST(test_tftp_receive_wrq_unexpected)
RUN_TEST(test_tftp_receive_wrq_too_large)
RUN_TEST(test_tftp_receive_wrq_no_tsize)
RUN_TEST(test_tftp_receive_wrq_send_oack)
RUN_TEST(test_tftp_receive_wrq_blocksize)
RUN_TEST(test_tftp_receive_wrq_timeout)
RUN_TEST(test_tftp_receive_wrq_windowsize)
RUN_TEST(test_tftp_receive_wrq_have_overrides)
RUN_TEST(test_tftp_receive_force_wrq_no_overrides)
RUN_TEST(test_tftp_receive_force_wrq_have_overrides)
END_TEST_CASE(tftp_receive_wrq)

BEGIN_TEST_CASE(tftp_receive_rrq)
RUN_TEST(test_tftp_receive_rrq_unexpected)
RUN_TEST(test_tftp_receive_rrq_too_large)
RUN_TEST(test_tftp_receive_rrq_no_tsize)
RUN_TEST(test_tftp_receive_rrq_send_oack)
RUN_TEST(test_tftp_receive_rrq_blocksize)
RUN_TEST(test_tftp_receive_rrq_timeout)
RUN_TEST(test_tftp_receive_rrq_windowsize)
RUN_TEST(test_tftp_receive_rrq_have_overrides)
RUN_TEST(test_tftp_receive_force_rrq_no_overrides)
RUN_TEST(test_tftp_receive_force_rrq_have_overrides)
END_TEST_CASE(tftp_receive_rrq)

BEGIN_TEST_CASE(tftp_receive_oack)
RUN_TEST(test_tftp_receive_wrq_oack)
RUN_TEST(test_tftp_receive_wrq_oack_blocksize)
RUN_TEST(test_tftp_receive_wrq_oack_timeout)
RUN_TEST(test_tftp_receive_wrq_oack_windowsize)
RUN_TEST(test_tftp_receive_rrq_oack)
RUN_TEST(test_tftp_receive_rrq_oack_blocksize)
RUN_TEST(test_tftp_receive_rrq_oack_timeout)
RUN_TEST(test_tftp_receive_rrq_oack_windowsize)
RUN_TEST(test_tftp_receive_oack_overrides)
END_TEST_CASE(tftp_receive_oack)

BEGIN_TEST_CASE(tftp_receive_data)
RUN_TEST(test_tftp_receive_data)
RUN_TEST(test_tftp_receive_data_final_block)
RUN_TEST(test_tftp_receive_data_blocksize)
RUN_TEST(test_tftp_receive_data_windowsize)
RUN_TEST(test_tftp_receive_data_skipped_block)
RUN_TEST(test_tftp_receive_data_windowsize_skipped_block)
RUN_TEST(test_tftp_receive_data_block_wrapping)
END_TEST_CASE(tftp_receive_data)

BEGIN_TEST_CASE(tftp_send_data)
RUN_TEST(test_tftp_send_data_receive_ack)
RUN_TEST(test_tftp_send_data_receive_final_ack)
RUN_TEST(test_tftp_send_data_receive_ack_skipped_block)
RUN_TEST(test_tftp_send_data_receive_ack_window_size)
RUN_TEST(test_tftp_send_data_receive_ack_block_wrapping)
RUN_TEST(test_tftp_send_data_receive_ack_skip_block_wrap)
END_TEST_CASE(tftp_send_data)

BEGIN_TEST_CASE(tftp_send_err)
RUN_TEST(test_tftp_open_read_should_wait)
RUN_TEST(test_tftp_open_write_should_wait)
END_TEST_CASE(tftp_send_err)

BEGIN_TEST_CASE(tftp_recv_err)
RUN_TEST(test_tftp_recv_busy_from_rrq)
RUN_TEST(test_tftp_recv_busy_from_wrq)
RUN_TEST(test_tftp_recv_other_err)
END_TEST_CASE(tftp_recv_err)

int main(int argc, char* argv[]) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
