// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

/**
 * This is a library that implements TFTP (RFC 1350) with support for the
 * option extension (RFC 2347) the block size (RFC 2348) timeout interval,
 * transfer size (RFC 2349) and the window size (RFC 7440) options.
 *
 * This library does not deal with the transport of the protocol itself and
 * should be able to be plugged into an existing client or server program.
 *
 * Memory management is the responsibility of the client of the library,
 * allowing its use in more restricted environments like bootloaders.
 *
 * To use this library, one should initialize a TFTP Session and generate
 * a request if the transfer needs to be triggered by the consumer of this
 * library.
 *
 * Once a transfer has been succesfully started, repeated calls to the receive
 * method should be made with the incoming data. Outgoing packets will be
 * generated in the outgoing buffer parameters to each method call.
 *
 * In the case of the passive side of the connection, the receive method should
 * be called repeatedly as well. Upon reception of the first packet the
 * |tftp_open_file| callback will be called to prepare for receiving the file.
 *
 * A timeout value is returned when calling |tftp_generate_write_request| and
 * |tftp_handle_msg| and should be used to notify the library that the expected
 * packet was not receive within the value returned.
 **/

enum {
    TFTP_NO_ERROR = 0,
    TFTP_TRANSFER_COMPLETED = 1,

    TFTP_ERR_INTERNAL = -1,
    TFTP_ERR_NOT_SUPPORTED = -2,
    TFTP_ERR_NOT_FOUND = -3,
    TFTP_ERR_INVALID_ARGS = -10,
    TFTP_ERR_BUFFER_TOO_SMALL = -14,
    TFTP_ERR_BAD_STATE = -20,
    TFTP_ERR_TIMED_OUT = -23,
    TFTP_ERR_IO = -40,
};

#ifdef __cplusplus
extern "C" {
#endif

// Opaque structure
typedef struct tftp_session_t tftp_session;

typedef int32_t tftp_status;

typedef enum {
    MODE_NETASCII,
    MODE_OCTET,
    MODE_MAIL,
} tftp_mode;

// tftp_open_file is called by the library to prepare for writing. |cookie| will
// be passed to this function from the argument to tftp_handle_msg.
//
// This function should return TFTP_NO_ERROR on success, or negative values on
// error.
typedef tftp_status (*tftp_open_file)(const char* filename,
                                      size_t size,
                                      void* cookie);

// tftp_read is called by the library to read |length| bytes, starting at
// |offset|, into |data|. |cookie| will be passed to this function from the
// argument to tftp_handle_msg.
typedef tftp_status (*tftp_read)(void* data, size_t* length, off_t offset, void* cookie);

// tftp_write is called by the library to write |length| bytes, starting at
// |offset|, into the destination. |cookie| will be passed to this function from
// the argument to tftp_handle_msg.
typedef tftp_status (*tftp_write)(const void* data, size_t* length, off_t offset, void* cookie);

// Returns the number of bytes needed to hold a tftp_session.
size_t tftp_sizeof_session(void);

// Initialize the tftp_session pointer using the memory in |buffer| of size
// |size|. Returns TFTP_ERR_BUFFER_TOO_SMALL if |size| is too small.
tftp_status tftp_init(tftp_session** session, void* buffer, size_t size);

// Sets the session callback for opening files for transfer.
int tftp_session_set_open_cb(tftp_session* session, tftp_open_file cb);

// Sets the session callback for reading files to send.
int tftp_session_set_read_cb(tftp_session* session, tftp_read cb);

// Sets the session callback for writing files that are received.
int tftp_session_set_write_cb(tftp_session* session, tftp_write cb);

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
                                        uint8_t window_size,
                                        void* outgoing,
                                        size_t* outlen,
                                        uint32_t* timeout_ms);

// Handle an incoming tftp packet. |incoming| must point to the packet of size
// |inlen|. |outgoing| must point to a scratch buffer the library can use to
// assemble the next packet to send. |outlen| is the size of the outgoing
// scratch buffer. |timeout_ms| is set to the next timeout value the user of the
// library should use when waiting for a response. |cookie| will be passed to
// the tftp callback functions.
tftp_status tftp_handle_msg(tftp_session* session,
                            void* incoming,
                            size_t inlen,
                            void* outgoing,
                            size_t* outlen,
                            uint32_t* timeout_ms,
                            void* cookie);

// Prepare a DATA packet to send to the remote host. This is only required when
// tftp_session_has_pending(session) returns true, as tftp_handle_msg() will
// prepare the first DATA message in each window.
tftp_status tftp_prepare_data(tftp_session* session,
                              void* outgoing,
                              size_t* outlen,
                              uint32_t* timeout_ms,
                              void* cookie);

// If no response from the peer is received before the most recent timeout_ms
// value, this function should be called to take the next appropriate action
// (e.g., retransmit or cancel). |outgoing| must point to a scratch buffer the
// library can use to assemble the next packet to send. |outlen| is the size of
// the outgoing scratch buffer. |timeout_ms| is set to the next timeout value
// the user of the library should use when waiting for a response. |cookie| will
// be passed to the tftp callback functions.
tftp_status tftp_timeout(tftp_session* session,
                         void* outgoing,
                         size_t* outlen,
                         uint32_t* timeout_ms,
                         void* cookie);

// TODO: tftp_error() for client errors that need to be sent to the remote host

#ifdef __cplusplus
}  // extern "C"
#endif
