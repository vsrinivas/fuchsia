// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tftp/tftp.h"

#define OPCODE_RRQ 1
#define OPCODE_WRQ 2
#define OPCODE_DATA 3
#define OPCODE_ACK 4
#define OPCODE_ERROR 5
#define OPCODE_OACK 6
#define OPCODE_OERROR 8

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tftp_msg_t {
    uint16_t opcode;
    char data[0];
} tftp_msg;

typedef struct tftp_data_msg_t {
    uint16_t opcode;
    uint16_t block;
    uint8_t data[0];
} tftp_data_msg;

#define BLOCKSIZE_OPTION 0x01  // RFC 2348
#define TIMEOUT_OPTION 0x02    // RFC 2349
#define FILESIZE_OPTION 0x04   // RFC 2349
#define WINDOWSIZE_OPTION 0x08 // RFC 7440

#define DEFAULT_BLOCKSIZE 512
#define DEFAULT_TIMEOUT 1
#define DEFAULT_FILESIZE 0
#define DEFAULT_WINDOWSIZE 1
#define DEFAULT_MODE MODE_OCTET
#define DEFAULT_MAX_TIMEOUTS 5

typedef struct tftp_options_t {
    // Maximum filename really is 505 including \0
    // max request size (512) - opcode (2) - shortest mode (4) - null (1)
    char filename[512];
    tftp_mode mode;
    uint8_t requested;

    uint16_t block_size;
    uint8_t timeout;
    uint32_t file_size;

    uint16_t window_size;

    // Maximum number of times we will retransmit a single msg before aborting
    uint16_t max_timeouts;
} tftp_options;

/**
 Sender
 NONE -(tftp_generate_write_request)-> SENT_WRQ
 SENT_WRQ -(tftp_process_msg = OPCODE_OACK)-> SENT_FIRST_PKT
 SENT_WRQ -(tftp_process_msg = OPCODE_ERROR)-> ERROR
 SENT_FIRST_PKT -(tftp_process_msg = OPCODE_ACK)-> SENT_DATA
 SENT_FIRST_PKT -(tftp_process_msg = OPCODE_ERROR)-> ERROR
 SENT_DATA -(tftp_process_msg = OPCODE_ACK)-> SENT_DATA
 SENT_DATA -(tftp_process_msg = OPCODE_ERROR)-> ERROR
 SENT_DATA -(OPCODE_ACK is last packet)-> COMPLETED
 COMPLETED -(tftp_process_msg)-> ERROR

 Receiver
 NONE -(tftp_process_msg = OPCODE_WRQ)-> RECV_WRQ
 NONE -(tftp_process_msg != OPCODE_WRQ)-> ERROR
 RECV_WRQ -(tftp_process_msg = OPCODE_DATA) -> RECV_DATA
 RECV_WRQ -(tftp_process_msg != OPCODE_DATA) -> ERROR
 RECV_DATA -(tftp_process_msg = OPCODE_DATA)-> RECV_DATA
 RECV_DATA -(tftp_process_msg != OPCODE_DATA)-> ERROR
 RECV_DATA -(last packet)-> COMPLETED
 COMPLETED -(tftp_process_msg)-> ERROR
**/

typedef enum {
    NONE = 0,
    SENT_WRQ,
    RECV_WRQ,
    SENT_FIRST_DATA,
    SENT_DATA,
    RECV_DATA,
    ERROR,
    COMPLETED,
} tftp_state;

struct tftp_session_t {
    tftp_options options;
    tftp_state state;
    size_t offset;

    uint32_t block_number;
    uint32_t window_index;

    uint32_t consecutive_timeouts;

    // "Negotiated" values
    size_t file_size;
    tftp_mode mode;
    uint16_t window_size;
    uint16_t block_size;
    uint8_t timeout;

    // Callbacks
    tftp_file_interface file_interface;
    tftp_transport_interface transport_interface;
};

