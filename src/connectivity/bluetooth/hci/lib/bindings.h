// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_LIB_BINDINGS_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_LIB_BINDINGS_H_

typedef struct bt_hci_transport_handle* bt_hci_transport_handle_t;

// Start up a new worker thread, returning a handle that can be used to pass messages to that
// thread.
extern "C" zx_status_t bt_hci_transport_start(const char* name, bt_hci_transport_handle_t*);

// Shut down and destroy worker thread, freeing the bt_hci_transport_handle_t afterward.
// bt_hci_transport_handle_t is not a valid pointer after this function is called.
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

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_LIB_BINDINGS_H_
