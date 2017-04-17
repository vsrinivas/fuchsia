// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdlib.h>

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
 * generated and delivered through the |tftp_send_message| callback.
 *
 * In the case of the passive side of the connection, the receive method should
 * be called repeatedly as well. Upon reception of the first packet the
 * |tftp_open_file| callback will be called to allocate the memory necessary
 * to receive the file.
 *
 * A timeout value is returned when calling |tftp_generate_write_request| and
 * |tftp_handle_msg| and should be used to notify the library that the expected
 * packet was not receive within the value returned.
 **/

// TODO(tkilbourn): clean these up
enum {
    TFTP_NO_ERROR = 0,
    TFTP_TRANSFER_COMPLETED = 1,

    TFTP_ERR_INTERNAL = -1,
    TFTP_ERR_NOT_SUPPORTED  = -2,
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

// tftp_open_file is called by the library to prepare for writing. |data| must
// point to at least |size| bytes to hold the incoming file. |cookie| will be
// passed to this function from the argument to tftp_handle_msg.
//
// This function should return TFTP_NO_ERROR on success, or negative values on
// error.
typedef tftp_status (*tftp_open_file)(const char* filename,
                                      size_t size,
                                      void** data,
                                      void* cookie);

// tftp_send_message is called to send data from the library. |cookie| will be
// passed to this function from the argument to one of the tftp library
// functions.
//
// This function should return the number of bytes sent on success, or negative
// values on error.
typedef tftp_status (*tftp_send_message)(void* data, size_t length, void* cookie);

// Initialize the tftp_session pointer using the memory in |buffer| of size
// |size|. Returns TFTP_ERR_BUFFER_TOO_SMALL if |size| is too small.
tftp_status tftp_init(tftp_session** session, void* buffer, size_t size);

// Sets the session callback for opening files for transfer.
int tftp_session_set_open_cb(tftp_session*, tftp_open_file cb);

// Sets the session callback for sending data.
int tftp_session_set_send_cb(tftp_session*, tftp_send_message cb);


// Generates a write request to send to a tftp server. |filename| is the name
// sent to the server. |data| must point to the data to be sent, of size
// |datalen|. |block_size|, |timeout|, and |window_size| negotiate tftp options
// with the server. |outgoing| must point to a scratch buffer the library can
// use to assemble the request. |outlen| is the size of the outgoing scratch
// buffer. (TODO: why is it an inout parameter?) |timeout_ms| is set to the next
// timeout value the user of the library should use when waiting for a response.
// |cookie| will be passed to the tftp callback functions.
tftp_status tftp_generate_write_request(tftp_session* session,
                                        const char* filename,
                                        tftp_mode mode,
                                        void* data,
                                        size_t datalen,
                                        size_t block_size,
                                        uint8_t timeout,
                                        uint8_t window_size,
                                        void* outgoing,
                                        size_t* outlen,
                                        uint32_t* timeout_ms,
                                        void* cookie);

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

#ifdef __cplusplus
}  // extern "C"
#endif