// tftp_session_has_pending returns true if the tftp_session has more data to
// send before waiting for an ack. It is recommended that the caller do a
// non-blocking read to see if an out-of-order ACK was sent by the remote host
// before sending additional data packets.
bool tftp_session_has_pending(tftp_session* session);

// Generates a write request to send to a tftp server. |filename| is the name
// sent to the server. |datalen| is the size of the data to be sent.
// |block_size|, |timeout|, and |window_size| negotiate tftp options with the
// server. |outgoing| must point to a scratch buffer the library can
// use to assemble the request. |outlen| is the size of the outgoing scratch
// buffer, and will be set to the size of the request. |timeout_ms| is set to
// the next timeout value the user of the library should use when waiting for a
// response.
tftp_status tftp_generate_write_request(tftp_session* session,
                                        const char* filename,
                                        tftp_mode mode,
                                        size_t datalen,
                                        size_t block_size,
                                        uint8_t timeout,
                                        uint16_t window_size,
                                        void* outgoing,
                                        size_t* outlen,
                                        uint32_t* timeout_ms);

// Handle an incoming tftp packet. |incoming| must point to the packet of size
// |inlen|. |outgoing| must point to a scratch buffer the library can use to
// assemble the next packet to send. |outlen| is the size of the outgoing
// scratch buffer. |timeout_ms| is set to the next timeout value the user of the
// library should use when waiting for a response. |cookie| will be passed to
// the tftp callback functions.
tftp_status tftp_process_msg(tftp_session* session,
                             void* incoming,
                             size_t inlen,
                             void* outgoing,
                             size_t* outlen,
                             uint32_t* timeout_ms,
                             void* cookie);

// Prepare a DATA packet to send to the remote host. This is only required when
// tftp_session_has_pending(session) returns true, as tftp_process_msg() will
// prepare the first DATA message in each window.
tftp_status tftp_prepare_data(tftp_session* session,
                              void* outgoing,
                              size_t* outlen,
                              uint32_t* timeout_ms,
                              void* cookie);

// Internal handlers
tftp_status tx_data(tftp_session* session, tftp_data_msg* resp, size_t* outlen, void* cookie);
tftp_status tftp_handle_rrq(tftp_session* session,
                            tftp_msg* rrq,
                            size_t rrq_len,
                            tftp_msg* resp,
                            size_t* resp_len,
                            uint32_t* timeout_ms,
                            void* cookie);
tftp_status tftp_handle_wrq(tftp_session* session,
                            tftp_msg* wrq,
                            size_t wrq_len,
                            tftp_msg* resp,
                            size_t* resp_len,
                            uint32_t* timeout_ms,
                            void* cookie);
tftp_status tftp_handle_data(tftp_session* session,
                             tftp_msg* msg,
                             size_t msg_len,
                             tftp_msg* resp,
                             size_t* resp_len,
                             uint32_t* timeout_ms,
                             void* cookie);
tftp_status tftp_handle_ack(tftp_session* session,
                            tftp_msg* ack,
                            size_t ack_len,
                            tftp_msg* resp,
                            size_t* resp_len,
                            uint32_t* timeout_ms,
                            void* cookie);
tftp_status tftp_handle_error(tftp_session* session,
                              tftp_msg* err,
                              size_t err_len,
                              tftp_msg* resp,
                              size_t* resp_len,
                              uint32_t* timeout_ms,
                              void* cookie);
tftp_status tftp_handle_oack(tftp_session* session,
                             tftp_msg* oack,
                             size_t oack_len,
                             tftp_msg* resp,
                             size_t* resp_len,
                             uint32_t* timeout_ms,
                             void* cookie);
tftp_status tftp_handle_oerror(tftp_session* session,
                               tftp_msg* oerr,
                               size_t oerr_len,
                               tftp_msg* resp,
                               size_t* resp_len,
                               uint32_t* timeout_ms,
                               void* cookie);

void print_hex(uint8_t* buf, size_t len);

#ifdef __cplusplus
}  // extern "C"
#endif
