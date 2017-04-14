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

typedef struct tftp_options_t {
    // Maximum filename really is 505 including \0
    // max request size (512) - opcode (2) - shortest mode (4) - null (1)
    char filename[512];
    tftp_mode mode;
    uint8_t requested;

    uint16_t block_size;
    uint8_t timeout;
    uint32_t file_size;

    uint32_t window_size;
} tftp_options;

/**
TODO: update this after refactoring
 Sender
 NONE -(tftp_generate_write_request)-> WRITE_REQUESTED
 WRITE_REQUESTED -(tftp_handle_msg = OPCODE_OACK)-> TRANSMITTING
 WRITE_REQUESTED -(tftp_handle_msg = OPCODE_ACK)-> TRANSMITTING
 WRITE_REQUESTED -(tftp_handle_msg = OPCODE_ERROR)-> ERROR
 TRANSMITTING -(tftp_handle_msg = OPCODE_ACK)-> TRANSMITTING
 TRANSMITTING -(tftp_handle_msg = OPCODE_ERROR)-> ERROR
 TRANSMITTING -(last packet)-> LAST_PACKET
 LAST_PACKET -(tftp_handle_msg = OPCODE_ERROR)-> ERROR
 LAST_PACKET -(tftp_handle_msg = OPCODE_ACK last packet)-> COMPLETED
 LAST_PACKET -(tftp_handle_msg = OPCODE_ACK not last packet)-> TRANSMITTING
 COMPLETED -(tftp_handle_msg)-> ERROR

 Receiver
 NONE -(tftp_handle_msg = OPCODE_WRQ)-> WRITE_REQUESTED
 NONE -(tftp_handle_msg != OPCODE_WRQ)-> ERROR
 WRITE_REQUESTED -(tftp_handle_msg = OPCODE_DATA) -> TRANSMITTING
 WRITE_REQUESTED -(tftp_handle_msg != OPCODE_DATA) -> ERROR
 TRANSMITTING -(tftp_handle_msg = OPCODE_DATA)-> TRANSMITTING
 TRANSMITTING -(tftp_handle_msg != OPCODE_DATA)-> ERROR
 TRANSMITTING -(last packet)-> COMPLETED
 COMPLETED -(tftp_handle_msg)-> ERROR
**/

typedef enum {
    NONE = 0,
    WRITE_REQUESTED,
    TRANSMITTING,
    LAST_PACKET,
    ERROR,
    COMPLETED,
} tftp_state;

// TODO add a state so time out can be handled properly as well as unexpected traffic
struct tftp_session_t {
    tftp_options options;
    tftp_state state;
    size_t offset;

    uint32_t block_number;
    uint32_t window_index;

    // "Negotiated" values
    size_t file_size;
    tftp_mode mode;
    uint32_t window_size;
    uint16_t block_size;
    uint8_t timeout;

    // Callbacks
    tftp_open_file open_fn;
    tftp_read read_fn;
    tftp_write write_fn;
};

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
