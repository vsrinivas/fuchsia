// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_TYPES_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_TYPES_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/availability.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// This header defines the public types used in the zxio and zxio_ops interface.

__BEGIN_CDECLS

// Flags for read and write operations -----------------------------------------

typedef uint32_t zxio_flags_t;

#define ZXIO_PEEK ((zxio_flags_t)1u << 0)

// Flags for VMO retrieval operations ------------------------------------------
typedef uint32_t zxio_vmo_flags_t;

// Request that the VMO be readable.
#define ZXIO_VMO_READ ((zxio_vmo_flags_t)1u << 0)

// Request that the VMO be writable.
#define ZXIO_VMO_WRITE ((zxio_vmo_flags_t)1u << 1)

// Request that the VMO be executable.
#define ZXIO_VMO_EXECUTE ((zxio_vmo_flags_t)1u << 2)

// Require a copy-on-write clone of the underlying VMO. The request should fail
// if the VMO cannot be cloned. May not be supplied with
// `ZXIO_VMO_SHARED_BUFFER`.
#define ZXIO_VMO_PRIVATE_CLONE ((zxio_vmo_flags_t)1u << 16)

// Require an exact (non-cloned) handle to the underlying VMO. All clients using
// this flag would get a VMO with the same koid. The request should fail if a
// handle to the exact VMO cannot be returned. May not be supplied with
// `ZXIO_VMO_PRIVATE_CLONE`.
#define ZXIO_VMO_SHARED_BUFFER ((zxio_vmo_flags_t)1u << 17)

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

// Objects ---------------------------------------------------------------------

// A zxio object. See zxio.h for documentation on the behaviors and operations
// on zxio objects.
typedef struct zxio_tag {
  uint64_t reserved[4];
} zxio_t;

// Storage for the |zxio_ops_t| implementation.
// All |zxio_t| implementations must fit within this space.
typedef struct zxio_private {
  uint64_t reserved[29];
} zxio_private_t;

// The storage backing a |zxio_t|.
typedef struct zxio_storage {
  zxio_t io;
  zxio_private_t reserved;
} zxio_storage_t;

// Type of a zxio object.
typedef uint32_t zxio_object_type_t;

// Allocates storage for a zxio_t object of a given type.
//
// This function should store a pointer to zxio_storage_t space suitable for an
// object of the given type into |*out_storage| and return ZX_OK.
// If the allocation fails, this should store the null value into |*out_storage|
// and return an error value. Returning a status other than ZX_OK or failing to store
// a non-null value into |*out_storage| are considered allocation failures.
//
// This function may also store additional data related to the allocation in
// |*out_context| which will be returned in functions that use this allocator.
// This can be useful if the allocator is allocating zxio_storage_t within a
// larger allocation to keep track of that allocation.
typedef zx_status_t (*zxio_storage_alloc)(zxio_object_type_t type, zxio_storage_t** out_storage,
                                          void** out_context);

// Prelude sizes for datagram sockets.
typedef struct zxio_datagram_prelude_size {
  const size_t tx;
  const size_t rx;
} zxio_datagram_prelude_size_t;

// clang-format off
#define ZXIO_OBJECT_TYPE_NONE                         ((zxio_object_type_t) 0)
#define ZXIO_OBJECT_TYPE_NODE                         ((zxio_object_type_t) 1)
#define ZXIO_OBJECT_TYPE_DIR                          ((zxio_object_type_t) 2)
#define ZXIO_OBJECT_TYPE_SERVICE                      ((zxio_object_type_t) 3)
#define ZXIO_OBJECT_TYPE_FILE                         ((zxio_object_type_t) 4)
#define ZXIO_OBJECT_TYPE_TTY                          ((zxio_object_type_t) 5)
#define ZXIO_OBJECT_TYPE_VMOFILE                      ((zxio_object_type_t) 6)
#define ZXIO_OBJECT_TYPE_VMO                          ((zxio_object_type_t) 7)
#define ZXIO_OBJECT_TYPE_DEBUGLOG                     ((zxio_object_type_t) 8)
#define ZXIO_OBJECT_TYPE_PIPE                         ((zxio_object_type_t) 9)
#define ZXIO_OBJECT_TYPE_SYNCHRONOUS_DATAGRAM_SOCKET  ((zxio_object_type_t)10)
#define ZXIO_OBJECT_TYPE_STREAM_SOCKET                ((zxio_object_type_t)11)
#define ZXIO_OBJECT_TYPE_RAW_SOCKET                   ((zxio_object_type_t)12)
#define ZXIO_OBJECT_TYPE_PACKET_SOCKET                ((zxio_object_type_t)13)
#define ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET              ((zxio_object_type_t)14)
// clang-format on

