// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_TYPES_H_
#define LIB_ZXIO_TYPES_H_

#include <stdint.h>
#include <zircon/compiler.h>

// This header defines the public types used in the zxio and zxio_ops interface.

__BEGIN_CDECLS

// Flags -----------------------------------------------------------------------

typedef uint32_t zxio_flags_t;

#define ZXIO_PEEK ((zxio_flags_t)1u << 0)

// Signals ---------------------------------------------------------------------

// Signals are single bits of information that reflect some state on the
// I/O object, i.e. they are level-triggered. Signals are implemented under
// the hood using Zircon signals, but they are distinct. One may wait for
// signals using the |zxio_wait_*| set of APIs.
//
// The signals defined here are rather generic (e.g. ZXIO_SIGNAL_READABLE
// applies to both files and sockets); as such, not all I/O objects support
// all signals. Unsupported signals are ignored during waiting.
typedef uint32_t zxio_signals_t;

#define ZXIO_SIGNAL_NONE ((zxio_signals_t)0u)

// Indicates the object is ready for reading.
#define ZXIO_SIGNAL_READABLE ((zxio_signals_t)1u << 0)

// Indicates the object is ready for writing.
#define ZXIO_SIGNAL_WRITABLE ((zxio_signals_t)1u << 1)

// Indicates writing is disabled permanently for the remote endpoint.
// Note that reads on the local endpoint may succeed until all unread data
// have been depleted.
#define ZXIO_SIGNAL_READ_DISABLED ((zxio_signals_t)1u << 2)

// Indicates writing is disabled permanently for the local endpoint.
#define ZXIO_SIGNAL_WRITE_DISABLED ((zxio_signals_t)1u << 3)

// Indicates data queued up on the object for reading exceeds the read threshold.
#define ZXIO_SIGNAL_READ_THRESHOLD ((zxio_signals_t)1u << 4)

// Indicates space available on the object for writing exceeds the write threshold.
#define ZXIO_SIGNAL_WRITE_THRESHOLD ((zxio_signals_t)1u << 5)

// Indicates an out-of-band state transition has occurred that needs attention.
// Primarily used for devices with some out-of-band signalling mechanism.
#define ZXIO_SIGNAL_OUT_OF_BAND ((zxio_signals_t)1u << 6)

// Indicates the object has encountered an error state.
#define ZXIO_SIGNAL_ERROR ((zxio_signals_t)1u << 7)

// Indicates the object has closed the current connection.
// Further I/O may not be performed.
#define ZXIO_SIGNAL_PEER_CLOSED ((zxio_signals_t)1u << 8)

#define ZXIO_SIGNAL_ALL                                                                    \
  (ZXIO_SIGNAL_READABLE | ZXIO_SIGNAL_WRITABLE | ZXIO_SIGNAL_READ_DISABLED |               \
   ZXIO_SIGNAL_WRITE_DISABLED | ZXIO_SIGNAL_READ_THRESHOLD | ZXIO_SIGNAL_WRITE_THRESHOLD | \
   ZXIO_SIGNAL_OUT_OF_BAND | ZXIO_SIGNAL_ERROR | ZXIO_SIGNAL_PEER_CLOSED)

// File and directory access ---------------------------------------------------

// Objective information about a node.
typedef struct zxio_node_attr {
  // Protection bits and node type information.
  uint32_t mode;

  // A filesystem-unique ID.
  uint64_t id;

  // Node size, in bytes.
  uint64_t content_size;

  // Space needed to store the node (possibly larger than size), in bytes.
  uint64_t storage_size;

  // Hard link count.
  uint64_t link_count;

  // Time of creation in nanoseconds since Unix epoch, UTC.
  uint64_t creation_time;

  // Time of last modification in ns since Unix epoch, UTC.
  uint64_t modification_time;
} zxio_node_attr_t;

typedef uint32_t zxio_seek_origin_t;

#define ZXIO_SEEK_ORIGIN_START ((zxio_seek_origin_t)0u)
#define ZXIO_SEEK_ORIGIN_CURRENT ((zxio_seek_origin_t)1u)
#define ZXIO_SEEK_ORIGIN_END ((zxio_seek_origin_t)2u)

// An entry in a directory.
typedef struct zxio_dirent {
  // The inode number of the entry.
  uint64_t inode;

  // The length of the name of the entry.
  uint8_t size;

  // The type of the entry.
  //
  // Aligned with the POSIX d_type values.
  uint8_t type;

  // The name of the entry.
  //
  // This string is not null terminated. Instead, refer to |size| to
  // determine the length of the string.
  char name[0];
} __PACKED zxio_dirent_t;

__END_CDECLS

#endif  // LIB_ZXIO_TYPES_H_
