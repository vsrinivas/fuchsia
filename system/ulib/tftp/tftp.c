// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L  // for strnlen
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

// TODO: update this
// RRQ    ->
//        <- DATA or OACK or ERROR
// ACK(0) -> (to confirm reception of OACK)
// ERROR  -> (on OACK with non requested options)
//        <- DATA(1)
// ACK(1) ->

// WRQ     ->
//         <- ACK or OACK or ERROR
// DATA(1) ->
// ERROR   -> (on OACK with non requested options)
//        <- DATA(2)
// ACK(2) ->

// MODE
static const char* kNetascii = "NETASCII";
static const char* kOctet = "OCTET";
static const char* kMail = "MAIL";
static const size_t kMaxMode = 9;  // strlen(NETASCII) + 1

// TSIZE
// Limit transfer to less than 10GB
static const char* kTsize = "TSIZE";
static const size_t kMaxTsizeOpt = 17;  // strlen(TSIZE) + 1 + strlen(1000000000) + 1

// BLKSIZE
// Max size is 65535 (max IP datagram)
static const char* kBlkSize = "BLKSIZE";
static const size_t kMaxBlkSizeOpt = 14;  // strlen(BLKSIZE) + 1 + strlen(65535) + 1

// TIMEOUT
// Max is 255 (RFC 2349)
static const char* kTimeout = "TIMEOUT";
static const size_t kMaxTimeoutOpt = 12;  // strlen(TIMEOUT) + 1 + strlen(255) + 1

// WINDOWSIZE
// Max is 65535 (RFC 7440)
static const char* kWindowSize = "WINDOWSIZE";
static const size_t kMaxWindowSizeOpt = 17;  // strlen(WINDOWSIZE) + 1 + strlen(65535) + 1

// Since RRQ and WRQ come before option negotation, they are limited to max TFTP
// blocksize of 512 (RFC 1350 and 2347).
static const size_t kMaxRequestSize = 512;

#define DEBUG 0

#if DEBUG
# define xprintf(args...) fprintf(stderr, args)
#else
# define xprintf(args...)
#endif

#define __ATTR_PRINTF(__fmt, __varargs) \
    __attribute__((__format__(__printf__, __fmt, __varargs)))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

static void append_option_name(char** body, size_t* left, const char* name) {
    size_t offset = strlen(name);
    memcpy(*body, name, offset);
    offset++;
    *body += offset;
    *left -= offset;
}

static void __ATTR_PRINTF(4, 5) append_option(char** body, size_t* left, const char* name,
        const char* fmt, ...) {
    char* bodyp = *body;
    size_t leftp = *left;

    size_t offset = strlen(name);
    memcpy(bodyp, name, offset);
    offset++;
    bodyp += offset;
    leftp -= offset;
    va_list args;
    va_start(args, fmt);
    offset = vsnprintf(bodyp, leftp - 1, fmt, args);
    va_end(args);
    offset++;
    bodyp += offset;
    leftp -= offset;

    *body = bodyp;
    *left = leftp;
}

#define OPCODE(msg, value)                \
    do {                                  \
        (msg)->opcode = htons(value);     \
    } while (0)

#define TRANSMIT_MORE 1
#define TRANSMIT_WAIT_ON_ACK 2

static size_t next_option(char* buffer, size_t len, char** option, char** value) {
    size_t left = len;
    size_t option_len = strnlen(buffer, left);
    if (option_len == len) {
        return 0;
    }

    *option = buffer;
    xprintf("'%s' %ld\n", *option, option_len);
    buffer += option_len + 1;
    left -= option_len + 1;
    size_t value_len = strnlen(buffer, left);
    if (value_len == left) {
        return 0;
    }
    *value = buffer;
    xprintf("'%s' %ld\n", *value, value_len);
    left -= value_len + 1;
    return len - left;
}

static void set_error(tftp_session* session, uint16_t opcode, tftp_msg* resp, size_t* resp_len) {
    OPCODE(resp, opcode);
    *resp_len = sizeof(*resp);
    session->state = ERROR;
}

