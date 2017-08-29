// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L  // for strnlen
#include <arpa/inet.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
static const size_t kBlkSizeLen = 7; // strlen(kBlkSize)
static const size_t kMaxBlkSizeOpt = 15; // kBlkSizeLen + strlen("!") + 1 + strlen(65535) + 1

// TIMEOUT
// Max is 255 (RFC 2349)
static const char* kTimeout = "TIMEOUT";
static const size_t kTimeoutLen = 7; // strlen(kTimeout)
static const size_t kMaxTimeoutOpt = 13; // kTimeoutLen + strlen("!") + 1 + strlen(255) + 1;

// WINDOWSIZE
// Max is 65535 (RFC 7440)
static const char* kWindowSize = "WINDOWSIZE";
static const size_t kWindowSizeLen = 10; // strlen(kWindowSize);
static const size_t kMaxWindowSizeOpt = 18; // kWindowSizeLen + strlen("!") + 1 + strlen(65535) + 1;

// Since RRQ and WRQ come before option negotation, they are limited to max TFTP
// blocksize of 512 (RFC 1350 and 2347).
static const size_t kMaxRequestSize = 512;

#if defined(TFTP_HOSTLIB)
// Host (e.g., netcp, bootserver)
#define DEBUG 0
#elif defined(TFTP_USERLIB)
// Fuchsia (e.g., netsvc)
#define DEBUG 0
#elif defined(TFTP_EFILIB)
// Bootloader: use judiciously, since the console can easily become overwhelmed and hang
#define DEBUG 0
#else
#error unable to identify target environment
#endif

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

