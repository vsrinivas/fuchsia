// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L  // for strnlen
#include <arpa/inet.h>
#include <ctype.h>
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
static const size_t kTsizeLen = 5; // strlen(kTsize)
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
#include <time.h>
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

/* Build an err packet in resp_buf and set session state to ERROR

     2 bytes        2 bytes    string   1 byte
   +--------------+----------+---------+------+
   | OPCODE_ERROR | ERR_CODE | ERR_MSG |   0  |
   +--------------+----------+---------+------+
*/
static void set_error(tftp_session* session, uint16_t err_code, void* resp_buf,
                      size_t* resp_len, const char* err_msg) {
    tftp_err_msg* resp = resp_buf;
    OPCODE(session, resp, OPCODE_ERROR);
    resp->err_code = htons(err_code);
    size_t err_msg_len = strlen(err_msg);
    size_t max_msg_sz = *resp_len - (sizeof(tftp_err_msg) + 1);
    if (err_msg_len >= max_msg_sz) {
        memcpy(resp->msg, err_msg, max_msg_sz);
        resp->msg[max_msg_sz] = '\0';
        // *resp_len is unchanged - the whole buffer was used
    } else {
        strcpy(resp->msg, err_msg);
        *resp_len = sizeof(tftp_err_msg) + err_msg_len + 1;
    }
    session->state = ERROR;
}