tftp_status tx_data(tftp_session* session, tftp_data_msg* resp, size_t* outlen, void* cookie) {
    session->offset = (session->block_number + session->window_index) * session->block_size;
    *outlen = 0;
    if (session->offset < session->file_size) {
        session->window_index++;
        OPCODE(resp, OPCODE_DATA);
        resp->block = session->block_number + session->window_index;
        size_t len = MIN(session->file_size - session->offset, session->block_size);
        xprintf(" -> Copying block #%d (size:%zu/%d) from %zu/%zu [%d/%d]\n",
                session->block_number + session->window_index, len, session->block_size,
                session->offset, session->file_size, session->window_index, session->window_size);
        // TODO(tkilbourn): assert that these function pointers are set
        tftp_status s = session->file_interface.read(resp->data, &len, session->offset, cookie);
        if (s < 0) {
            xprintf("Err reading: %d\n", s);
            return s;
        }
        *outlen = sizeof(*resp) + len;

        if (session->window_index < session->window_size) {
            xprintf(" -> TRANSMIT_MORE(%d < %d)\n", session->window_index, session->window_size);
        } else {
            xprintf(" -> TRANSMIT_WAIT_ON_ACK(%d >= %d)\n", session->window_index,
                    session->window_size);
            session->block_number += session->window_size;
            session->window_index = 0;
        }
    } else {
        xprintf(" -> TRANSMIT_WAIT_ON_ACK(completed)\n");
    }
    return TFTP_NO_ERROR;
}

size_t tftp_sizeof_session(void) {
    return sizeof(tftp_session);
}

int tftp_init(tftp_session** session, void* buffer, size_t size) {
    if (buffer == NULL) {
        return TFTP_ERR_INVALID_ARGS;
    }
    if (size < sizeof(tftp_session)) {
        return TFTP_ERR_BUFFER_TOO_SMALL;
    }
    *session = buffer;
    tftp_session* s = *session;
    memset(s, 0, sizeof(tftp_session));

    // Sensible default values
    s->file_size = s->options.file_size = DEFAULT_FILESIZE;
    s->window_size = s->options.window_size = DEFAULT_WINDOWSIZE;
    s->block_size = s->options.block_size = DEFAULT_BLOCKSIZE;
    s->timeout = s->options.timeout = DEFAULT_TIMEOUT;
    s->mode = s->options.mode = DEFAULT_MODE;

    return TFTP_NO_ERROR;
}

tftp_status tftp_session_set_file_interface(tftp_session* session,
                                            tftp_file_interface* callbacks) {
    if (session == NULL) {
        return TFTP_ERR_INVALID_ARGS;
    }

    session->file_interface = *callbacks;
    return TFTP_NO_ERROR;
}

tftp_status tftp_session_set_transport_interface(tftp_session* session,
                                                 tftp_transport_interface* callbacks) {
    if (session == NULL) {
        return TFTP_ERR_INVALID_ARGS;
    }
    session->transport_interface = *callbacks;
    return TFTP_NO_ERROR;
}

bool tftp_session_has_pending(tftp_session* session) {
    return session->window_index > 0 && session->window_index < session->window_size;
}

