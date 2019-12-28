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

typedef uint32_t zxio_signals_t;

// These values match the corresponding values in zircon/types.h
#define ZXIO_SIGNAL_NONE ((zxio_signals_t)0u)
#define ZXIO_READABLE ((zxio_signals_t)1u << 0)
#define ZXIO_WRITABLE ((zxio_signals_t)1u << 1)
#define ZXIO_READ_DISABLED ((zxio_signals_t)1u << 4)
#define ZXIO_WRITE_DISABLED ((zxio_signals_t)1u << 5)
#define ZXIO_READ_THRESHOLD ((zxio_signals_t)1u << 10)
#define ZXIO_WRITE_THRESHOLD ((zxio_signals_t)1u << 11)

#define ZXIO_SIGNAL_ALL                                                       \
  (ZXIO_READABLE | ZXIO_WRITABLE | ZXIO_READ_DISABLED | ZXIO_WRITE_DISABLED | \
   ZXIO_READ_THRESHOLD | ZXIO_WRITE_THRESHOLD)

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