// File and directory access ---------------------------------------------------

// The set of supported representations of a node.
// Refer to |fuchsia.io/NodeProtocols| for the documentation of each item.
typedef uint64_t zxio_node_protocols_t;

#define ZXIO_NODE_PROTOCOL_NONE ((zxio_node_protocols_t)0ul)

#define ZXIO_NODE_PROTOCOL_CONNECTOR ((zxio_node_protocols_t)1ul << 0)
#define ZXIO_NODE_PROTOCOL_DIRECTORY ((zxio_node_protocols_t)1ul << 1)
#define ZXIO_NODE_PROTOCOL_FILE ((zxio_node_protocols_t)1ul << 2)

#define ZXIO_NODE_PROTOCOL_ALL                                                             \
  (ZXIO_NODE_PROTOCOL_CONNECTOR | ZXIO_NODE_PROTOCOL_DIRECTORY | ZXIO_NODE_PROTOCOL_FILE | \
   ZXIO_NODE_PROTOCOL_MEMORY | ZXIO_NODE_PROTOCOL_DEVICE)

typedef uint64_t zxio_id_t;

// The kinds of operations behind |zxio_rights_t| and |zxio_abilities_t|.
// Refer to |fuchsia.io/Operations| for the documentation of each item.
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

#define ZXIO_OPERATION_ALL                                                                     \
  (ZXIO_OPERATION_CONNECT | ZXIO_OPERATION_READ_BYTES | ZXIO_OPERATION_WRITE_BYTES |           \
   ZXIO_OPERATION_EXECUTE | ZXIO_OPERATION_GET_ATTRIBUTES | ZXIO_OPERATION_UPDATE_ATTRIBUTES | \
   ZXIO_OPERATION_ENUMERATE | ZXIO_OPERATION_TRAVERSE | ZXIO_OPERATION_MODIFY_DIRECTORY)

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
  zxio_id_t id;

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
  // Therefore, a completely empty |zxio_node_attributes_t| may be conveniently
  // obtained via value-initialization e.g. `zxio_node_attributes_t a = {};`.
  struct zxio_node_attr_has_t {
    bool protocols;
    bool abilities;
    bool id;
    bool content_size;
    bool storage_size;
    bool link_count;
    bool creation_time;
    bool modification_time;

#ifdef __cplusplus
    constexpr bool operator==(const zxio_node_attr_has_t& other) const {
      return protocols == other.protocols && abilities == other.abilities && id == other.id &&
             content_size == other.content_size && storage_size == other.storage_size &&
             link_count == other.link_count && creation_time == other.creation_time &&
             modification_time == other.modification_time;
    }
    constexpr bool operator!=(const zxio_node_attr_has_t& other) const { return !(*this == other); }
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
  constexpr bool operator!=(const zxio_node_attr& other) const { return !(*this == other); }
#endif  // _cplusplus
} zxio_node_attributes_t;

#define ZXIO_NODE_ATTR_SET(attr, field_name, value) \
  do {                                              \
    zxio_node_attributes_t* _tmp_attr = &(attr);    \
    _tmp_attr->field_name = value;                  \
    _tmp_attr->has.field_name = true;               \
  } while (0)

// The zxio_seek_origin_t enum matches zx_stream_seek_origin_t.
typedef uint32_t zxio_seek_origin_t;

