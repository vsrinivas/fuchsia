// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_TYPES_H_
#define LIB_ZXIO_TYPES_H_

#include <stdbool.h>
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

// The set of supported representations of a node.
// Refer to |fuchsia.io2/NodeProtocolSet| for the documentation of each item.
typedef uint64_t zxio_node_protocols_t;

#define ZXIO_NODE_PROTOCOL_NONE ((zxio_node_protocols_t)0ul)

#define ZXIO_NODE_PROTOCOL_CONNECTOR ((zxio_node_protocols_t)1ul << 0)
#define ZXIO_NODE_PROTOCOL_DIRECTORY ((zxio_node_protocols_t)1ul << 1)
#define ZXIO_NODE_PROTOCOL_FILE ((zxio_node_protocols_t)1ul << 2)
#define ZXIO_NODE_PROTOCOL_MEMORY ((zxio_node_protocols_t)1ul << 3)
#define ZXIO_NODE_PROTOCOL_POSIX_SOCKET ((zxio_node_protocols_t)1ul << 4)
#define ZXIO_NODE_PROTOCOL_PIPE ((zxio_node_protocols_t)1ul << 5)
#define ZXIO_NODE_PROTOCOL_DEBUGLOG ((zxio_node_protocols_t)1ul << 6)
#define ZXIO_NODE_PROTOCOL_DEVICE ((zxio_node_protocols_t)0x10000000ul)
#define ZXIO_NODE_PROTOCOL_TTY ((zxio_node_protocols_t)0x20000000ul)

#define ZXIO_NODE_PROTOCOL_ALL                                                             \
  (ZXIO_NODE_PROTOCOL_CONNECTOR | ZXIO_NODE_PROTOCOL_DIRECTORY | ZXIO_NODE_PROTOCOL_FILE | \
   ZXIO_NODE_PROTOCOL_MEMORY | ZXIO_NODE_PROTOCOL_POSIX_SOCKET | ZXIO_NODE_PROTOCOL_PIPE | \
   ZXIO_NODE_PROTOCOL_DEBUGLOG | ZXIO_NODE_PROTOCOL_DEVICE | ZXIO_NODE_PROTOCOL_TTY)

typedef uint64_t zxio_node_id_t;

// The kinds of operations behind |zxio_rights_t| and |zxio_abilities_t|.
// Refer to |fuchsia.io2/Operations| for the documentation of each item.
typedef uint64_t zxio_operations_t;

#define ZXIO_OPERATION_NONE ((zxio_operations_t)0ul)

#define ZXIO_OPERATION_CONNECT ((zxio_operations_t)1ul << 0)
#define ZXIO_OPERATION_READ_BYTES ((zxio_operations_t)1ul << 1)
#define ZXIO_OPERATION_WRITE_BYTES ((zxio_operations_t)1ul << 2)
#define ZXIO_OPERATION_EXECUTE ((zxio_operations_t)1ul << 3)
#define ZXIO_OPERATION_GET_ATTRIBUTES ((zxio_operations_t)1ul << 4)
#define ZXIO_OPERATION_UPDATE_ATTRIBUTES ((zxio_operations_t)1ul << 5)
#define ZXIO_OPERATION_ENUMERATE ((zxio_operations_t)1ul << 6)
#define ZXIO_OPERATION_TRAVERSE ((zxio_operations_t)1ul << 7)
#define ZXIO_OPERATION_MODIFY_DIRECTORY ((zxio_operations_t)1ul << 8)
#define ZXIO_OPERATION_ADMIN ((zxio_operations_t)0x100000000000000ul)

#define ZXIO_OPERATION_ALL                                                                     \
  (ZXIO_OPERATION_CONNECT | ZXIO_OPERATION_READ_BYTES | ZXIO_OPERATION_WRITE_BYTES |           \
   ZXIO_OPERATION_EXECUTE | ZXIO_OPERATION_GET_ATTRIBUTES | ZXIO_OPERATION_UPDATE_ATTRIBUTES | \
   ZXIO_OPERATION_ENUMERATE | ZXIO_OPERATION_TRAVERSE | ZXIO_OPERATION_MODIFY_DIRECTORY |      \
   ZXIO_OPERATION_ADMIN)

typedef zxio_operations_t zxio_rights_t;
typedef zxio_operations_t zxio_abilities_t;

// Objective information about a node.
//
// Each field has a corresponding presence indicator. When creating
// a new object, it is desirable to use the ZXIO_NODE_ATTR_SET helper macro
// to set the fields, to avoid forgetting to change the presence indicator.
typedef struct zxio_node_attr {
  // The kinds of representations supported by the node.
  zxio_node_protocols_t protocols;

  // The kinds of operations supported by the node.
  zxio_abilities_t abilities;

  // A filesystem-unique ID.
  zxio_node_id_t id;

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

  // Presence indicator for these fields.
  //
  // If a particular field is absent, it should be set to zero/none,
  // and the corresponding presence indicator will be false.
  // Therefore, a completely empty |zxio_node_attr_t| may be conveniently
  // obtained via value-initialization e.g. `zxio_node_attr_t a = {};`.
  struct has_t {
    bool protocols;
    bool abilities;
    bool id;
    bool content_size;
    bool storage_size;
    bool link_count;
    bool creation_time;
    bool modification_time;

#ifdef __cplusplus
    constexpr bool operator==(const has_t& other) const {
      return protocols == other.protocols && abilities == other.abilities && id == other.id &&
             content_size == other.content_size && storage_size == other.storage_size &&
             link_count == other.link_count && creation_time == other.creation_time &&
             modification_time == other.modification_time;
    }
    constexpr bool operator!=(const has_t& other) const {
      return !(*this == other);
    }
#endif  // _cplusplus
  } has;

#ifdef __cplusplus
  constexpr bool operator==(const zxio_node_attr& other) const {
    if (has != other.has) {
      return false;
    }
    if (has.protocols && (protocols != other.protocols)) {
      return false;
    }
    if (has.abilities && (abilities != other.abilities)) {
      return false;
    }
    if (has.id && (id != other.id)) {
      return false;
    }
    if (has.content_size && (content_size != other.content_size)) {
      return false;
    }
    if (has.storage_size && (storage_size != other.storage_size)) {
      return false;
    }
    if (has.link_count && (link_count != other.link_count)) {
      return false;
    }
    if (has.creation_time && (creation_time != other.creation_time)) {
      return false;
    }
    if (has.modification_time && (modification_time != other.modification_time)) {
      return false;
    }
    return true;
  }
  constexpr bool operator!=(const zxio_node_attr& other) const {
    return !(*this == other);
  }
#endif  // _cplusplus
} zxio_node_attr_t;

#define ZXIO_NODE_ATTR_SET(attr, field_name, value) \
  do {                                              \
    zxio_node_attr_t* _tmp_attr = &(attr);          \
    _tmp_attr->field_name = value;                  \
    _tmp_attr->has.field_name = true;               \
  } while (0)

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
