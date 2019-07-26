// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t stream_id;
  size_t total_length;
} xdc_packet_header_t;

typedef struct {
  xdc_packet_header_t header;
  // Number of bytes received for this packet so far.
  // Once this equals header.total_length, the packet has been fully received.
  size_t bytes_received;
} xdc_packet_state_t;

// Updates the packet state with the read data buffer.
// If out_new_packet is not NULL, it will be populated with whether this
// data buffer starts a new xdc packet and hence contains a header.
//
// Returns ZX_OK if successful, or an error if the data buffer was malformed.
zx_status_t xdc_update_packet_state(xdc_packet_state_t* packet_state, void* data, size_t data_len,
                                    bool* out_new_packet);

#ifdef __cplusplus
}  // extern "C"
#endif
