// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_EXTENSIONS_H_
#define LIB_ZXIO_EXTENSIONS_H_

#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// This header exposes an extension mechanism for clients to inject
// custom implementations of the |zxio_t| interface, for |fuchsia.io|
// node types which are not fully supported by zxio.
//
// Currently, these node types are:
// - |fuchsia.io/DatagramSocket|
// - |fuchsia.io/StreamSocket|

__BEGIN_CDECLS

typedef struct zxio_node zxio_node_t;
typedef struct zxio_extension_ops zxio_extension_ops_t;

// Initializes |node| with a generic |fuchsia.io/Node| protocol support,
// consuming |control| regardless of success or failure.
//
// If not null, |ops| specifies operations to override from the base node
// implementation. See |zxio_extension_ops_t|.
typedef zx_status_t (*zxio_node_init_t)(zxio_node_t* node, zx_handle_t control,
                                        const zxio_extension_ops_t* ops);

// These are node types that zxio does not have full support for, and where an
// external library can inject the full implementation. By default, zxio provides
// support for the |fuchsia.io/Node| portion of the protocol.
//
// In the function arguments, |io| points to an uninitialized storage for the
// new zxio object. The first handle, |control| is always the client end
// of the FIDL channel used when opening a new node. The extra handles usually
// correspond to transport specific logic, and their names denote the expected
// object type.
//
// |node_init| is provided as a convenience if the client wishes to re-use the
// |fuchsia.io/Node| support in zxio. In that case, users will compose |zxio_node_t|
// in their custom |zxio_t| implementation, then call this function to populate
// the node implementation. Doing so also handles picking the fuchsia.io v1/v2
// implementation automatically.
typedef struct zxio_extensions {
  zx_status_t (*datagram_socket_init)(zxio_storage_t* io, zx_handle_t control,
                                      zx_handle_t eventpair, zxio_node_init_t node_init);
  zx_status_t (*stream_socket_init)(zxio_storage_t* io, zx_handle_t control, zx_handle_t socket,
                                    zxio_node_init_t node_init);
} zxio_extensions_t;

// Replaces the extensions table within a |io|. Note that extensions
// are inherited across opening/cloning, and defaults to nullptr.
void zxio_extensions_set(zxio_t* io, const zxio_extensions_t* extensions);

// If applicable, returns which function in |extensions| was used to
// initialize this |zxio_t|. Otherwise, returns 0.
uintptr_t zxio_extensions_get_init_function(const zxio_t* io);

// Building block composed by socket/datagram/etc.
// This is provided for convenience, since the custom transports are expected to
// be largely identical in handling |fuchsia.io/Node| operations.
typedef struct zxio_node {
  zxio_t io;
  uint64_t opaque[2];
} zxio_node_t;

// Apart from |close|, these functions extend the |zxio_node_t| with
// implementations of operations beyond the ones relevant to |fuchsia.io/Node|.
//
// TODO(fxb/45659): We eventually would want to plumb extension support for all
// non-Node methods.
typedef struct zxio_extension_ops {
  // A hook to close any resources held by the custom transport before the
  // node portion is invalidated as part of |zxio_close|.
  //
  // If this entry is |nullptr|, the default behavior is to do nothing for the
  // custom part (i.e. treat them as pure data).
  void (*close)(zxio_node_t* io);

  // Specifies whether running |zxio_close| on this node should call
  // |fuchsia.io/Node.Close| and block.
  // If true, the node will not call the FIDL |Close| method.
  //
  // If a |zxio_extension_ops_t| was not specified when initializing the node,
  // the default behavior is to call |Close| and block.
  bool skip_close_call;

  // The default behavior is returning |ZX_ERR_NOT_SUPPORTED|.
  zx_status_t (*readv)(zxio_node_t* io, const zx_iovec_t* vector, size_t vector_count,
                       zxio_flags_t flags, size_t* out_actual);

  // The default behavior is returning |ZX_ERR_NOT_SUPPORTED|.
  zx_status_t (*writev)(zxio_node_t* io, const zx_iovec_t* vector, size_t vector_count,
                        zxio_flags_t flags, size_t* out_actual);
} zxio_extension_ops_t;

zx_handle_t zxio_node_borrow_channel(const zxio_node_t* node);

__END_CDECLS

#endif  // LIB_ZXIO_EXTENSIONS_H_