tftp_status tftp_generate_write_request(tftp_session* session,
                                        const char* filename,
                                        tftp_mode mode,
                                        size_t datalen,
                                        size_t block_size,
                                        uint8_t timeout,
                                        uint16_t window_size,
                                        void* outgoing,
                                        size_t* outlen,
                                        uint32_t* timeout_ms) {
    if (*outlen < 2) {
        xprintf("outlen too short: %zd\n", *outlen);
        return TFTP_ERR_BUFFER_TOO_SMALL;
    }

    tftp_msg* ack = outgoing;
    OPCODE(ack, OPCODE_WRQ);
    char* body = ack->data;
    memset(body, 0, *outlen - sizeof(*ack));
    size_t left = *outlen - sizeof(*ack);
    if (strlen(filename) + 1 > left - kMaxMode) {
        xprintf("filename too long %zd > %zd\n", strlen(filename), left - kMaxMode);
        return TFTP_ERR_INVALID_ARGS;
    }
    strncpy(session->options.filename, filename, sizeof(session->options.filename));
    memcpy(body, filename, strlen(filename));
    body += strlen(filename) + 1;
    left -= strlen(filename) + 1;
    switch (mode) {
    case MODE_NETASCII:
        append_option_name(&body, &left, kNetascii);
        break;
    case MODE_OCTET:
        append_option_name(&body, &left, kOctet);
        break;
    case MODE_MAIL:
        append_option_name(&body, &left, kMail);
        break;
    default:
        return TFTP_ERR_INVALID_ARGS;
    }
    session->options.mode = mode;

    if (left < kMaxTsizeOpt) {
        return TFTP_ERR_BUFFER_TOO_SMALL;
    }
    append_option(&body, &left, kTsize, "%zu", datalen);
    session->file_size = datalen;

    if (block_size > 0) {
        if (left < kMaxBlkSizeOpt) {
            return TFTP_ERR_BUFFER_TOO_SMALL;
        }
        append_option(&body, &left, kBlkSize, "%zu", block_size);
        session->options.block_size = block_size;
        session->options.requested |= BLOCKSIZE_OPTION;
    }

    if (timeout > 0) {
        if (left < kMaxTimeoutOpt) {
            return TFTP_ERR_BUFFER_TOO_SMALL;
        }
        append_option(&body, &left, kTimeout, "%d", timeout);
        session->options.timeout = timeout;
        session->options.requested |= TIMEOUT_OPTION;
    }

    if (window_size > 1) {
        if (left < kMaxWindowSizeOpt) {
            return TFTP_ERR_BUFFER_TOO_SMALL;
        }
        append_option(&body, &left, kWindowSize, "%d", window_size);
        session->options.window_size = window_size;
        session->options.requested |= WINDOWSIZE_OPTION;
    }

    *outlen = *outlen - left;
    // Nothing has been negotiated yet so use default
    *timeout_ms = 1000 * session->timeout;

    session->state = WRITE_REQUESTED;
    xprintf("Generated write request, len=%zu\n", *outlen);
    return TFTP_NO_ERROR;
}

tftp_status tftp_handle_rrq(tftp_session* session,
                            tftp_msg* rrq,
                            size_t rrq_len,
                            tftp_msg* resp,
                            size_t* resp_len,
                            uint32_t* timeout_ms,
                            void* cookie) {
    // TODO(tkilbourn): implement this after refactoring tftp_handle_wrq into
    // some more common methods for option handling.
    return TFTP_ERR_NOT_SUPPORTED;
}