tftp_status tx_data(tftp_session* session, tftp_data_msg* resp, size_t* outlen, void* cookie) {
    session->offset = (session->block_number + session->window_index) * session->block_size;
    *outlen = 0;
    if (session->offset <= session->file_size) {
        session->window_index++;
        OPCODE(session, resp, OPCODE_DATA);
        resp->block = htons(session->block_number + session->window_index);
        size_t len = MIN(session->file_size - session->offset, session->block_size);
        xprintf(" -> Copying block #%" PRIu64 " (size:%zu/%d) from %zu/%zu [%d/%d]\n",
                session->block_number + session->window_index, len, session->block_size,
                session->offset, session->file_size, session->window_index, session->window_size);
        void* buf = resp->data;
        size_t len_remaining = len;
        size_t off = session->offset;
        while (len_remaining > 0) {
            // TODO(tkilbourn): assert that these function pointers are set
            size_t rr = len_remaining;
            tftp_status s = session->file_interface.read(buf, &rr, off, cookie);
            if (s < 0) {
                xprintf("Err reading: %d\n", s);
                return s;
            }
            buf += rr;
            off += rr;
            len_remaining -= rr;
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
    return session->direction == SEND_FILE &&
           session->window_index > 0 &&
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

tftp_status tftp_generate_request(tftp_session* session,
                                  tftp_file_direction direction,
                                  const char* local_filename,
                                  const char* remote_filename,
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
    OPCODE(session, ack, (direction == SEND_FILE) ? OPCODE_WRQ : OPCODE_RRQ);
    char* body = ack->data;
    memset(body, 0, *outlen - sizeof(*ack));
    size_t left = *outlen - sizeof(*ack);
    size_t remote_filename_len = strlen(remote_filename);
    if (remote_filename_len + 1 > left - kMaxMode) {
        xprintf("filename too long %zd > %zd\n", remote_filename_len, left - kMaxMode);
        return TFTP_ERR_INVALID_ARGS;
    }
    memcpy(body, remote_filename, remote_filename_len);
    body += remote_filename_len + 1;
    left -= remote_filename_len + 1;
    strncpy(session->filename, local_filename, sizeof(session->filename));
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

    session->direction = direction;
    session->state = REQ_SENT;
    xprintf("Generated %s request, len=%zu\n",
            (direction == SEND_FILE) ? "write" : "read", *outlen);
    return TFTP_NO_ERROR;
}

tftp_status tftp_handle_request(tftp_session* session,
                                tftp_file_direction direction,
                                tftp_msg* req,
                                size_t req_len,
                                tftp_msg* resp,
                                size_t* resp_len,
                                uint32_t* timeout_ms,
                                void* cookie) {
    // We could be in REQ_RECEIVED if our OACK was dropped.
    if (session->state != NONE && session->state != REQ_RECEIVED) {
        xprintf("Invalid state transition %d -> %d\n", session->state, REQ_RECEIVED);
        set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "invalid state transition");
        return TFTP_ERR_BAD_STATE;
    }
    // opcode, filename, 0, mode, 0, opt1, 0, value1 ... optN, 0, valueN, 0
    // Max length is 512 no matter
    if (req_len > kMaxRequestSize) {
        xprintf("Write request is too large\n");
        set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "write request is too large");
        return TFTP_ERR_INTERNAL;
    }
    // Skip opcode
    size_t left = req_len - sizeof(*resp);
    char* cur = req->data;
    char *option, *value;
    // filename, 0, mode, 0 can be interpreted like option, 0, value, 0
    size_t offset = next_option(cur, left, &option, &value);
    if (!offset) {
        xprintf("No options\n");
        set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "no options");
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
        set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "unknown write request mode");
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
            set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "no more options");
            return TFTP_ERR_INTERNAL;
        }

        if (!strncasecmp(option, kTsize, kTsizeLen)) { // RFC 2349
            if (direction == RECV_FILE) {
                long val = atol(value);
                if (val < 0) {
                    xprintf("invalid file size\n");
                    set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len,
                              "invalid file size");
                    return TFTP_ERR_INTERNAL;
                }
                session->file_size = val;
            }
            file_size_seen = true;
        } else if (!strncasecmp(option, kBlkSize, kBlkSizeLen)) { // RFC 2348
            bool force_block_size = (option[kBlkSizeLen] == '!');
            // Valid values range between "8" and "65464" octets, inclusive
            long val = atol(value);
            // TODO(tkilbourn): with an MTU of 1500, shouldn't be more than 1428
            if (val < 8 || val > 65464) {
                xprintf("invalid block size\n");
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "invalid block size");
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
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "invalid timeout");
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
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len,
                          "invalid window size");
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

    // Open file, if we haven't already
    if (session->state == NONE) {
        if (direction == RECV_FILE) {
            if (!session->file_interface.open_write) {
                xprintf("Unable to service write request: no open_write implementation\n");
                set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "internal error");
                return TFTP_ERR_BAD_STATE;
            }
            switch(session->file_interface.open_write(session->filename, session->file_size,
                                                      cookie)) {
            case TFTP_ERR_SHOULD_WAIT:
                // The open_write() callback can return an ERR_SHOULD_WAIT response if it isn't
                // prepared to service another requst at the moment and the client should retry
                // later.
                xprintf("Denying write request received when not ready\n");
                set_error(session, TFTP_ERR_CODE_BUSY, resp, resp_len, "not ready to receive");
                session->state = NONE;
                return TFTP_ERR_SHOULD_WAIT;
            case TFTP_NO_ERROR:
                break;
            default:
                xprintf("Could not open file on write request\n");
                set_error(session, TFTP_ERR_CODE_ACCESS_VIOLATION, resp, resp_len,
                          "could not open file for writing");
                return TFTP_ERR_BAD_STATE;
            }
        } else {
            ssize_t file_size;
            if (!session->file_interface.open_read) {
                xprintf("Unable to service read request: no open_read implementation\n");
                set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "internal error");
                return TFTP_ERR_BAD_STATE;
            }

            file_size = session->file_interface.open_read(session->filename, cookie);
            if (file_size == TFTP_ERR_SHOULD_WAIT) {
                // The open_read() callback can return an ERR_SHOULD_WAIT response if it isn't
                // prepared to service another requst at the moment and the client should retry
                // later.
                xprintf("Denying read request received when not ready\n");
                set_error(session, TFTP_ERR_CODE_BUSY, resp, resp_len, "not ready to send");
                session->state = NONE;
                return TFTP_ERR_SHOULD_WAIT;
            }
            if (file_size < 0) {
                xprintf("Unable to open file %s for reading\n", session->filename);
                set_error(session, TFTP_ERR_CODE_FILE_NOT_FOUND, resp, resp_len,
                          "could not open file for reading");
                return TFTP_ERR_BAD_STATE;
            }
            session->file_size = file_size;
        }
    }

    if (file_size_seen) {
        append_option(&body, &left, kTsize, false, "%zu", session->file_size);
    } else {
        xprintf("No TSIZE option specified\n");
        set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "no TSIZE option");
        if (session->file_interface.close) {
            session->file_interface.close(cookie);
        }
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
    *resp_len = *resp_len - left;
    session->state = REQ_RECEIVED;
    session->direction = direction;

    xprintf("%s Request Parsed\n", (direction == SEND_FILE) ? "Read" : "Write");
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

