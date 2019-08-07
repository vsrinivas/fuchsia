// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <xdc-server-utils/packet.h>

zx_status_t xdc_update_packet_state(xdc_packet_state_t* packet_state, void* data, size_t data_len,
                                    bool* out_new_packet) {
  // If we've received all the bytes for a packet, this data buffer must be the start
  // of a new xdc packet, and contain the xdc packet header.
  bool new_packet = packet_state->bytes_received >= packet_state->header.total_length;
  if (new_packet) {
    if (data_len < sizeof(xdc_packet_header_t)) {
      fprintf(stderr, "malformed header, only received %zu bytes\n", data_len);
      return ZX_ERR_BAD_STATE;
    }
    memcpy(&packet_state->header, data, sizeof(xdc_packet_header_t));
    packet_state->bytes_received = 0;
  }
  packet_state->bytes_received += data_len;
  if (out_new_packet) {
    *out_new_packet = new_packet;
  }
  return ZX_OK;
}
