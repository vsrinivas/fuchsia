// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_ZXIO_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_ZXIO_H_

#include <lib/zxio/types.h>
#include <stdarg.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define ZXIO_EXPORT __EXPORT

// Overview
//
// The zxio library provides an ergonomic C interface to the fuchsia.io family
// of protocols. These protocols are optimized for efficiency at the cost of
// ergonomics. This library provides a more ergonomic interface to the same
// underlying protocol without sacrificing (much) performance.
//
// This library is organized around a zxio object type |zxio_t| defined in
// types.h. A zxio object abstracts over several related protocols (e.g. file,
// directory) to provide a uniform interface. Advanced clients can also provide
// their own implementation of the underlying ops table to provide drop-in
// replacements for zxio objects with different backends.

// Threading model

// Most operations on zxio objects can be called concurrently from any thread.
// However, the caller needs to synchronize |zxio_close| with other operations.
// Specifically, no operations may be called concurrently with |zxio_close| on
// the same zxio object.

// Node

// Creates a new zxio_t object wrapping |handle| into the provided storage.
//
// On success, this function returns ZX_OK and initializes a zxio_t instance in
// |storage->io|. The caller is responsible for calling zxio_close() on this
// object when done with it.
//
// Always consumes |handle|. If zxio does not know how to wrap a handle, returns
// ZX_ERR_NOT_SUPPORTED and initializes a zxio_t into |storage|. The caller
// can extract |handle| with zxio_release() or close the handle and object
// with zxio_close().
//
// In other error cases, consumes |handle| and initializes a null zxio into
// |storage|.
//
// May block to communicate with the server about the state of the object.
ZXIO_EXPORT zx_status_t zxio_create(zx_handle_t handle, zxio_storage_t* storage);

// Like zxio_create for channel objects expecting an incoming
// fuchsia.io.Node/OnOpen event such as from zxio_open_async.
//
// Always consumes |handle|. |handle| must refer to a channel object.
ZXIO_EXPORT zx_status_t zxio_create_with_on_open(zx_handle_t handle, zxio_storage_t* storage);

// Like zxio_create but the caller provides information about the handle.
ZXIO_EXPORT zx_status_t zxio_create_with_info(zx_handle_t handle,
                                              const zx_info_handle_basic_t* handle_info,
                                              zxio_storage_t* storage);

// Creates a new zxio_t object with a known type and type-specific parameters.
//
// On success, this function returns ZX_OK and initializes a zxio_t instance on
// |storage->io|.
//
// Always consumes any known handle parameters for the given type.
//
// C++ callers should use the type safe wrappers in <lib/zxio/cpp/create_with_type.h>
//
// Expected parameters for supported types:
//
//  ZXIO_OBJECT_TYPE_SYNCHRONOUS_DATAGRAM_SOCKET:
//     |zx_handle_t| eventpair associated with the socket
//     |zx_handle_t| channel for the client end of the
//     fuchsia.posix.socket.SynchronousDatagramSocket protocol
//
//  ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET:
//     |zx_handle_t| socket for the data plane of the socket
//     |zx_handle_t| channel for the client end of the fuchsia.posix.socket.DatagramSocket protocol
//     |zx_info_socket_t*| information about the socket
//
//  ZXIO_OBJECT_TYPE_DIR:
//     |zx_handle_t| channel for the client end of the fuchsia.io.Directory protocol
//
//  ZXIO_OBJECT_TYPE_NODE:
//     |zx_handle_t| channel for the client end of the fuchsia.io.Node protocol
//
//  ZXIO_OBJECT_TYPE_STREAM_SOCKET:
//     |zx_handle_t| socket for the data plane of the socket
//     |zx_handle_t| channel for the client end of the fuchsia.posix.socket.StreamSocket protocol
//     |zx_info_socket_t*| information about the socket
//
//  ZXIO_OBJECT_TYPE_PIPE:
//     |zx_handle_t| socket for the data plane of the pipe
//     |zx_info_socket_t*| information about the socket
//
//  ZXIO_OBJECT_TYPE_RAW_SOCKET:
//     |zx_handle_t| eventpair associated with the socket
//     |zx_handle_t| channel for the client end of the fuchsia.posix.socket.raw.Socket protocol
//
//  ZXIO_OBJECT_TYPE_PACKET_SOCKET:
//     |zx_handle_t| eventpair associated with the socket
//     |zx_handle_t| channel for the client end of the fuchsia.posix.socket.packet.Socket protocol
//
//  ZXIO_OBJECT_TYPE_VMO:
//     |zx_handle_t| vmo containing the file contents
//     |zx_handle_t| stream referring to the file contents and offset
zx_status_t zxio_create_with_type(zxio_storage_t* storage, zxio_object_type_t type, ...);