tftp_status tftp_handle_wrq(tftp_session* session,
                            tftp_msg* wrq,
                            size_t wrq_len,
                            tftp_msg* resp,
                            size_t* resp_len,
                            uint32_t* timeout_ms,
                            void* cookie) {
    return tftp_handle_request(session, RECV_FILE, wrq, wrq_len, resp, resp_len, timeout_ms,
                               cookie);
}

tftp_status tftp_handle_rrq(tftp_session* session,
                            tftp_msg* rrq,
                            size_t rrq_len,
                            tftp_msg* resp,
                            size_t* resp_len,
                            uint32_t* timeout_ms,
                            void* cookie) {
    return tftp_handle_request(session, SEND_FILE, rrq, rrq_len, resp, resp_len, timeout_ms,
                               cookie);
}

static void tftp_prepare_ack(tftp_session* session,
                             tftp_msg* msg,
                             size_t* msg_len) {
    tftp_data_msg* ack_data = (tftp_data_msg*)msg;
    xprintf(" -> Ack %" PRIu64 "\n", session->block_number);
    session->window_index = 0;
    OPCODE(session, ack_data, OPCODE_ACK);
    ack_data->block = htons(session->block_number & 0xffff);
    *msg_len = sizeof(*ack_data);
}

tftp_status tftp_handle_data(tftp_session* session,
                             tftp_msg* msg,
                             size_t msg_len,
                             tftp_msg* resp,
                             size_t* resp_len,
                             uint32_t* timeout_ms,
                             void* cookie) {
    if ((session->direction == RECV_FILE) &&
        ((session->state == REQ_RECEIVED) ||
         (session->state == FIRST_DATA) ||
         (session->state == RECEIVING_DATA))) {
        session->state = RECEIVING_DATA;
    } else {
        set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "internal error: bad state");
        return TFTP_ERR_INTERNAL;
    }

    tftp_data_msg* data = (tftp_data_msg*)msg;

    uint16_t block_num = ntohs(data->block);

    // The block field of the message is only 16 bits wide. To support large files
    // (> 65535 * blocksize bytes), we allow the block number to wrap. We use signed modulo
    // math to determine the relative location of the block to our current position.
    int16_t block_delta = block_num - (uint16_t)session->block_number;
    xprintf(" <- Block %" PRIu64 " (Last = %" PRIu64 ", Offset = %" PRIu64
                ", Size = %zd, Left = %" PRIu64 ")\n",
            session->block_number + block_delta, session->block_number,
            session->block_number * session->block_size, session->file_size,
            session->file_size - session->block_number * session->block_size);
    if (block_delta == 1) {
        xprintf("Advancing normally + 1\n");
        void* buf = data->data;
        size_t len = msg_len - sizeof(tftp_data_msg);
        size_t off = session->block_number * session->block_size;
        while (len > 0) {
            tftp_status ret;
            // TODO(tkilbourn): assert that these function pointers are set
            size_t wr = len;
            ret = session->file_interface.write(buf, &wr, off, cookie);
            if (ret < 0) {
                xprintf("Error writing: %d\n", ret);
                return ret;
            }
            buf += wr;
            off += wr;
            len -= wr;
        }
        session->block_number++;
        session->window_index++;
    } else if (block_delta > 1) {
        // Force sending a ACK with the last block_number we received
        xprintf("Skipped: got %" PRIu64 ", expected %" PRIu64 "\n",
                session->block_number + block_delta, session->block_number + 1);
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
    if ((session->direction != SEND_FILE) ||
        ((session->state != FIRST_DATA) &&
         (session->state != REQ_RECEIVED) &&
         (session->state != SENDING_DATA))) {
        set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "internal error: bad state");
        return TFTP_ERR_INTERNAL;
    }
    // Need to move forward in data and send it
    tftp_data_msg* ack_data = (void*)ack;
    tftp_data_msg* resp_data = (void*)resp;

    uint16_t ack_block = ntohs(ack_data->block);
    xprintf(" <- Ack %d\n", ack_block);

    // Since we track blocks in 32 bits, but the packets only support 16 bits, calculate the
    // signed 16 bit offset to determine the adjustment to the current position.
    int16_t block_offset = ack_block - (uint16_t)session->block_number;

    if (session->state != FIRST_DATA && session->state != REQ_RECEIVED && block_offset == 0) {
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
    session->state = SENDING_DATA;
    session->block_number += block_offset;
    session->window_index = 0;

    if (session->block_number * session->block_size > session->file_size) {
        *resp_len = 0;
        return TFTP_TRANSFER_COMPLETED;
    }

    tftp_status ret = tx_data(session, resp_data, resp_len, cookie);
    if (ret < 0) {
        set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "could not transmit data");
    }
    return ret;
}

