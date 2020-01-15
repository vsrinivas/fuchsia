// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_NETWORK_H_
#define SYSROOT_ZIRCON_DEVICE_NETWORK_H_

#include <stdint.h>

// Nomenclature:
// Inbound = Device->Client on Rx, Client->Device on Tx
// Return = Client->Device on Rx, Device->Client on Tx

// Flags and constants are found on the definition of the fuchsia.hardware.network
// FIDL library.

#define NETWORK_DEVICE_DESCRIPTOR_VERSION (1)

// A buffer descriptor, which contains a region of the data VMO that can be used
// to store data plus associated metadata.
// The region of the VMO described by a buffer descriptor has the form:
// | head | data | tail |.
// The three regions have lengths |head_length|, |data_length|, |tail_length| and
// always start |offset| bytes from the start of the data VMO.
// A data-carrying buffer's payload is ALWAYS in the |data| region of the described
// memory space, but the owner of the buffer is always guaranteed (by the FIFO
// contract) to be the sole accessor of the entire allocated region.
// When descriptors are chained using |chain_length| and |nxt|, |head_length| may
// only be different than 0 for the first buffer and |tail_length| may only be
// different than 0 for the last buffer.
typedef struct buffer_descriptor {
  // Frame type, as defined by NetworkDevice FIDL.
  uint8_t frame_type;
  // The number of following descriptors in the linked list started at |next|.
  uint8_t chain_length;
  // The index of the next descriptor to use, ignored if |chain_length| is 0.
  uint16_t nxt;
  // Identifies type of sidecar metadata associated with the buffer. The metadata is written
  // immediately after the buffer_descriptor in the descriptors VMO.
  // |DESC_NO_INFO| describes no extra information.
  uint32_t info_type;

  // Buffer offset in data VMO
  uint64_t offset;

  // Offset of payload in VMO region.
  // For data-carrying buffer, the payload always starts
  // |inner_offset| after the VMO |offset|.
  uint16_t head_length;
  // The number of bytes available at the end of this buffer after |data_length|.
  uint16_t tail_length;
  // Length of data written in VMO buffer, in bytes.
  // Set by Client on Tx and by Device on Rx for data-carrying buffers.
  // For scratch Rx space buffers, |data_length| is the total available space,
  // starting from |inner_offset|.
  uint32_t data_length;

  // Inbound flags, set by Client on Tx and by Server on Rx.
  uint32_t inbound_flags;
  // Return flags, set by Client on Rx and by Server on Tx.
  uint32_t return_flags;
} buffer_descriptor_t;

#endif  // SYSROOT_ZIRCON_DEVICE_NETWORK_H_