// Attempt to close |io|.
//
// Where applicable, waits for an acknowledgement from the server which may communicate any I/O
// errors.
//
// Always consumes |io|.
ZXIO_EXPORT zx_status_t zxio_close(zxio_t* io);

// Extracts the underlying |zx_handle_t| for |io| if one exists. Does not
// terminate the connection with the server.
//
// Does not block.
//
// Does not consume |io|. However, after this method returns, future I/O on this
// object are guaranteed to return |ZX_ERR_BAD_HANDLE|.
ZXIO_EXPORT zx_status_t zxio_release(zxio_t* io, zx_handle_t* out_handle);

// Access the primary |zx_handle_t| for |io| if one exists.
//
// Does not block.
//
// Does not consume |io| and does not release ownership of |*out_handle|.
//
// The caller is responsible for ensuring that any interactions with
// |*out_handle| are safe with any concurrent operations on |io|.
zx_status_t zxio_borrow(zxio_t* io, zx_handle_t* out_handle);

// Attempt to create a |zx_handle_t| that represents another session with |io|.
//
// The returned |zx_handle_t| is suitable for transfer to another process or for
// use within this process.
//
// Does not block.
//
// Does not consume |io|.
ZXIO_EXPORT zx_status_t zxio_clone(zxio_t* io, zx_handle_t* out_handle);

// Wait for |signals| to be asserted for |io|.
//
// Returns |ZX_ERR_TIMED_OUT| if |deadline| passes before any of the |signals|
// are asserted for |io|. Returns the set of signals that were actually observed
// via |observed|.
ZXIO_EXPORT zx_status_t zxio_wait_one(zxio_t* io, zxio_signals_t signals, zx_time_t deadline,
                                      zxio_signals_t* out_observed);

// Translate |zxio_signals_t| into |zx_signals_t| for |io|.
//
// The client should wait on |handle| for |zx_signals| in order to observe the
// given |zxio_signals|.
//
// Use |zxio_wait_end| to translate the observed |zx_signals_t| back into
// |zxio_signals_t|.
ZXIO_EXPORT void zxio_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                                 zx_signals_t* out_zx_signals);

// Translate |zx_signals_t| into |zxio_signals_t| for |io|.
//
// Typically used with |zxio_wait_begin| to wait asynchronously on a
// |zx_handle_t| and to interpret the observed |zx_signals|.
ZXIO_EXPORT void zxio_wait_end(zxio_t* io, zx_signals_t zx_signals,
                               zxio_signals_t* out_zxio_signals);

// Synchronizes updates to the file to the underlying media, if it exists.
ZXIO_EXPORT zx_status_t zxio_sync(zxio_t* io);

// Returns information about the file.
ZXIO_EXPORT zx_status_t zxio_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr);

// Update information about the file.
//
// The presence of a particular field in |attr| indicates it is to be updated.
ZXIO_EXPORT zx_status_t zxio_attr_set(zxio_t* io, const zxio_node_attributes_t* attr);

// File

// Attempt to read |capacity| bytes into |buffer| at the current seek offset.
//
// The seek offset is moved forward by the actual number of bytes read.
//
// The actual number of bytes read is returned in |out_actual|.
ZXIO_EXPORT zx_status_t zxio_read(zxio_t* io, void* buffer, size_t capacity, zxio_flags_t flags,
                                  size_t* out_actual);

// Attempt to read |capacity| bytes into |buffer| at |offset|.
//
// Does not affect the seek offset.
//
// The actual number of bytes read is returned in |out_actual|.
ZXIO_EXPORT zx_status_t zxio_read_at(zxio_t* io, zx_off_t offset, void* buffer, size_t capacity,
                                     zxio_flags_t flags, size_t* out_actual);

// Attempt to write |capacity| bytes into |buffer| at the current seek offset.
//
// The seek offset is moved forward by the actual number of bytes written.
//
// The actual number of bytes written is returned in |out_actual|.
ZXIO_EXPORT zx_status_t zxio_write(zxio_t* io, const void* buffer, size_t capacity,
                                   zxio_flags_t flags, size_t* out_actual);

