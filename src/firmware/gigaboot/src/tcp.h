// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// EFI TCP Wrapper
//
// These APIs provide a simple wrapper around the EFI TCP protocol, hiding much
// of the complexity around the asynchronous behavior and error handling to
// expose a basic set of accept/read/write/disconnect APIs.
//
// This API was designed to mesh well with the existing fastboot code, which
// executes as a state machine run in a main loop. For this purpose, the TCP
// callback mechanism isn't used, and instead we expose functions that can be
// polled. General usage will look like this:
//
// switch (tcp6_func(...)) {
//   case TCP6_RESULT_SUCCESS:  // operation completed successfully
//   case TCP6_RESULT_PENDING:  // not ready yet, call again next loop
//   case TCP6_RESULT_DISCONNECTED:  // client disconnected
//   case TCP6_RESULT_ERROR:  // unexpected error
// }
//
// Limitations:
//   * currently only supports TCP6
//   * the device must implement EFI_TCP6_PROTOCOL; we aren't implementing TCP
//     here, we're just wrapping an existing driver in a simpler API
//   * only supports being the TCP host/server with a single client
//   * must have exclusive access to incoming network packets; trying to read
//     packets manually from the network will steal TCP packets and cause errors

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_TCP_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_TCP_H_

#include <stdint.h>
#include <zircon/compiler.h>

#include <efi/boot-services.h>
#include <efi/protocol/service-binding.h>
#include <efi/protocol/tcp6.h>
#include <efi/types.h>

__BEGIN_CDECLS

// This struct is mostly used as an opaque token for callers, they generally
// shouldn't have to use any of the members directly.
typedef struct {
  // Save the boot services table so the caller doesn't need to pass it to
  // each function.
  efi_boot_services* boot_services;

  // The binding protocol used to open the server protocol.
  efi_handle binding_handle;
  efi_service_binding_protocol* binding_protocol;

  // The server protocol for accepting new client connections.
  efi_handle server_handle;
  efi_tcp6_protocol* server_protocol;
  efi_tcp6_close_token server_close_token;
} tcp6_socket;

typedef enum {
  TCP6_RESULT_SUCCESS,       // The operation completed successfully.
  TCP6_RESULT_PENDING,       // The operation is still pending, call again later.
  TCP6_RESULT_DISCONNECTED,  // The operation was cancelled due to disconnect.
  TCP6_RESULT_ERROR          // The operation completed with an error.
} tcp6_result;

// Opens a TCP6 server socket.
//
// This uses the first TCP interface it finds; we may need to improve this for
// devices with multiple TCP interfaces
//
// Call tcp6_close() on this socket when finished.
//
// Args:
//   socket: socket struct to open; must not already be open
//   boot_services: EFI boot services table
//   address: IP6 address to open the server on
//   port: TCP server port to open
//
// Returns:
//   TCP6_RESULT_SUCCESS
//   TCP6_RESULT_ERROR
tcp6_result tcp6_open(tcp6_socket* socket, efi_boot_services* boot_services,
                      const efi_ipv6_addr* address, uint16_t port);

// Closes a TCP socket.
//
// The given |socket| cannot be reused until this function returns
// TCP6_RESULT_SUCCESS.
//
// Returns:
//   TCP6_RESULT_SUCCESS
//   TCP6_RESULT_PENDING
//   TCP6_RESULT_ERROR
tcp6_result tcp6_close(tcp6_socket* socket);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_TCP_H_