tftp_status tftp_handle_wrq(tftp_session* session,
                            tftp_msg* wrq,
                            size_t wrq_len,
                            tftp_msg* resp,
                            size_t* resp_len,
                            uint32_t* timeout_ms,
                            void* cookie) {
    if (session->state != NONE) {
        xprintf("Invalid state transition %d -> %d\n", session->state, WRITE_REQUESTED);
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_BAD_STATE;
    }
    // opcode, filename, 0, mode, 0, opt1, 0, value1 ... optN, 0, valueN, 0
    // Max length is 512 no matter
    if (wrq_len > kMaxRequestSize) {
        xprintf("Write request is too large\n");
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }
    // Skip opcode
    size_t left = wrq_len - sizeof(*resp);
    char* cur = wrq->data;
    char *option, *value;
    // filename, 0, mode, 0 can be interpreted like option, 0, value, 0
    size_t offset = next_option(cur, left, &option, &value);
    if (!offset) {
        xprintf("No options\n");
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }
    left -= offset;

    xprintf("filename = '%s', mode = '%s'\n", option, value);

    strncpy(session->options.filename, option, sizeof(session->options.filename));
    char* mode = value;
    if (!strncmp(mode, kNetascii, strlen(kNetascii))) {
        session->options.mode = MODE_NETASCII;
    } else if (!strncmp(mode, kOctet, strlen(kOctet))) {
        session->options.mode = MODE_OCTET;
    } else if (!strncmp(mode, kMail, strlen(kMail))) {
        session->options.mode = MODE_MAIL;
    } else {
        xprintf("Unknown write request mode\n");
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }

    // TODO(tkilbourn): refactor option handling code to share with
    // tftp_handle_oack
    cur += offset;
    while (offset > 0 && left > 0) {
        offset = next_option(cur, left, &option, &value);
        if (!offset) {
            xprintf("No more options\n");
            set_error(session, OPCODE_ERROR, resp, resp_len);
            return TFTP_ERR_INTERNAL;
        }

        if (!strncmp(option, kBlkSize, strlen(kBlkSize))) { // RFC 2348
            // Valid values range between "8" and "65464" octets, inclusive
            long val = atol(value);
            if (val < 8 || val > 65464) {
                xprintf("invalid block size\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            // TODO(tkilbourn): with an MTU of 1500, shouldn't be more than 1428
            session->options.block_size = val;
            session->options.requested |= BLOCKSIZE_OPTION;
        } else if (!strncmp(option, kTimeout, strlen(kTimeout))) { // RFC 2349
            // Valid values range between "1" and "255" seconds inclusive.
            long val = atol(value);
            if (val < 1 || val > 255) {
                xprintf("invalid timeout\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            session->options.timeout = val;
            session->options.requested |= TIMEOUT_OPTION;
        } else if (!strncmp(option, kTsize, strlen(kTsize))) { // RFC 2349
            long val = atol(value);
            if (val < 1) {
                xprintf("invalid file size\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            session->options.file_size = val;
            session->options.requested |= FILESIZE_OPTION;
        } else if (!strncmp(option, kWindowSize, strlen(kWindowSize))) { // RFC 7440
            // The valid values range MUST be between 1 and 65535 blocks, inclusive.
            long val = atol(value);
            if (val < 1 || val > 65535) {
                xprintf("invalid window size\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            session->options.window_size = val;
            session->options.requested |= WINDOWSIZE_OPTION;
        } else {
            // Options which the server does not support should be omitted from the
            // OACK; they should not cause an ERROR packet to be generated.
        }

        cur += offset;
        left -= offset;
    }

    char* body = resp->data;
    memset(body, 0, *resp_len - sizeof(*resp));
    left = *resp_len - sizeof(*resp);

    OPCODE(resp, OPCODE_OACK);
    if (session->options.requested & FILESIZE_OPTION) {
        append_option(&body, &left, kTsize, "%d", session->options.file_size);
        session->file_size = session->options.file_size;
    } else {
        xprintf("No TSIZE option specified\n");
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_BAD_STATE;
    }
    if (session->options.requested & BLOCKSIZE_OPTION) {
        // TODO(jpoichet) Make sure this block size is possible. Need API upwards to
        // request allocation of block size * window size memory
        append_option(&body, &left, kBlkSize, "%d", session->options.block_size);
        session->block_size = session->options.block_size;
    }
    if (session->options.requested & TIMEOUT_OPTION) {
        // TODO(jpoichet) Make sure this timeout is possible. Need API upwards to
        // request allocation of block size * window size memory
        append_option(&body, &left, kTimeout, "%d", session->options.timeout);
        session->timeout = session->options.timeout;
        *timeout_ms = 1000 * session->timeout;
    }
    if (session->options.requested & WINDOWSIZE_OPTION) {
        append_option(&body, &left, kWindowSize, "%d", session->options.window_size);
        session->window_size = session->options.window_size;
    }
    if (!session->file_interface.open_write ||
            session->file_interface.open_write(session->options.filename, session->options.file_size, cookie)) {
        xprintf("Could not open file on write request\n");
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_BAD_STATE;
    }
    *resp_len = *resp_len - left;
    session->state = WRITE_REQUESTED;

    xprintf("Read/Write Request Parsed\n");
    xprintf("Options requested: %08x\n", session->options.requested);
    xprintf("    Block Size : %d\n", session->options.block_size);
    xprintf("    Timeout    : %d\n", session->options.timeout);
    xprintf("    File Size  : %d\n", session->options.file_size);
    xprintf("    Window Size: %d\n", session->options.window_size);

    xprintf("Using options\n");
    xprintf("    Block Size : %d\n", session->block_size);
    xprintf("    Timeout    : %d\n", session->timeout);
    xprintf("    File Size  : %zu\n", session->file_size);
    xprintf("    Window Size: %d\n", session->window_size);

    return TFTP_NO_ERROR;
}

tftp_status tftp_handle_data(tftp_session* session,
                             tftp_msg* msg,
                             size_t msg_len,
                             tftp_msg* resp,
                             size_t* resp_len,
                             uint32_t* timeout_ms,
                             void* cookie) {
    switch (session->state) {
    case WRITE_REQUESTED:
    case TRANSMITTING:
        session->state = TRANSMITTING;
        break;
    case NONE:
    case LAST_PACKET:
    case ERROR:
    case COMPLETED:
    default:
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }

    tftp_data_msg* data = (tftp_data_msg*)msg;
    tftp_data_msg* ack_data = (tftp_data_msg*)resp;
    xprintf(" <- Block %u (Last = %u, Offset = %d, Size = %ld, Left = %ld)\n", data->block,
            session->block_number, session->block_number * session->block_size,
            session->file_size, session->file_size - session->block_number * session->block_size);
    // The block field of the message is only 16 bits wide. To support large files
    // (> 65535 * blocksize bytes), we allow the block number to wrap. We use signed modulo
    // math to determine the relative location of the block to our current position.
    int16_t block_delta = data->block - (uint16_t)session->block_number;
    if (block_delta == 1) {
        xprintf("Advancing normally + 1\n");
        size_t wr = msg_len - sizeof(tftp_data_msg);
        // TODO(tkilbourn): assert that these function pointers are set
        tftp_status ret = session->file_interface.write(data->data, &wr,
                session->block_number * session->block_size, cookie);
        if (ret < 0) {
            xprintf("Error writing: %d\n", ret);
            return ret;
        }
        session->block_number++;
        session->window_index++;
    } else {
        // Force sending a ACK with the last block_number we received
        xprintf("Skipped: got %d, expected %d\n", session->block_number + block_delta,
                session->block_number + 1);
        session->window_index = session->window_size;
    }

    if (session->window_index == session->window_size ||
            session->block_number * session->block_size >= session->file_size) {
        xprintf(" -> Ack %d\n", session->block_number);
        session->window_index = 0;
        OPCODE(ack_data, OPCODE_ACK);
        ack_data->block = session->block_number & 0xffff;
        *resp_len = sizeof(*ack_data);
        if (session->block_number * session->block_size >= session->file_size) {
            return TFTP_TRANSFER_COMPLETED;
        }
    } else {
        // Nothing to send
        *resp_len = 0;
    }
    return TFTP_NO_ERROR;
}

tftp_status tftp_handle_ack(tftp_session* session,
                            tftp_msg* ack,
                            size_t ack_len,
                            tftp_msg* resp,
                            size_t* resp_len,
                            uint32_t* timeout_ms,
                            void* cookie) {
    switch (session->state) {
    case WRITE_REQUESTED:
    case TRANSMITTING:
        session->state = TRANSMITTING;
        break;
    case NONE:
    case LAST_PACKET:
    case ERROR:
    case COMPLETED:
    default:
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }
    // Need to move forward in data and send it
    tftp_data_msg* ack_data = (void*)ack;
    tftp_data_msg* resp_data = (void*)resp;

    xprintf(" <- Ack %d\n", ack_data->block);

    // Since we track blocks in 32 bits, but the packets only support 16 bits, calculate the
    // signed 16 bit offset to determine the adjustment to the current position.
    int16_t block_offset = ack_data->block - (uint16_t)session->block_number;
    session->block_number += block_offset;
    session->window_index = 0;

    if (((session->block_number + session->window_index) * session->block_size) >=
        session->file_size) {
        *resp_len = 0;
        return TFTP_TRANSFER_COMPLETED;
    }

    tftp_status ret = tx_data(session, resp_data, resp_len, cookie);
    if (ret < 0) {
        set_error(session, OPCODE_ERROR, resp, resp_len);
    }
    return ret;
}

tftp_status tftp_handle_error(tftp_session* session,
                              tftp_msg* err,
                              size_t err_len,
                              tftp_msg* resp,
                              size_t* resp_len,
                              uint32_t* timeout_ms,
                              void* cookie) {
    xprintf("Transfer Error\n");
    session->state = ERROR;
    return TFTP_ERR_INTERNAL;
}

tftp_status tftp_handle_oack(tftp_session* session,
                             tftp_msg* oack,
                             size_t oack_len,
                             tftp_msg* resp,
                             size_t* resp_len,
                             uint32_t* timeout_ms,
                             void* cookie) {
    xprintf("Option Ack\n");
    switch (session->state) {
    case WRITE_REQUESTED:
        session->state = TRANSMITTING;
        break;
    case TRANSMITTING:
    case NONE:
    case LAST_PACKET:
    case ERROR:
    case COMPLETED:
    default:
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }

    size_t left = oack_len - sizeof(*oack);
    char* cur = oack->data;
    size_t offset;
    char *option, *value;

    session->mode = session->options.mode;
    if (session->options.requested & BLOCKSIZE_OPTION) {
        session->block_size = session->options.block_size;
    }
    if (session->options.requested & TIMEOUT_OPTION) {
        session->timeout = session->options.timeout;
    }
    if (session->options.requested & WINDOWSIZE_OPTION) {
        session->window_size = session->options.window_size;
    }
    while (left > 0) {
        offset = next_option(cur, left, &option, &value);
        if (!offset) {
            set_error(session, OPCODE_ERROR, resp, resp_len);
            return TFTP_ERR_INTERNAL;
        }

        if (!strncmp(option, kBlkSize, strlen(kBlkSize))) { // RFC 2348
            if (!(session->options.requested & BLOCKSIZE_OPTION)) {
                xprintf("block size not requested\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            // Valid values range between "8" and "65464" octets, inclusive
            long val = atol(value);
            if (val < 8 || val > 65464) {
                xprintf("invalid block size\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            // TODO(tkilbourn): with an MTU of 1500, shouldn't be more than 1428
            session->block_size = val;
        } else if (!strncmp(option, kTimeout, strlen(kTimeout))) { // RFC 2349
            if (!(session->options.requested & TIMEOUT_OPTION)) {
                xprintf("timeout not requested\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            // Valid values range between "1" and "255" seconds inclusive.
            long val = atol(value);
            if (val < 1 || val > 255) {
                xprintf("invalid timeout\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            session->timeout = val;
        } else if (!strncmp(option, kWindowSize, strlen(kWindowSize))) { // RFC 7440
            if (!(session->options.requested & WINDOWSIZE_OPTION)) {
                xprintf("window size not requested\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            // The valid values range MUST be between 1 and 65535 blocks, inclusive.
            long val = atol(value);
            if (val < 1 || val > 65535) {
                xprintf("invalid window size\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            session->window_size = val;
        } else {
            // Options which the server does not support should be omitted from the
            // OACK; they should not cause an ERROR packet to be generated.
        }

        cur += offset;
        left -= offset;
    }
    *timeout_ms = 1000 * session->timeout;

    xprintf("Options negotiated\n");
    xprintf("    Block Size : %d\n", session->block_size);
    xprintf("    Timeout    : %d\n", session->timeout);
    xprintf("    File Size  : %zu\n", session->file_size);
    xprintf("    Window Size: %d\n", session->window_size);

    tftp_data_msg* resp_data = (void*)resp;
    session->offset = 0;
    session->block_number = 0;

    tftp_status ret = tx_data(session, resp_data, resp_len, cookie);
    if (ret < 0) {
        set_error(session, OPCODE_ERROR, resp, resp_len);
    }
    return ret;
}

tftp_status tftp_handle_oerror(tftp_session* session,
                               tftp_msg* oerr,
                               size_t oerr_len,
                               tftp_msg* resp,
                               size_t* resp_len,
                               uint32_t* timeout_ms,
                               void* cookie) {
    xprintf("Option Error\n");
    session->state = ERROR;
    return TFTP_ERR_INTERNAL;
}

tftp_status tftp_process_msg(tftp_session* session,
                             void* incoming,
                             size_t inlen,
                             void* outgoing,
                             size_t* outlen,
                             uint32_t* timeout_ms,
                             void* cookie) {
    tftp_msg* msg = incoming;
    tftp_msg* resp = outgoing;

    // Decode opcode
    uint16_t opcode = ntohs(msg->opcode);
    xprintf("handle_msg opcode=%u\n", opcode);

    // Set default timeout
    *timeout_ms = 1000 * session->timeout;

    switch (opcode) {
    case OPCODE_RRQ:
        return tftp_handle_rrq(session, msg, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_WRQ:
        return tftp_handle_wrq(session, msg, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_DATA:
        return tftp_handle_data(session, msg, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_ACK:
        return tftp_handle_ack(session, msg, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_ERROR:
        return tftp_handle_error(session, msg, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_OACK:
        return tftp_handle_oack(session, msg, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_OERROR:
        return tftp_handle_oerror(session, msg, inlen, resp, outlen, timeout_ms, cookie);
    default:
        xprintf("Unknown opcode\n");
        session->state = ERROR;
        return TFTP_ERR_INTERNAL;
    }
}

tftp_status tftp_prepare_data(tftp_session* session,
                              void* outgoing,
                              size_t* outlen,
                              uint32_t* timeout_ms,
                              void* cookie) {
    tftp_data_msg* resp_data = outgoing;

    if ((session->block_number + session->window_index) * session->block_size >= session->file_size) {
        *outlen = 0;
        return TFTP_TRANSFER_COMPLETED;
    }

    tftp_status ret = tx_data(session, resp_data, outlen, cookie);
    if (ret < 0) {
        set_error(session, OPCODE_ERROR, outgoing, outlen);
    }
    return ret;
}

tftp_status tftp_timeout(tftp_session* session,
                         void* outgoing,
                         size_t* outlen,
                         uint32_t* timeout_ms,
                         void* cookie) {
    // TODO: really handle timeouts
    return TFTP_NO_ERROR;
}

#define REPORT_ERR(opts,...)                                     \
    if (opts->err_msg) {                                         \
        snprintf(opts->err_msg, opts->err_msg_sz, __VA_ARGS__);  \
    }

tftp_status tftp_push_file(tftp_session* session,
                           void* transport_cookie,
                           void* file_cookie,
                           const char* local_filename,
                           const char* remote_filename,
                           tftp_request_opts* opts) {
    if (!opts || !opts->inbuf || !opts->outbuf) {
        return TFTP_ERR_INVALID_ARGS;
    }
    ssize_t file_size;
    file_size = session->file_interface.open_read(local_filename, file_cookie);
    if (file_size < 0) {
        REPORT_ERR(opts, "failed during file open callback");
        return file_size;
    }

    size_t in_buf_sz = opts->inbuf_sz;
    void* incoming = (void*)opts->inbuf;
    size_t out_buf_sz = opts->outbuf_sz;
    void* outgoing = (void*)opts->outbuf;
    uint8_t timeout = opts->timeout ? *opts->timeout : TFTP_DEFAULT_CLIENT_TIMEOUT;
    tftp_mode mode = opts->mode ? *opts->mode : TFTP_DEFAULT_CLIENT_MODE;
    uint16_t block_size = opts->block_size ? *opts->block_size : TFTP_DEFAULT_CLIENT_BLOCKSZ;
    uint32_t window_size = opts->window_size ? *opts->window_size : TFTP_DEFAULT_CLIENT_WINSZ;

    size_t out_sz = out_buf_sz;
    uint32_t timeout_ms;
    tftp_status s =
        tftp_generate_write_request(session,
                                    remote_filename,
                                    mode,
                                    file_size,
                                    block_size,
                                    timeout,
                                    window_size,
                                    outgoing,
                                    &out_sz,
                                    &timeout_ms);
    if (s < 0) {
        REPORT_ERR(opts, "failed to generate write request");
        return s;
    }
    if (!out_sz) {
        REPORT_ERR(opts, "no write request generated");
        return TFTP_ERR_INTERNAL;
    }

    int n = session->transport_interface.send(outgoing, out_sz, transport_cookie);
    if (n < 0) {
        REPORT_ERR(opts, "failed during transport send callback");
        return (tftp_status)n;
    }

    int ret;
    bool pending = false;
    do {
        ret = session->transport_interface.timeout_set(timeout_ms, transport_cookie);
        if (ret < 0) {
            REPORT_ERR(opts, "failed during transport timeout set callback");
            return ret;
        }

        n = session->transport_interface.recv(incoming, in_buf_sz, !pending, transport_cookie);
        if (n < 0) {
            if (pending && (n == TFTP_ERR_TIMED_OUT)) {
                out_sz = out_buf_sz;
                ret = tftp_prepare_data(session,
                                        outgoing,
                                        &out_sz,
                                        &timeout_ms,
                                        file_cookie);
                if (out_sz) {
                    n = session->transport_interface.send(outgoing, out_sz, transport_cookie);
                    if (n < 0) {
                        REPORT_ERR(opts, "failed during transport send callback");
                        return n;
                    }
                }
                if (ret < 0) {
                    REPORT_ERR(opts, "failed to prepare data to send");
                    return ret;
                }
                if (!tftp_session_has_pending(session)) {
                    pending = false;
                }
                continue;
            }
            if (n == TFTP_ERR_TIMED_OUT) {
                ret = tftp_timeout(session,
                                   outgoing,
                                   &out_sz,
                                   &timeout_ms,
                                   transport_cookie);
                if (out_sz) {
                    n = session->transport_interface.send(outgoing, out_sz, transport_cookie);
                    if (n < 0) {
                        REPORT_ERR(opts, "failed during transport send callback");
                        return n;
                    }
                }
                if (ret < 0) {
                    REPORT_ERR(opts, "failed during timeout processing");
                    return ret;
                }
                continue;
            }
            REPORT_ERR(opts, "failed during transport recv callback");
            return n;
        }

        out_sz = out_buf_sz;
        ret = tftp_process_msg(session,
                               incoming,
                               n,
                               outgoing,
                               &out_sz,
                               &timeout_ms,
                               file_cookie);
        if (out_sz) {
            n = session->transport_interface.send(outgoing, out_sz, transport_cookie);
            if (n < 0) {
                REPORT_ERR(opts, "failed during transport send callback");
                return n;
            }
        }
        if (ret < 0) {
            REPORT_ERR(opts, "failed to parse request");
            return ret;
        } else if (ret == TFTP_TRANSFER_COMPLETED) {
            break;
        }
        if (tftp_session_has_pending(session)) {
            pending = true;
        } else {
            pending = false;
        }
    } while (1);

    return TFTP_NO_ERROR;
}

tftp_status tftp_handle_request(tftp_session* session,
                                void* transport_cookie,
                                void* file_cookie,
                                tftp_handler_opts* opts) {
    if (!opts || !opts->inbuf || !opts->outbuf) {
        return TFTP_ERR_INVALID_ARGS;
    }
    size_t in_buf_sz = opts->inbuf_sz;
    void* incoming = (void*)opts->inbuf;
    size_t out_buf_sz = opts->outbuf_sz;
    void* outgoing = (void*)opts->outbuf;

    int n, ret;
    bool transfer_in_progress = false;
    do {
        size_t in_sz = in_buf_sz;
        n = session->transport_interface.recv(incoming, in_sz, true, transport_cookie);
        if (n < 0) {
            if (n == TFTP_ERR_TIMED_OUT) {
                if (transfer_in_progress) {
                    uint32_t timeout_ms;
                    size_t out_sz = out_buf_sz;
                    ret = tftp_timeout(session,
                                       outgoing,
                                       &out_sz,
                                       &timeout_ms,
                                       file_cookie);
                    if (out_sz) {
                        n = session->transport_interface.send(outgoing, out_sz, transport_cookie);
                        if (n < 0) {
                            REPORT_ERR(opts, "failed during transport send callback");
                            return (tftp_status)n;
                        }
                    }
                    if (ret < 0) {
                        REPORT_ERR(opts, "failed during timeout processing");
                        return ret;
                    }
                }
                continue;
            }
            REPORT_ERR(opts, "failed during transport recv callback");
            return n;
        } else {
            in_sz = n;
            transfer_in_progress = true;
        }

        tftp_handler_opts send_opts;
        send_opts = *opts;
        send_opts.inbuf_sz = in_sz;
        tftp_status status = tftp_handle_msg(session, transport_cookie,
                                             file_cookie, &send_opts);
        if (status != TFTP_NO_ERROR) {
            return status;
        }
    } while (1);

    return TFTP_NO_ERROR;
}

tftp_status tftp_handle_msg(tftp_session* session,
                            void* transport_cookie,
                            void* file_cookie,
                            tftp_handler_opts* opts) {
    if (!opts || !opts->inbuf || !opts->outbuf) {
        return TFTP_ERR_INVALID_ARGS;
    }
    uint32_t timeout_ms;
    tftp_status ret;
    size_t out_sz = opts->outbuf_sz;
    ret = tftp_process_msg(session, opts->inbuf, opts->inbuf_sz,
                           opts->outbuf, &out_sz, &timeout_ms, file_cookie);
    if (out_sz) {
        int n = session->transport_interface.send(opts->outbuf, out_sz, transport_cookie);
        if (n < 0) {
            REPORT_ERR(opts, "failed during transport send callback");
            return n;
        }
    }
    if (ret < 0) {
        REPORT_ERR(opts, "failed to parse request");
    } else if (ret == TFTP_TRANSFER_COMPLETED) {
        session->file_interface.close(file_cookie);
    } else {
        ret = session->transport_interface.timeout_set(timeout_ms, transport_cookie);
        if (ret < 0) {
            REPORT_ERR(opts, "failed during transport timeout set callback");
        }
    }
    return ret;
}