// Attempt to write |capacity| bytes into |buffer| at |offset|.
//
// Does not affect the seek offset.
//
// The actual number of bytes written is returned in |out_actual|.
ZXIO_EXPORT zx_status_t zxio_write_at(zxio_t* io, zx_off_t offset, const void* buffer,
                                      size_t capacity, zxio_flags_t flags, size_t* out_actual);

// Attempt to read bytes into the buffers described by |vector|.
//
// The seek offset is moved forward by the actual number of bytes read.
//
// The actual number of bytes read is returned in |out_actual|.
ZXIO_EXPORT zx_status_t zxio_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                   zxio_flags_t flags, size_t* out_actual);

// Attempt to read bytes into the buffers described by |vector| at |offest|.
//
// Does not affect the seek offset.
//
// The actual number of bytes read is returned in |out_actual|.
ZXIO_EXPORT zx_status_t zxio_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                      size_t vector_count, zxio_flags_t flags, size_t* out_actual);

// Attempt to write bytes into the buffers described by |vector|.
//
// The seek offset is moved forward by the actual number of bytes written.
//
// The actual number of bytes written is returned in |out_actual|.
ZXIO_EXPORT zx_status_t zxio_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                    zxio_flags_t flags, size_t* out_actual);

// Attempt to write bytes into the buffers described by |vector| at |offest|.
//
// Does not affect the seek offset.
//
// The actual number of bytes written is returned in |out_actual|.
ZXIO_EXPORT zx_status_t zxio_writev_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                       size_t vector_count, zxio_flags_t flags, size_t* out_actual);

// Modify the seek offset.
//
// The seek offset for the file is modified by |offset| relative to |start|.
//
// The resulting seek offset relative to the start of the file is returned in
// |out_offset|.
ZXIO_EXPORT zx_status_t zxio_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset,
                                  size_t* out_offset);

// Shrink the file size to |length| bytes.
ZXIO_EXPORT zx_status_t zxio_truncate(zxio_t* io, uint64_t length);

// Returns the flags associated with the file.
//
// These flags are typically set when the file is opened but can be modified by
// |zxio_flags_set|.
//
// See fuchsia.io for the available |flags|.
ZXIO_EXPORT zx_status_t zxio_flags_get(zxio_t* io, uint32_t* out_flags);

// Modifies the flags associated with the file.
//
// This function can modify the following flags:
//
//  * |fuchsia::io::OpenFlags::APPEND|.
//
// See fuchsia.io for the available |flags|.
ZXIO_EXPORT zx_status_t zxio_flags_set(zxio_t* io, uint32_t flags);

// Gets a token associated with a directory connection.
//
// This token can be used to identify a directory at a later time, for use
// in operations involving multiple nodes e.g. rename.
//
// See the fuchsia.io documentation on |fuchsia.io/Directory.GetToken|.
ZXIO_EXPORT zx_status_t zxio_token_get(zxio_t* io, zx_handle_t* out_token);

// Acquires a VMO representing this file, if there is one, with the requested
// access rights.
ZXIO_EXPORT zx_status_t zxio_vmo_get(zxio_t* io, zxio_vmo_flags_t flags, zx_handle_t* out_vmo);

// Get a read-only VMO containing the whole contents of the file. This function creates a clone of
// the underlying VMO using `zxio_vmo_get_clone()` when possible, falling back to eagerly reading
// the contents into a freshly-created VMO. Copying the file data can have significant memory and
// performance implications for large files so this function must be used carefully, but having this
// fallback avoids the many error cases of `zxio_vmo_get_clone()` and `zxio_vmo_get_exact()`.
//
// When cloning is successful, the cloned VMO will have at least the semantics of the
// `ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE` flag for the `zx_vmo_create_child()` system call. This
// leaves unspecified whether the returned VMO will reflect subsequent changes in the underlying
// file or not. When the eager-reading fallback happens, the returned VMO will be a non-atomic
// snapshot of the file when it was read. The size of the VMO will not reflect changes to the
// underlying file. As a result, users should not make assumptions about the semantics of the
// returned VMO with respect to file content changes.
//
// See also:
//
//   * `fdio_get_vmo_copy()` which is a frontend for this function taking a file descriptor.
//   * `zxio_vmo_get_clone()`
//   * `zxio_vmo_get_exact()`
//   * `zxio_vmo_get_exec()`
ZXIO_EXPORT zx_status_t zxio_vmo_get_copy(zxio_t* io, zx_handle_t* out_vmo);