#define ZXIO_SEEK_ORIGIN_START ((zxio_seek_origin_t)0u)
#define ZXIO_SEEK_ORIGIN_CURRENT ((zxio_seek_origin_t)1u)
#define ZXIO_SEEK_ORIGIN_END ((zxio_seek_origin_t)2u)

// Directory iterator

// An iterator for |zxio_dirent_t| objects.
//
// To start iterating directory entries, call |zxio_dirent_iterator_init| to
// initialize the contents of the iterator. Then, call
// |zxio_dirent_iterator_next| to advance the iterator.
//
// Please note that this object is relatively large (slightly more than 64 KiB)
// and callers operating on a limited stack or in library code that must be
// conservative with stack usage should allocate this in the heap.
typedef struct zxio_dirent_iterator {
  zxio_t* io;
  uint8_t opaque[ZX_CHANNEL_MAX_MSG_BYTES + 48];
} zxio_dirent_iterator_t;

// Matches fuchsia.io/MAX_FILENAME
#define ZXIO_MAX_FILENAME 255

// An entry in a directory.
typedef struct zxio_dirent {
  // The kinds of representations supported by the node.
  zxio_node_protocols_t protocols;

  // The kinds of operations supported by the node.
  zxio_abilities_t abilities;

  // A filesystem-unique ID.
  zxio_id_t id;

  // Presence indicator for the above fields. Note that the |name| field
  // is never absent.
  //
  // If a particular field is absent, it should be set to zero/none,
  // and the corresponding presence indicator will be false.
  struct zxio_dirent_has_t {
    bool protocols;
    bool abilities;
    bool id;
  } has;

  // The length of the name of the entry.
  uint8_t name_length;

  // Pointer to a buffer containing the name of this entry. This must point to a
  // buffer of at least ZXIO_MAX_FILENAME bytes.
  //
  // This string is not null terminated by the zxio library. |name_length| indicates the
  // length of the string.
  //
  // If this buffer will be passed to code expecting a C-style null terminated string,
  // such as the |d_name| field of a |dirent| struct, callers should allocate a buffer
  // of at least ZXIO_MAX_FILENAME + 1 bytes and write a null terminator after calling
  // zxio_dirent_iterator_next().
  char* name;
} zxio_dirent_t;

#define ZXIO_DIRENT_SET(attr, field_name, value) \
  do {                                           \
    zxio_dirent_t* _tmp_attr = &(attr);          \
    _tmp_attr->field_name = value;               \
    _tmp_attr->has.field_name = true;            \
  } while (0)

typedef uint32_t zxio_shutdown_options_t;

#define ZXIO_SHUTDOWN_OPTIONS_WRITE ((zxio_shutdown_options_t)1ul << 0)
#define ZXIO_SHUTDOWN_OPTIONS_READ ((zxio_shutdown_options_t)1ul << 1)
#define ZXIO_SHUTDOWN_OPTIONS_MASK (ZXIO_SHUTDOWN_OPTIONS_WRITE | ZXIO_SHUTDOWN_OPTIONS_READ)

enum advisory_lock_type {
  ADVISORY_LOCK_SHARED = 0x1,
  ADVISORY_LOCK_EXCLUSIVE = 0x2,
  ADVISORY_LOCK_UNLOCK = 0x4
};

typedef struct advisory_lock_req {
  enum advisory_lock_type type;
  bool wait;
} zxio_advisory_lock_req_t;

// Directory watching

typedef uint32_t zxio_watch_directory_event_t;

// clang-format: off
#define ZXIO_WATCH_EVENT_ADD_FILE ((zxio_watch_directory_event_t)1)
#define ZXIO_WATCH_EVENT_REMOVE_FILE ((zxio_watch_directory_event_t)2)

#define ZXIO_WATCH_EVENT_WAITING ((zxio_watch_directory_event_t)3)
// clang-format: on

typedef zx_status_t (*zxio_watch_directory_cb)(zxio_watch_directory_event_t event, const char* name,
                                               void* context) ZX_AVAILABLE_SINCE(7);

__END_CDECLS

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_TYPES_H_
