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
 * It also supports block count rollover, which allows us to transfer files
 * larger than 65536 * block size bytes. This is purported to be a common
 * extension of the TFTP protocol.
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
 * |tftp_file_open_cb| callback will be called to prepare for receiving the
 * file.
 *
 * A timeout value is returned when calling |tftp_generate_write_request| and
 * |tftp_process_msg| and should be used to notify the library that the
 * expected packet was not receive within the value returned.
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

// These are the default values used when sending a tftp request
#define TFTP_DEFAULT_CLIENT_BLOCKSZ 1024
#define TFTP_DEFAULT_CLIENT_TIMEOUT 1
#define TFTP_DEFAULT_CLIENT_WINSZ 64
#define TFTP_DEFAULT_CLIENT_MODE MODE_OCTET

typedef struct {
    char* inbuf;          // required - buffer for assembling incoming msgs
    size_t inbuf_sz;      // required
    char* outbuf;         // required - buffer for assembling outgoing msgs
    size_t outbuf_sz;     // required
    tftp_mode* mode;
    uint16_t* block_size;
    uint16_t* window_size;
    uint8_t* timeout;
    char* err_msg;
    size_t err_msg_sz;
} tftp_request_opts;

typedef struct {
    char* inbuf;         // required - buffer for assembling incoming msgs
    size_t inbuf_sz;     // required
    char* outbuf;        // required - buffer for assembling outgoing msgs
    size_t* outbuf_sz;   // required
    char* err_msg;
    size_t err_msg_sz;
} tftp_handler_opts;

// tftp_file_open_read_cb is called by the library to prepare for reading.
// |file_cookie| will be passed to this function from the argument to
// tftp_process_msg.
//
// This function should return the size of the file on success, or a TFTP_ERR_*
// error code on failure.
typedef ssize_t (*tftp_file_open_read_cb)(const char* filename,
                                          void* file_cookie);

// tftp_file_open_write_cb is called by the library to prepare a file for
// writing. |file_cookie| will be passed to this function from the argument to
// tftp_process_msg. |size| indicates the size of the file that will be
// created (it may be ignored if this information is not needed on opening).
typedef tftp_status (*tftp_file_open_write_cb)(const char* filename,
                                               size_t size,
                                               void* file_cookie);

// tftp_file_read_cb is called by the library to read |length| bytes, starting
// at |offset|, into |data|. |file_cookie| will be passed to this function from
// the argument to tftp_process_msg.
typedef tftp_status (*tftp_file_read_cb)(void* data,
                                         size_t* length,
                                         off_t offset,
                                         void* file_cookie);

// tftp_file_write_cb is called by the library to write |length| bytes,
// starting at |offset|, into the destination. |file_cookie| will be passed to
// this function from the argument to tftp_process_msg.
typedef tftp_status (*tftp_file_write_cb)(const void* data,
                                          size_t* length,
                                          off_t offset,
                                          void* file_cookie);

// tftp_file_close_cb is called by the library to finish a file read or write
// operation. |file_cookie| will be passed to this function from the argument to
// tftp_process_msg.
typedef void (*tftp_file_close_cb)(void* file_cookie);

typedef struct {
    tftp_file_open_read_cb open_read;
    tftp_file_open_write_cb open_write;
    tftp_file_read_cb read;
    tftp_file_write_cb write;
    tftp_file_close_cb close;
} tftp_file_interface;

// tftp_transport_send_cb is called by the library to send |len| bytes from
// |data| over a previously-established connection. On success, the function
// should return the number of bytes sent. On error, it should return a
// tftp_status error code.
typedef int (*tftp_transport_send_cb)(void* data,
                                      size_t len,
                                      void* transport_cookie);

// tftp_transport_recv_cb is called by the library to read from the transport
// interface. It will read values into |data|, up to |len| bytes. If |block| is
// set, the operation will block until data is received or a timeout happens.
// (For starting communication, the timeout should be set by the user if
// desired. Once communication has been established, the timeout is set by the
// tftp library using the timeout_set callback).
// On success, the function should return the number of bytes received. On
// failure it should return a tftp_status error code.
typedef int (*tftp_transport_recv_cb)(void* data,
                                      size_t len,
                                      bool block,
                                      void* transport_cookie);

// tftp_transport_timeout_set_cb is called by the library to set the timeout
// length of the transport interface. This function should return 0 on success
// or -1 on failure.
typedef int (*tftp_transport_timeout_set_cb)(uint32_t timeout_ms,
                                             void* transport_cookie);