// Gets a VMO containing a read-only child of the underlying VMO representing the file.
//
// Zxio supports things other than normal files, and no filesystem is required to provide the
// contents of the file as a VMO. Therefore, callers should be prepared for this function to fail.
// Callers that can tolerate the performance and memory implications of an eager copy of the entire
// contents of the file can use `zxio_vmo_get_copy()` which includes automatic fallback.
//
// On success, the returned VMO will have at least the semantics of the
// `ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE` flag for the `zx_vmo_create_child()` system call. This
// leaves unspecified whether the returned VMO will reflect subsequent changes in the underlying
// file or not. The size of the VMO will not reflect changes to the underlying file. These semantics
// match the POSIX `mmap()` `MAP_PRIVATE` flag.
//
// Users requiring a guaranteed snapshot of the file should use `zxio_vmo_get_exact()` and then use
// `zx_vmo_create_child()` with `ZX_VMO_CHILD_SNAPSHOT` to create a snapshot. However, clients
// should be prepared for this to fail in common cases, both because the filesystem is not required
// to supply an exact VMO, and because creating a snapshot has additional restrictions (most
// commonly that the VMO must not be attached to the paging system).
//
// See also:
//
//   * `fdio_get_vmo_clone()` which is a frontend for this function taking a file descriptor.
//   * `zxio_vmo_get_copy()`
//   * `zxio_vmo_get_exact()`
//   * `zxio_vmo_get_exec()`
ZXIO_EXPORT zx_status_t zxio_vmo_get_clone(zxio_t* io, zx_handle_t* out_vmo);

// Get a read-only handle to the exact VMO used to represent the file. This VMO will track size and
// content changes to the file.
//
// Not all file descriptors represent normal files, and no filesystem is required to provide the
// contents of the file as a VMO. Therefore, callers should be prepared for this function to fail.
// Callers that can tolerate the performance and memory implications of an eager copy of the entire
// contents of the file can use `zxio_vmo_get_copy()` which includes automatic fallback.
//
// See also:
//
//   * `fdio_get_vmo_exact()` which is a frontend for this function taking a file descriptor.
//   * `zxio_vmo_get_clone()`
//   * `zxio_vmo_get_copy()`
//   * `zxio_vmo_get_exec()`
ZXIO_EXPORT zx_status_t zxio_vmo_get_exact(zxio_t* io, zx_handle_t* out_vmo);

// Get VMO containing a read-only executable child of the underlying VMO. This function will fail
// rather than copying the contents if it cannot be cloned.
//
// This function is identical to `zxio_vmo_get_clone()` except it adds executable rights. See that
// function for more information.
//
// See also:
//
//   * `fdio_get_vmo_exec()` which is a frontend for this function taking a file descriptor.
//   * `zxio_vmo_get_clone()`
//   * `zxio_vmo_get_copy()`
//   * `zxio_vmo_get_exact()`
ZXIO_EXPORT zx_status_t zxio_vmo_get_exec(zxio_t* io, zx_handle_t* out_vmo);

// Queries the number of bytes available to read from this object without
// blocking.
ZXIO_EXPORT zx_status_t zxio_get_read_buffer_available(zxio_t* io, size_t* out_available);

// Shuts a given IO object down for reading, writing, or both.
//
// |options| can be any of:
//   ZXIO_SHUTDOWN_OPTIONS_READ - disables reading from this object
//   ZXIO_SHUTDOWN_OPTIONS_WRITE - disables writing to this object
//   ZXIO_SHUTDOWN_OPTIONS_READ | ZXIO_SHUTDOWN_OPTIONS_WRITE  - disables reading and
//     writing for this object.
ZXIO_EXPORT zx_status_t zxio_shutdown(zxio_t* io, zxio_shutdown_options_t options,
                                      int16_t* out_code);

// Directory

// Open a new zxio object relative to the given |directory| and initialize it
// into |storage|.
//
// This call blocks on the remote server.
//
// See fuchsia.io for the available |flags| and |mode|.
ZXIO_EXPORT zx_status_t zxio_open(zxio_t* directory, uint32_t flags, uint32_t mode,
                                  const char* path, size_t path_len, zxio_storage_t* storage);

// Open a new object relative to the given |directory|.
//
// The object's connection is represented as a |zx_handle_t|. The caller is
// responsible for creating the |zx_handle_t|, which must be a channel. This
// call does not block on the remote server.
//
// If the caller specifies the flag fuchsia.io.OPEN_FLAG_DESCRIBE then the
// connection can be wrapped in a zxio object by calling
// zxio_create_with_on_open().
//
// See fuchsia.io for the available |flags| and |mode|.
ZXIO_EXPORT zx_status_t zxio_open_async(zxio_t* directory, uint32_t flags, uint32_t mode,
                                        const char* path, size_t path_len, zx_handle_t request);

