// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_LIB_BINDINGS_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_LIB_BINDINGS_H_

#include <ddk/protocol/serialimpl/async.h>

typedef struct bt_hci_transport_handle* bt_hci_transport_handle_t;

// Start up a new worker thread, returning a handle that can be used to pass messages to that
// thread.
extern "C" zx_status_t bt_hci_transport_start(const char* name, bt_hci_transport_handle_t*);

// Shut down and destroy worker thread, freeing the bt_hci_transport_handle_t afterward.
// bt_hci_transport_handle_t is not valid after this function is called.
//
// The second argument is a timeout in milliseconds. The function will block until
// all the resources associated with the bt_hci_transport_handle_t have been freed or the timeout
// is exceeded, whichever comes first.
extern "C" void bt_hci_transport_shutdown(bt_hci_transport_handle_t, uint64_t timeout_ms);

// Perform clean up procedures in response to a DDK "unbind" message.
//
// The second argument is a timeout in milliseconds. If the timeout is exceeded, a ZX_ERR_TIMEOUT
// status will be returned
extern "C" zx_status_t bt_hci_transport_unbind(const bt_hci_transport_handle_t,
                                               uint64_t timeout_ms);

// bt-hci protocol handlers
//
// The second argument is a timeout in milliseconds. If the timeout is exceeded, a ZX_ERR_TIMEOUT
// status will be returned
extern "C" zx_status_t bt_hci_transport_open_command_channel(const bt_hci_transport_handle_t,
                                                             zx_handle_t cmd_channel,
                                                             uint64_t timeout_ms);
extern "C" zx_status_t bt_hci_transport_open_acl_data_channel(const bt_hci_transport_handle_t,
                                                              zx_handle_t acl_data_channel,
                                                              uint64_t timeout_ms);
extern "C" zx_status_t bt_hci_transport_open_snoop_channel(const bt_hci_transport_handle_t,
                                                           zx_handle_t snoop_channel,
                                                           uint64_t timeout_ms);

// Interact with a uart-based hci transport using the serial implementation directly.
// Note that this function takes ownership of the serial.
//
// |serial| must point to a valid handle for a serial_impl_async_protocol
// implementation. The client is responsible for obtaining that valid handle before calling this
// function to pass ownership of the handle into the library. Generally, the underlying serial
// transport will be a parent device onto which which the client has bound as a child.
//
// The third argument is a timeout in milliseconds. If the timeout is exceeded, a ZX_ERR_TIMEOUT
// status will be returned.
extern "C" zx_status_t bt_hci_transport_open_uart(const bt_hci_transport_handle_t,
                                                  serial_impl_async_protocol_t* serial,
                                                  uint64_t timeout_ms);

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_LIB_BINDINGS_H_