typedef struct {
    tftp_transport_send_cb send;
    tftp_transport_recv_cb recv;
    tftp_transport_timeout_set_cb timeout_set;
} tftp_transport_interface;

// Returns the number of bytes needed to hold a tftp_session.
size_t tftp_sizeof_session(void);

// Initialize the tftp_session pointer using the memory in |buffer| of size
// |size|. Returns TFTP_ERR_BUFFER_TOO_SMALL if |size| is too small.
tftp_status tftp_init(tftp_session** session, void* buffer, size_t size);

// Specifies the callback functions to use when reading or writing files.
tftp_status tftp_session_set_file_interface(tftp_session* session,
                                            tftp_file_interface* callbacks);

// Specifies the callback functions to use for the network interface. Note that
// setting up the transport must be performed outside of the purview of the
// library, since the initial configuration options are highly interface-
// dependent.
tftp_status tftp_session_set_transport_interface(tftp_session* session,
                                                 tftp_transport_interface* callbacks);

// Specifies how many consecutive timeouts we will endure before terminating
// a session.
void tftp_session_set_max_timeouts(tftp_session* session,
                                   uint16_t max_timeouts);

// Specify whether to use the upper 8 bits of the opcode field as a pseudo
// retransmission count. When enabled, this tweaks the contents of a
// retransmitted packet just enough that it will have a different checksum,
// avoiding the problem we have on ASIX 88179 adapters that (reliably)
// generate incorrect checksums for certain packets.
void tftp_session_set_opcode_prefix_use(tftp_session* session,
                                        bool enable);

// When acting as a server, the options that will be overridden when a
// value is requested by the client. Note that if the client does not
// specify a setting, the default will be used regardless of server
// setting.
// When acting as a client, the default options that will be used when
// initiating a transfer. If any of the values are not set, it will not be
// specified in the request packet, and so the tftp defaults will be used.
tftp_status tftp_set_options(tftp_session* session,
                             const uint16_t* block_size,
                             const uint8_t* timeout,
                             const uint16_t* window_size);

// If no response from the peer is received before the most recent timeout_ms
// value, this function should be called to take the next appropriate action
// (e.g., retransmit or cancel). |sending| indicates whether we are sending or
// receiving a file. |msg_buf| must point to the last message sent, which is
// |msg_len| bytes long. |buf_sz| represents the total size of |msg_buf|,
// which may be used to assemble the next packet to send. |timeout_ms| is set
// to the next timeout value the user of the library should use when waiting
// for a response. |file_cookie| will be passed to the tftp callback functions.
// On return, TFTP_ERR_TIMED out is returned if the maximum number of timeouts
// has been exceeded. If a message should be sent out, |msg_len| will be set
// to the size of the message.
tftp_status tftp_timeout(tftp_session* session,
                         bool sending,
                         void* msg_buf,
                         size_t* msg_len,
                         size_t buf_sz,
                         uint32_t* timeout_ms,
                         void* file_cookie);

// Request to send the file |local_filename| across an existing session
// to |remote_filename| on the target. If |options| is NULL, all values are
// set to some (semi-)reasonable defaults. Otherwise, all non-NULL members of
// |options| are used to override defaults. Before calling this function, the
// client transport interface should be configured as needed.
tftp_status tftp_push_file(tftp_session* session,
                           void* transport_cookie,
                           void* file_cookie,
                           const char* local_filename,
                           const char* remote_filename,
                           tftp_request_opts* options);

// Wait for a client to request an operation, then service that request.
// Returns (with TFTP_TRANSFER_COMPLETED) after each successful operation, or
// on error. This function will call the transport send, recv, and timeout_set
// operations as needed to facilitate communication with the requestor.
tftp_status tftp_handle_request(tftp_session* session,
                                void* transport_cookie,
                                void* file_cookie,
                                tftp_handler_opts* opts);

// Processes a single message from the requestor, which is passed in as the
// inbuf component of |opts|. Responds to the request and updates the
// connection timeout using the appropriate transport send and timeout_set
// functions. Also, sets the value of outbuf_sz in |opts| to the size of
// the message sent (or 0 if no message was sent).
tftp_status tftp_handle_msg(tftp_session* session,
                            void* transport_cookie,
                            void* file_cookie,
                            tftp_handler_opts* opts);

// TODO: tftp_error() for client errors that need to be sent to the remote host

#ifdef __cplusplus
}  // extern "C"
#endif