static void __ATTR_PRINTF(5, 6) append_option(char** body, size_t* left, const char* name,
        bool force, const char* fmt, ...) {
    char* bodyp = *body;
    size_t leftp = *left;

    size_t offset = strlen(name);
    memcpy(bodyp, name, offset);
    if (force) {
        bodyp[offset] = '!';
        offset++;
    }
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

#define OPCODE(session, msg, value)                                                           \
    do {                                                                                      \
        if (session->use_opcode_prefix) {                                                     \
            (msg)->opcode = htons((value & 0xff) | ((uint16_t)session->opcode_prefix << 8));  \
        } else {                                                                              \
            (msg)->opcode = htons(value);                                                     \
        }                                                                                     \
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
    OPCODE(session, resp, opcode);
    *resp_len = sizeof(*resp);
    session->state = ERROR;
}

tftp_status tx_data(tftp_session* session, tftp_data_msg* resp, size_t* outlen, void* cookie) {
    session->offset = (session->block_number + session->window_index) * session->block_size;
    *outlen = 0;
    if (session->offset <= session->file_size) {
        session->window_index++;
        OPCODE(session, resp, OPCODE_DATA);
        resp->block = session->block_number + session->window_index;
        size_t len = MIN(session->file_size - session->offset, session->block_size);
        xprintf(" -> Copying block #%d (size:%zu/%d) from %zu/%zu [%d/%d]\n",
                session->block_number + session->window_index, len, session->block_size,
                session->offset, session->file_size, session->window_index, session->window_size);
        if (len > 0) {
            // TODO(tkilbourn): assert that these function pointers are set
            tftp_status s = session->file_interface.read(resp->data, &len, session->offset, cookie);
            if (s < 0) {
                xprintf("Err reading: %d\n", s);
                return s;
            }
        }
        *outlen = sizeof(*resp) + len;

        if (session->window_index < session->window_size) {
            xprintf(" -> TRANSMIT_MORE(%d < %d)\n", session->window_index, session->window_size);
        } else {
            xprintf(" -> TRANSMIT_WAIT_ON_ACK(%d >= %d)\n", session->window_index,
                    session->window_size);
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

    // Sensible defaults for non-negotiated values
    s->file_size = DEFAULT_FILESIZE;
    s->mode = DEFAULT_MODE;
    s->max_timeouts = DEFAULT_MAX_TIMEOUTS;
    s->use_opcode_prefix = DEFAULT_USE_OPCODE_PREFIX;

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
    return session->window_index > 0 &&
           session->window_index < session->window_size &&
           ((session->block_number + session->window_index) * session->block_size) <=
            session->file_size;
}

tftp_status tftp_set_options(tftp_session* session, const uint16_t* block_size,
                             const uint8_t* timeout, const uint16_t* window_size) {
    session->options.mask = 0;
    if (block_size) {
        session->options.block_size = *block_size;
        session->options.mask |= BLOCKSIZE_OPTION;
    }
    if (timeout) {
        session->options.timeout = *timeout;
        session->options.mask |= TIMEOUT_OPTION;
    }
    if (window_size) {
        session->options.window_size = *window_size;
        session->options.mask |= WINDOWSIZE_OPTION;
    }
    return TFTP_NO_ERROR;
}

tftp_status tftp_generate_write_request(tftp_session* session,
                                        const char* filename,
                                        tftp_mode mode,
                                        size_t datalen,
                                        const uint16_t* block_size,
                                        const uint8_t* timeout,
                                        const uint16_t* window_size,
                                        void* outgoing,
                                        size_t* outlen,
                                        uint32_t* timeout_ms) {
    if (*outlen < 2) {
        xprintf("outlen too short: %zd\n", *outlen);
        return TFTP_ERR_BUFFER_TOO_SMALL;
    }

    // The actual options are not set until we get a confirmation OACK message. Until then,
    // we have to assume the TFTP defaults.
    session->block_size = DEFAULT_BLOCKSIZE;
    session->timeout = DEFAULT_TIMEOUT;
    session->window_size = DEFAULT_WINDOWSIZE;

    tftp_msg* ack = outgoing;
    OPCODE(session, ack, OPCODE_WRQ);
    char* body = ack->data;
    memset(body, 0, *outlen - sizeof(*ack));
    size_t left = *outlen - sizeof(*ack);
    if (strlen(filename) + 1 > left - kMaxMode) {
        xprintf("filename too long %zd > %zd\n", strlen(filename), left - kMaxMode);
        return TFTP_ERR_INVALID_ARGS;
    }
    strncpy(session->filename, filename, sizeof(session->filename));
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
    session->mode = mode;

    if (left < kMaxTsizeOpt) {
        return TFTP_ERR_BUFFER_TOO_SMALL;
    }
    append_option(&body, &left, kTsize, false, "%zu", datalen);
    session->file_size = datalen;
    tftp_options* sent_opts = &session->client_sent_opts;
    sent_opts->mask = 0;

    if (block_size || session->options.mask & BLOCKSIZE_OPTION) {
        if (left < kMaxBlkSizeOpt) {
            return TFTP_ERR_BUFFER_TOO_SMALL;
        }
        bool force_value;
        if (block_size) {
            force_value = true;
            sent_opts->block_size = *block_size;
        } else {
            force_value = false;
            sent_opts->block_size = session->options.block_size;
        }
        append_option(&body, &left, kBlkSize, force_value, "%"PRIu16, sent_opts->block_size);
        sent_opts->mask |= BLOCKSIZE_OPTION;
    }

    if (timeout || session->options.mask & TIMEOUT_OPTION) {
        if (left < kMaxTimeoutOpt) {
            return TFTP_ERR_BUFFER_TOO_SMALL;
        }
        bool force_value;
        if (timeout) {
            force_value = true;
            sent_opts->timeout = *timeout;
        } else {
            force_value = false;
            sent_opts->timeout = session->options.timeout;
        }
        append_option(&body, &left, kTimeout, force_value, "%"PRIu8, sent_opts->timeout);
        sent_opts->mask |= TIMEOUT_OPTION;
    }

    if (window_size || session->options.mask & WINDOWSIZE_OPTION) {
        if (left < kMaxWindowSizeOpt) {
            return TFTP_ERR_BUFFER_TOO_SMALL;
        }
        bool force_value;
        if (window_size) {
            force_value = true;
            sent_opts->window_size = *window_size;
        } else {
            force_value = false;
            sent_opts->window_size = session->options.window_size;
        }
        append_option(&body, &left, kWindowSize, force_value, "%"PRIu16, sent_opts->window_size);
        sent_opts->mask |= WINDOWSIZE_OPTION;
    }

    *outlen = *outlen - left;
    // Nothing has been negotiated yet so use default
    *timeout_ms = 1000 * session->timeout;

    session->state = SENT_WRQ;
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
    // We could be in RECV_WRQ if our OACK was dropped.
    if (session->state != NONE && session->state != RECV_WRQ) {
        xprintf("Invalid state transition %d -> %d\n", session->state, RECV_WRQ);
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

    strncpy(session->filename, option, sizeof(session->filename));
    char* mode = value;
    if (!strncasecmp(mode, kNetascii, strlen(kNetascii))) {
        session->mode = MODE_NETASCII;
    } else if (!strncasecmp(mode, kOctet, strlen(kOctet))) {
        session->mode = MODE_OCTET;
    } else if (!strncasecmp(mode, kMail, strlen(kMail))) {
        session->mode = MODE_MAIL;
    } else {
        xprintf("Unknown write request mode\n");
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }

    // Initialize the values to TFTP defaults
    session->block_size = DEFAULT_BLOCKSIZE;
    session->timeout = DEFAULT_TIMEOUT;
    session->window_size = DEFAULT_WINDOWSIZE;

    // TODO(tkilbourn): refactor option handling code to share with
    // tftp_handle_oack
    cur += offset;
    bool file_size_seen = false;
    tftp_options requested_options = {.mask = 0};
    tftp_options* override_opts = &session->options;
    while (offset > 0 && left > 0) {
        offset = next_option(cur, left, &option, &value);
        if (!offset) {
            xprintf("No more options\n");
            set_error(session, OPCODE_ERROR, resp, resp_len);
            return TFTP_ERR_INTERNAL;
        }

        if (!strncasecmp(option, kTsize, strlen(kTsize))) { // RFC 2349
            long val = atol(value);
            if (val < 0) {
                xprintf("invalid file size\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            session->file_size = val;
            file_size_seen = true;
        } else if (!strncasecmp(option, kBlkSize, kBlkSizeLen)) { // RFC 2348
            bool force_block_size = (option[kBlkSizeLen] == '!');
            // Valid values range between "8" and "65464" octets, inclusive
            long val = atol(value);
            // TODO(tkilbourn): with an MTU of 1500, shouldn't be more than 1428
            if (val < 8 || val > 65464) {
                xprintf("invalid block size\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            requested_options.block_size = val;
            requested_options.mask |= BLOCKSIZE_OPTION;
            if (force_block_size || !(override_opts->mask & BLOCKSIZE_OPTION)) {
                session->block_size = val;
            } else {
                session->block_size = override_opts->block_size;
            }
        } else if (!strncasecmp(option, kTimeout, kTimeoutLen)) { // RFC 2349
            bool force_timeout_val = (option[kTimeoutLen] == '!');
            // Valid values range between "1" and "255" seconds inclusive.
            long val = atol(value);
            if (val < 1 || val > 255) {
                xprintf("invalid timeout\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            requested_options.timeout = val;
            requested_options.mask |= TIMEOUT_OPTION;
            if (force_timeout_val || !(override_opts->mask & TIMEOUT_OPTION)) {
                session->timeout = val;
            } else {
                session->timeout = override_opts->timeout;
            }
        } else if (!strncasecmp(option, kWindowSize, kWindowSizeLen)) { // RFC 7440
            bool force_window_size = (option[kWindowSizeLen] == '!');
            // The valid values range MUST be between 1 and 65535 blocks, inclusive.
            long val = atol(value);
            if (val < 1 || val > 65535) {
                xprintf("invalid window size\n");
                set_error(session, OPCODE_OERROR, resp, resp_len);
                return TFTP_ERR_INTERNAL;
            }
            requested_options.window_size = val;
            requested_options.mask |= WINDOWSIZE_OPTION;
            if (force_window_size || !(override_opts->mask & WINDOWSIZE_OPTION)) {
                session->window_size = val;
            } else {
                session->window_size = override_opts->window_size;
            }
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

    OPCODE(session, resp, OPCODE_OACK);
    if (file_size_seen) {
        append_option(&body, &left, kTsize, false, "%zu", session->file_size);
    } else {
        xprintf("No TSIZE option specified\n");
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_BAD_STATE;
    }
    if (requested_options.mask & BLOCKSIZE_OPTION) {
        // TODO(jpoichet) Make sure this block size is possible. Need API upwards to
        // request allocation of block size * window size memory
        append_option(&body, &left, kBlkSize, false, "%d", session->block_size);
    }
    if (requested_options.mask & TIMEOUT_OPTION) {
        // TODO(jpoichet) Make sure this timeout is possible. Need API upwards to
        // request allocation of block size * window size memory
        append_option(&body, &left, kTimeout, false, "%d", session->timeout);
        *timeout_ms = 1000 * session->timeout;
    }
    if (requested_options.mask & WINDOWSIZE_OPTION) {
        append_option(&body, &left, kWindowSize, false, "%d", session->window_size);
    }
    if (!session->file_interface.open_write ||
            session->file_interface.open_write(session->filename, session->file_size, cookie)) {
        xprintf("Could not open file on write request\n");
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_BAD_STATE;
    }
    *resp_len = *resp_len - left;
    session->state = RECV_WRQ;

    xprintf("Read/Write Request Parsed\n");
    xprintf("    Mode       : %s\n", session->mode == MODE_NETASCII ? "netascii" :
                                     session->mode == MODE_OCTET ? "octet" :
                                     session->mode == MODE_MAIL ? "mail" :
                                     "unrecognized");
    xprintf("    File Size  : %zu\n", session->file_size);
    xprintf("Options requested: %08x\n", requested_options.mask);
    xprintf("    Block Size : %d\n", requested_options.block_size);
    xprintf("    Timeout    : %d\n", requested_options.timeout);
    xprintf("    Window Size: %d\n", requested_options.window_size);
    xprintf("Using options\n");
    xprintf("    Block Size : %d\n", session->block_size);
    xprintf("    Timeout    : %d\n", session->timeout);
    xprintf("    Window Size: %d\n", session->window_size);

    return TFTP_NO_ERROR;
}

static void tftp_prepare_ack(tftp_session* session,
                             tftp_msg* msg,
                             size_t* msg_len) {
    tftp_data_msg* ack_data = (tftp_data_msg*)msg;
    xprintf(" -> Ack %d\n", session->block_number);
    session->window_index = 0;
    OPCODE(session, ack_data, OPCODE_ACK);
    ack_data->block = session->block_number & 0xffff;
    *msg_len = sizeof(*ack_data);
}

tftp_status tftp_handle_data(tftp_session* session,
                             tftp_msg* msg,
                             size_t msg_len,
                             tftp_msg* resp,
                             size_t* resp_len,
                             uint32_t* timeout_ms,
                             void* cookie) {
    if (session->state == RECV_WRQ || session->state == RECV_DATA) {
        session->state = RECV_DATA;
    } else {
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }

    tftp_data_msg* data = (tftp_data_msg*)msg;
    // The block field of the message is only 16 bits wide. To support large files
    // (> 65535 * blocksize bytes), we allow the block number to wrap. We use signed modulo
    // math to determine the relative location of the block to our current position.
    int16_t block_delta = data->block - (uint16_t)session->block_number;
    xprintf(" <- Block %u (Last = %u, Offset = %d, Size = %ld, Left = %ld)\n",
            session->block_number + block_delta, session->block_number,
            session->block_number * session->block_size, session->file_size,
            session->file_size - session->block_number * session->block_size);
    if (block_delta == 1) {
        xprintf("Advancing normally + 1\n");
        size_t wr = msg_len - sizeof(tftp_data_msg);
        if (wr > 0) {
            tftp_status ret;
            // TODO(tkilbourn): assert that these function pointers are set
            ret = session->file_interface.write(data->data, &wr,
                                                session->block_number * session->block_size,
                                                cookie);
            if (ret < 0) {
                xprintf("Error writing: %d\n", ret);
                return ret;
            }
        }
        session->block_number++;
        session->window_index++;
    } else if (block_delta > 1) {
        // Force sending a ACK with the last block_number we received
        xprintf("Skipped: got %d, expected %d\n", session->block_number + block_delta,
                session->block_number + 1);
        session->window_index = session->window_size;
        // It's possible that a previous ACK wasn't received, increment the prefix
        if (session->use_opcode_prefix) {
            session->opcode_prefix++;
        }
    }

    if (session->window_index == session->window_size ||
            session->block_number * session->block_size > session->file_size) {
        tftp_prepare_ack(session, resp, resp_len);
        if (session->block_number * session->block_size > session->file_size) {
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
    if (session->state != SENT_FIRST_DATA && session->state != SENT_DATA) {
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

    if (session->state != SENT_FIRST_DATA && block_offset == 0) {
        // Don't acknowledge duplicate ACKs, avoiding the "Sorcerer's Apprentice Syndrome"
        *resp_len = 0;
        return TFTP_NO_ERROR;
    }

    if (block_offset < session->window_size) {
        // If it looks like some of our data might have been dropped, modify the prefix
        // before resending.
        if (session->use_opcode_prefix) {
            session->opcode_prefix++;
        }
    }
    session->state = SENT_DATA;
    session->block_number += block_offset;
    session->window_index = 0;

    if (((session->block_number + session->window_index) * session->block_size) >
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
    if (session->state == SENT_WRQ || session->state == SENT_FIRST_DATA) {
        session->state = SENT_FIRST_DATA;
    } else {
        set_error(session, OPCODE_ERROR, resp, resp_len);
        return TFTP_ERR_INTERNAL;
    }

    size_t left = oack_len - sizeof(*oack);
    char* cur = oack->data;
    size_t offset;
    char *option, *value;

    while (left > 0) {
        offset = next_option(cur, left, &option, &value);
        if (!offset) {
            set_error(session, OPCODE_ERROR, resp, resp_len);
            return TFTP_ERR_INTERNAL;
        }

        if (!strncasecmp(option, kBlkSize, kBlkSizeLen)) { // RFC 2348
            if (!(session->client_sent_opts.mask & BLOCKSIZE_OPTION)) {
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
        } else if (!strncasecmp(option, kTimeout, kTimeoutLen)) { // RFC 2349
            if (!(session->client_sent_opts.mask & TIMEOUT_OPTION)) {
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
        } else if (!strncasecmp(option, kWindowSize, kWindowSizeLen)) { // RFC 7440
            if (!(session->client_sent_opts.mask & WINDOWSIZE_OPTION)) {
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
    xprintf("    File Size  : %zu\n", session->file_size);
    xprintf("    Block Size : %d\n", session->block_size);
    xprintf("    Timeout    : %d\n", session->timeout);
    xprintf("    Window Size: %d\n", session->window_size);

    tftp_data_msg* resp_data = (void*)resp;
    session->offset = 0;
    session->block_number = 0;
    session->window_index = 0;

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
    uint16_t opcode = ntohs(msg->opcode) & 0xff;
    xprintf("handle_msg opcode=%u length=%d\n", opcode, (int)inlen);

    // Set default timeout
    *timeout_ms = 1000 * session->timeout;

    // Reset timeout count
    session->consecutive_timeouts = 0;

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

    if ((session->block_number + session->window_index) * session->block_size > session->file_size) {
        *outlen = 0;
        return TFTP_TRANSFER_COMPLETED;
    }

    tftp_status ret = tx_data(session, resp_data, outlen, cookie);
    if (ret < 0) {
        set_error(session, OPCODE_ERROR, outgoing, outlen);
    }
    return ret;
}

void tftp_session_set_max_timeouts(tftp_session* session,
                                   uint16_t max_timeouts) {
    session->max_timeouts = max_timeouts;
}

void tftp_session_set_opcode_prefix_use(tftp_session* session,
                                        bool enable) {
    session->use_opcode_prefix = enable;
}

tftp_status tftp_timeout(tftp_session* session,
                         bool sending,
                         void* msg_buf,
                         size_t* msg_len,
                         size_t buf_sz,
                         uint32_t* timeout_ms,
                         void* file_cookie) {
    xprintf("Timeout\n");
    if (++session->consecutive_timeouts > session->max_timeouts) {
        return TFTP_ERR_TIMED_OUT;
    }
    // It's possible our previous transmission was dropped because of checksum errors.
    // Use a different opcode prefix when we resend.
    if (session->use_opcode_prefix) {
        session->opcode_prefix++;
    }
    if (session->state == SENT_WRQ || session->state == RECV_WRQ) {
        // Resend previous message (OACK for recv and WRQ for send)
        return TFTP_NO_ERROR;
    }
    *msg_len = buf_sz;
    if (sending) {
        // Reset back to the last-acknowledged block
        session->window_index = 0;
        return tftp_prepare_data(session, msg_buf, msg_len, timeout_ms, file_cookie);
    } else {
        // ACK up to the last block read
        tftp_prepare_ack(session, msg_buf, msg_len);
        return TFTP_NO_ERROR;
    }
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
    tftp_mode mode = opts->mode ? *opts->mode : TFTP_DEFAULT_CLIENT_MODE;

    size_t out_sz = out_buf_sz;
    uint32_t timeout_ms;
    tftp_status s =
        tftp_generate_write_request(session,
                                    remote_filename,
                                    mode,
                                    file_size,
                                    opts->block_size,
                                    opts->timeout,
                                    opts->window_size,
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
                                   true,
                                   outgoing,
                                   &out_sz,
                                   out_buf_sz,
                                   &timeout_ms,
                                   file_cookie);
                if (ret == TFTP_ERR_TIMED_OUT) {
                    REPORT_ERR(opts, "too many consecutive timeouts, aborting");
                    return ret;
                }
                if (ret < 0) {
                    REPORT_ERR(opts, "failed during timeout processing");
                    return ret;
                }
                if (out_sz) {
                    n = session->transport_interface.send(outgoing, out_sz, transport_cookie);
                    if (n < 0) {
                        REPORT_ERR(opts, "failed during transport send callback");
                        return n;
                    }
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
        pending = tftp_session_has_pending(session);
    } while (1);

    return TFTP_NO_ERROR;
}

tftp_status tftp_handle_request(tftp_session* session,
                                void* transport_cookie,
                                void* file_cookie,
                                tftp_handler_opts* opts) {
    if (!opts || !opts->inbuf || !opts->outbuf || !opts->outbuf_sz) {
        return TFTP_ERR_INVALID_ARGS;
    }
    size_t in_buf_sz = opts->inbuf_sz;
    void* incoming = (void*)opts->inbuf;
    size_t out_buf_sz = *opts->outbuf_sz;
    void* outgoing = (void*)opts->outbuf;
    size_t out_sz = 0;

    int n, ret;
    bool transfer_in_progress = false;
    do {
        size_t in_sz = in_buf_sz;
        n = session->transport_interface.recv(incoming, in_sz, true, transport_cookie);
        if (n < 0) {
            if (n == TFTP_ERR_TIMED_OUT) {
                if (transfer_in_progress) {
                    uint32_t timeout_ms;
                    ret = tftp_timeout(session,
                                       false,
                                       outgoing,
                                       &out_sz,
                                       out_buf_sz,
                                       &timeout_ms,
                                       file_cookie);
                    if (ret == TFTP_ERR_TIMED_OUT) {
                        REPORT_ERR(opts, "too many consecutive timeouts, aborting");
                        return ret;
                    }
                    if (ret < 0) {
                        REPORT_ERR(opts, "failed during timeout processing");
                        return ret;
                    }
                    if (out_sz) {
                        n = session->transport_interface.send(outgoing, out_sz, transport_cookie);
                        if (n < 0) {
                            REPORT_ERR(opts, "failed during transport send callback");
                            return (tftp_status)n;
                        }
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
        out_sz = out_buf_sz;
        send_opts.outbuf_sz = &out_sz;
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
    if (!opts || !opts->inbuf || !opts->outbuf || !opts->outbuf_sz) {
        return TFTP_ERR_INVALID_ARGS;
    }
    uint32_t timeout_ms;
    tftp_status ret;
    ret = tftp_process_msg(session, opts->inbuf, opts->inbuf_sz,
                           opts->outbuf, opts->outbuf_sz, &timeout_ms, file_cookie);
    if (*opts->outbuf_sz) {
        int n = session->transport_interface.send(opts->outbuf, *opts->outbuf_sz, transport_cookie);
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