// Adds a inotify filter on a file/directory relative to the given |directory|.
// The events on server side are communicated to the client via |socket|.
// The |watch_descriptor| is the unique ID used to identify the watch filters
// by the the client.
//
// See fuchsia.io/io for the available |mask|.
ZXIO_EXPORT zx_status_t zxio_add_inotify_filter(zxio_t* io, const char* path, size_t path_len,
                                                uint32_t mask, uint32_t watch_descriptor,
                                                zx_handle_t socket);

// Remove a file relative to the given directory. |flags| has the same values
// and semantics as POSIX's unlinkat |flags| argument.
ZXIO_EXPORT zx_status_t zxio_unlink(zxio_t* directory, const char* name, size_t name_len,
                                    int flags);

// Attempts to rename |old_path| relative to |old_directory| to |new_path|
// relative to the directory represented by |new_directory_token|.
//
// |old_directory| and |new_directory_token| may be aliased.
ZXIO_EXPORT zx_status_t zxio_rename(zxio_t* old_directory, const char* old_path,
                                    size_t old_path_len, zx_handle_t new_directory_token,
                                    const char* new_path, size_t new_path_len);

// Attempts to link |src_path| relative to |src_directory| to |dst_path| relative to
// the directory represented by |dst_directory_token|.
//
// |src_directory| and |dst_directory_token| may be aliased.
ZXIO_EXPORT zx_status_t zxio_link(zxio_t* src_directory, const char* src_path, size_t src_path_len,
                                  zx_handle_t dst_directory_token, const char* dst_path,
                                  size_t dst_path_len);

// Directory iterator

// Initializes a |zxio_dirent_iterator_t| for the given |directory|.
//
// At most one |zxio_dirent_iterator_t| can be active for a given |directory|
// at a time. The lifetime of |directory| must be greater than that of |iterator|.
//
// The initialized iterator should be destroyed by calling
// |zxio_dirent_iterator_destroy| when no longer used.
ZXIO_EXPORT zx_status_t zxio_dirent_iterator_init(zxio_dirent_iterator_t* iterator,
                                                  zxio_t* directory);

// Read a |zxio_dirent_t| from the given |iterator| into |inout_entry|.
//
// |inout_entry->name| must be initialized to point to a buffer of at least
// ZXIO_MAX_FILENAME bytes. This function does not null terminate
// |inout_entry->name| - refer to the |zxio_dirent_t| documentation in types.h
// for more details.
//
// This function reads |zxio_directory_entry_t| from the server in chunks, but this
// function returns the entries one at a time. When this function crosses into
// a new chunk, the function will block on the remote server to retrieve the
// next chunk.
//
// When there are no more directory entries to enumerate, this function will
// return |ZX_ERR_NOT_FOUND|.
//
// |iterator| must have been previously initialized via
// |zxio_dirent_iterator_init|.
ZXIO_EXPORT zx_status_t zxio_dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                                  zxio_dirent_t* inout_entry);

// Destroys a |zxio_dirent_iterator_t|, freeing associated resources.
//
// After destruction, another |zxio_dirent_iterator_init| call might be made on
// the corresponding directory.
ZXIO_EXPORT void zxio_dirent_iterator_destroy(zxio_dirent_iterator_t* iterator);

// Terminals

// Return in |tty| whether or not |io| represents a TTY object (should
// line buffer for stdio, etc).
ZXIO_EXPORT zx_status_t zxio_isatty(zxio_t* io, bool* tty);

// Gets the window size in characters for the tty in |io|.
//
// Returns ZX_ERR_NOT_SUPPORTED if |io| does not support setting the window size.
ZXIO_EXPORT zx_status_t zxio_get_window_size(zxio_t* io, uint32_t* width, uint32_t* height);

// Sets the window size in characters for the tty in |io|.
//
// Returns ZX_ERR_NOT_SUPPORTED if |io| does not support setting the window size.
ZXIO_EXPORT zx_status_t zxio_set_window_size(zxio_t* io, uint32_t width, uint32_t height);

// Executes the given specific ioctl.
ZXIO_EXPORT zx_status_t zxio_ioctl(zxio_t* io, int request, int16_t* out_code, va_list va);

__END_CDECLS

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_ZXIO_H_