tftp_status tftp_handle_error(tftp_session* session,
                              tftp_err_msg* err,
                              size_t err_len,
                              tftp_msg* resp,
                              size_t* resp_len,
                              uint32_t* timeout_ms,
                              void* cookie) {
    uint16_t err_code = ntohs(err->err_code);

    // There's no need to respond to an error
    *resp_len = 0;

    if (err_code == TFTP_ERR_CODE_BUSY) {
        xprintf("Target busy\n");
        session->state = NONE;
        return TFTP_ERR_SHOULD_WAIT;
    }
    xprintf("Target sent error %d\n", err_code);
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
    if (session->state == REQ_SENT || session->state == FIRST_DATA) {
        session->state = FIRST_DATA;
    }

    size_t left = oack_len - sizeof(*oack);
    char* cur = oack->data;
    size_t offset;
    char *option, *value;

    while (left > 0) {
        offset = next_option(cur, left, &option, &value);
        if (!offset) {
            set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "invalid option format");
            return TFTP_ERR_INTERNAL;
        }

        if (!strncasecmp(option, kTsize, kTsizeLen)) { // RFC 2349
            if (session->direction == RECV_FILE) {
                session->file_size = atol(value);
            }
            // If we are sending the file, we don't care what value the server wrote in here
        } else if (!strncasecmp(option, kBlkSize, kBlkSizeLen)) { // RFC 2348
            if (!(session->client_sent_opts.mask & BLOCKSIZE_OPTION)) {
                xprintf("block size not requested\n");
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "no block size");
                return TFTP_ERR_INTERNAL;
            }
            // Valid values range between "8" and "65464" octets, inclusive
            long val = atol(value);
            if (val < 8 || val > 65464) {
                xprintf("invalid block size\n");
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "invalid block size");
                return TFTP_ERR_INTERNAL;
            }
            // TODO(tkilbourn): with an MTU of 1500, shouldn't be more than 1428
            session->block_size = val;
        } else if (!strncasecmp(option, kTimeout, kTimeoutLen)) { // RFC 2349
            if (!(session->client_sent_opts.mask & TIMEOUT_OPTION)) {
                xprintf("timeout not requested\n");
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "no timeout");
                return TFTP_ERR_INTERNAL;
            }
            // Valid values range between "1" and "255" seconds inclusive.
            long val = atol(value);
            if (val < 1 || val > 255) {
                xprintf("invalid timeout\n");
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "invalid timeout");
                return TFTP_ERR_INTERNAL;
            }
            session->timeout = val;
        } else if (!strncasecmp(option, kWindowSize, kWindowSizeLen)) { // RFC 7440
            if (!(session->client_sent_opts.mask & WINDOWSIZE_OPTION)) {
                xprintf("window size not requested\n");
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len, "no window size");
                return TFTP_ERR_INTERNAL;
            }
            // The valid values range MUST be between 1 and 65535 blocks, inclusive.
            long val = atol(value);
            if (val < 1 || val > 65535) {
                xprintf("invalid window size\n");
                set_error(session, TFTP_ERR_CODE_BAD_OPTIONS, resp, resp_len,
                          "invalid window size");
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

    session->offset = 0;
    session->block_number = 0;
    session->window_index = 0;

    if (session->direction == SEND_FILE) {
        tftp_data_msg* resp_data = (void*)resp;
        tftp_status ret = tx_data(session, resp_data, resp_len, cookie);
        if (ret < 0) {
            set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "failure to transmit data");
        }
        return ret;
    } else {
        if (!session->file_interface.open_write ||
            session->file_interface.open_write(session->filename, session->file_size,
                                               cookie)) {
            xprintf("Could not open file on write request\n");
            set_error(session, TFTP_ERR_CODE_UNDEF, resp, resp_len, "could not open file for writing");
            return TFTP_ERR_BAD_STATE;
        }
        tftp_prepare_ack(session, resp, resp_len);
        return TFTP_NO_ERROR;
    }
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
        return tftp_handle_rrq(session, incoming, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_WRQ:
        return tftp_handle_wrq(session, incoming, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_DATA:
        return tftp_handle_data(session, incoming, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_ACK:
        return tftp_handle_ack(session, incoming, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_ERROR:
        return tftp_handle_error(session, incoming, inlen, resp, outlen, timeout_ms, cookie);
    case OPCODE_OACK:
        return tftp_handle_oack(session, incoming, inlen, resp, outlen, timeout_ms, cookie);
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
        set_error(session, TFTP_ERR_CODE_UNDEF, outgoing, outlen, "failure to transmit data");
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
    if (session->state == REQ_SENT || session->state == REQ_RECEIVED) {
        // Resend previous message
        return TFTP_NO_ERROR;
    }
    *msg_len = buf_sz;
    if (session->direction == SEND_FILE) {
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

typedef struct {
    void* incoming;
    size_t in_buf_sz;
    void* outgoing;
    size_t out_buf_sz;
    char* err_msg;
    size_t err_msg_sz;
} tftp_msg_loop_opts;

static tftp_status tftp_msg_loop(tftp_session* session,
                                 void* transport_cookie,
                                 void* file_cookie,
                                 tftp_msg_loop_opts* opts,
                                 uint32_t timeout_ms) {
    tftp_status ret;
    size_t out_sz = 0;

    do {
        tftp_status send_status;
        int result = session->transport_interface.timeout_set(timeout_ms, transport_cookie);
        if (result < 0) {
            REPORT_ERR(opts, "failed during transport timeout set callback");
            return TFTP_ERR_INTERNAL;
        }

        bool pending = tftp_session_has_pending(session);
        int n = session->transport_interface.recv(opts->incoming, opts->in_buf_sz, !pending,
                                                  transport_cookie);
        if (n == TFTP_ERR_TIMED_OUT) {
            if (pending) {
                out_sz = opts->out_buf_sz;
                ret = tftp_prepare_data(session,
                                        opts->outgoing,
                                        &out_sz,
                                        &timeout_ms,
                                        file_cookie);
                if (out_sz) {
                    send_status = session->transport_interface.send(opts->outgoing, out_sz,
                                                                    transport_cookie);
                    if (send_status != TFTP_NO_ERROR) {
                        REPORT_ERR(opts, "failed during transport send callback");
                        return send_status;
                    }
                }
                if (ret < 0) {
                    REPORT_ERR(opts, "failed to prepare data to send");
                    return ret;
                }
            } else if (session->state != NONE) {
                ret = tftp_timeout(session,
                                   opts->outgoing,
                                   &out_sz,
                                   opts->out_buf_sz,
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
                    send_status = session->transport_interface.send(opts->outgoing, out_sz,
                                                                    transport_cookie);
                    if (send_status != TFTP_NO_ERROR) {
                        REPORT_ERR(opts, "failed during transport send callback");
                        return n;
                    }
                }
            }
            continue;
        } else if (n < 0) {
            REPORT_ERR(opts, "failed during transport recv callback");
            return n;
        }

        out_sz = opts->out_buf_sz;
        ret = tftp_process_msg(session,
                               opts->incoming,
                               n,
                               opts->outgoing,
                               &out_sz,
                               &timeout_ms,
                               file_cookie);
        if (out_sz) {
            send_status = session->transport_interface.send(opts->outgoing, out_sz,
                                                            transport_cookie);
            if (send_status != TFTP_NO_ERROR) {
                REPORT_ERR(opts, "failed during transport send callback");
                return send_status;
            }
        }
        if (ret < 0) {
            REPORT_ERR(opts, "failed to handle message");
            return ret;
        } else if (ret == TFTP_TRANSFER_COMPLETED) {
            return ret;
        }
    } while (1);
}

static tftp_status transfer_file(tftp_session* session,
                                 void* transport_cookie,
                                 void* file_cookie,
                                 tftp_file_direction xfer_direction,
                                 const char* local_filename,
                                 const char* remote_filename,
                                 tftp_request_opts* opts) {
    if (!opts || !opts->inbuf || !opts->inbuf_sz || !opts->outbuf || !opts->outbuf_sz) {
        return TFTP_ERR_INVALID_ARGS;
    }

    tftp_status status;

    ssize_t file_size = 0;
    if (xfer_direction == SEND_FILE) {
        file_size = session->file_interface.open_read(local_filename, file_cookie);
        if (file_size < 0) {
            REPORT_ERR(opts, "failed during file open callback");
            return file_size;
        }
    }

    tftp_mode mode = opts->mode ? *opts->mode : TFTP_DEFAULT_CLIENT_MODE;

    size_t out_sz = opts->outbuf_sz;
    uint32_t timeout_ms;
    status = tftp_generate_request(session,
                                   xfer_direction,
                                   local_filename,
                                   remote_filename,
                                   mode,
                                   file_size,
                                   opts->block_size,
                                   opts->timeout,
                                   opts->window_size,
                                   opts->outbuf,
                                   &out_sz,
                                   &timeout_ms);

    const char* xfer_direction_str = (xfer_direction == SEND_FILE) ? "write" : "read";
    if (status < 0) {
        REPORT_ERR(opts, "failed to generate %s request", xfer_direction_str);
        goto done;
    }
    if (!out_sz) {
        REPORT_ERR(opts, "no %s request generated", xfer_direction_str);
        status = TFTP_ERR_INTERNAL;
        goto done;
    }

    status = session->transport_interface.send(opts->outbuf, out_sz, transport_cookie);
    if (status != TFTP_NO_ERROR) {
        REPORT_ERR(opts, "failed during transport send callback");
        goto done;
    }

    tftp_msg_loop_opts msg_loop_opts = {.incoming = opts->inbuf,
                                        .in_buf_sz = opts->inbuf_sz,
                                        .outgoing = opts->outbuf,
                                        .out_buf_sz = opts->outbuf_sz,
                                        .err_msg = opts->err_msg,
                                        .err_msg_sz = opts->err_msg_sz};
    status = tftp_msg_loop(session, transport_cookie, file_cookie, &msg_loop_opts, timeout_ms);

done:
    if ((xfer_direction == SEND_FILE) || (session->state != NONE)) {
        if (session->file_interface.close) {
            session->file_interface.close(file_cookie);
        }
    }
    return status;
}

tftp_status tftp_push_file(tftp_session* session,
                           void* transport_cookie,
                           void* file_cookie,
                           const char* local_filename,
                           const char* remote_filename,
                           tftp_request_opts* opts) {
    return transfer_file(session, transport_cookie, file_cookie, SEND_FILE,
                         local_filename, remote_filename, opts);
}

tftp_status tftp_pull_file(tftp_session* session,
                           void* transport_cookie,
                           void* file_cookie,
                           const char* local_filename,
                           const char* remote_filename,
                           tftp_request_opts* opts) {
    return transfer_file(session, transport_cookie, file_cookie, RECV_FILE,
                         local_filename, remote_filename, opts);
}

tftp_status tftp_service_request(tftp_session* session,
                                 void* transport_cookie,
                                 void* file_cookie,
                                 tftp_handler_opts* opts) {
    if (!opts || !opts->inbuf || !opts->outbuf || !opts->outbuf_sz) {
        return TFTP_ERR_INVALID_ARGS;
    }
    tftp_msg_loop_opts msg_loop_opts = {.incoming = opts->inbuf,
                                        .in_buf_sz = opts->inbuf_sz,
                                        .outgoing = opts->outbuf,
                                        .out_buf_sz = *opts->outbuf_sz,
                                        .err_msg = opts->err_msg,
                                        .err_msg_sz = opts->err_msg_sz};
    uint32_t timeout_ms = session->timeout * 1000;
    tftp_status status = tftp_msg_loop(session, transport_cookie, file_cookie, &msg_loop_opts,
                                       timeout_ms);
    if ((session->state != NONE) && session->file_interface.close) {
        session->file_interface.close(file_cookie);
    }
    return status;
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
        tftp_status send_status = session->transport_interface.send(opts->outbuf, *opts->outbuf_sz,
                                                                    transport_cookie);
        if (send_status != TFTP_NO_ERROR) {
            REPORT_ERR(opts, "failed during transport send callback");
            return send_status;
        }
    }
    if (ret == TFTP_ERR_SHOULD_WAIT) {
        REPORT_ERR(opts, "request received, host is busy");
    } else if (ret < 0) {
        REPORT_ERR(opts, "handling tftp request failed (file might not exist)");
    } else if (ret == TFTP_TRANSFER_COMPLETED) {
        if (session->file_interface.close) {
            session->file_interface.close(file_cookie);
        }
    } else {
        ret = session->transport_interface.timeout_set(timeout_ms, transport_cookie);
        if (ret < 0) {
            REPORT_ERR(opts, "failed during transport timeout set callback");
        }
    }
    return ret;
}

