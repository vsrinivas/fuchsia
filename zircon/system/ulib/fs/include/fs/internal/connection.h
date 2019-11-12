// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_CONNECTION_H_
#define FS_INTERNAL_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fuchsia/io/c/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/event.h>
#include <stdint.h>
#include <zircon/fidl.h>

#include <memory>

#include <fbl/intrusive_double_list.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fs {

constexpr zx_signals_t kLocalTeardownSignal = ZX_USER_SIGNAL_1;

// A one-way message which may be emitted by the server without an
// accompanying request. Optionally used as a part of the Open handshake.
struct OnOpenMsg {
  fuchsia_io_NodeOnOpenEvent primary;
  fuchsia_io_NodeInfo extra;
};

namespace internal {

// Connection represents an open connection to a Vnode (the server-side component
// of a file descriptor). Connections will be managed in a |fbl::DoublyLinkedList|.
// The Vnode's methods will be invoked in response to FIDL protocol messages
// received over the channel.
//
// This class is thread-safe.
class Connection final : public fbl::DoublyLinkedListable<std::unique_ptr<Connection>> {
 public:
  // Create a connection bound to a particular vnode.
  //
  // The VFS will be notified when remote side closes the connection.
  //
  // |vfs| is the VFS which is responsible for dispatching operations to the vnode.
  // |vnode| is the vnode which will handle I/O requests.
  // |channel| is the channel on which the FIDL protocol will be served.
  // |options| are client-specified options for this connection, converted from the
  //           flags and rights passed during the |fuchsia.io/Directory.Open| or
  //           |fuchsia.io/Node.Clone| FIDL call.
  Connection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel channel,
             VnodeConnectionOptions options);

  // Closes the connection.
  //
  // The connection must not be destroyed if its wait handler is running
  // concurrently on another thread.
  //
  // In practice, this means the connection must have already been remotely
  // closed, or it must be destroyed on the wait handler's dispatch thread
  // to prevent a race.
  virtual ~Connection();

  // Set a signal on the channel which causes it to be torn down and
  // closed asynchronously.
  void AsyncTeardown();

  // Explicitly tear down and close the connection synchronously.
  void SyncTeardown();

  // Begins waiting for messages on the channel.
  //
  // Must be called at most once in the lifetime of the connection.
  zx_status_t Serve();

  // Node Operations.
  zx_status_t NodeClone(uint32_t flags, zx_handle_t object);
  zx_status_t NodeClose(fidl_txn_t* txn);
  zx_status_t NodeDescribe(fidl_txn_t* txn);
  zx_status_t NodeSync(fidl_txn_t* txn);
  zx_status_t NodeGetAttr(fidl_txn_t* txn);
  zx_status_t NodeSetAttr(uint32_t flags, const fuchsia_io_NodeAttributes* attributes,
                          fidl_txn_t* txn);
  zx_status_t NodeNodeGetFlags(fidl_txn_t* txn);
  zx_status_t NodeNodeSetFlags(uint32_t flags, fidl_txn_t* txn);

  // File Operations.
  zx_status_t FileRead(uint64_t count, fidl_txn_t* txn);
  zx_status_t FileReadAt(uint64_t count, uint64_t offset, fidl_txn_t* txn);
  zx_status_t FileWrite(const uint8_t* data_data, size_t data_count, fidl_txn_t* txn);
  zx_status_t FileWriteAt(const uint8_t* data_data, size_t data_count, uint64_t offset,
                          fidl_txn_t* txn);
  zx_status_t FileSeek(int64_t offset, fuchsia_io_SeekOrigin start, fidl_txn_t* txn);
  zx_status_t FileTruncate(uint64_t length, fidl_txn_t* txn);
  zx_status_t FileGetFlags(fidl_txn_t* txn);
  zx_status_t FileSetFlags(uint32_t flags, fidl_txn_t* txn);
  zx_status_t FileGetBuffer(uint32_t flags, fidl_txn_t* txn);

  // Directory Operations.
  zx_status_t DirectoryOpen(uint32_t flags, uint32_t mode, const char* path_data, size_t path_size,
                            zx_handle_t object);
  zx_status_t DirectoryUnlink(const char* path_data, size_t path_size, fidl_txn_t* txn);
  zx_status_t DirectoryReadDirents(uint64_t max_out, fidl_txn_t* txn);
  zx_status_t DirectoryRewind(fidl_txn_t* txn);
  zx_status_t DirectoryGetToken(fidl_txn_t* txn);
  zx_status_t DirectoryRename(const char* src_data, size_t src_size, zx_handle_t dst_parent_token,
                              const char* dst_data, size_t dst_size, fidl_txn_t* txn);
  zx_status_t DirectoryLink(const char* src_data, size_t src_size, zx_handle_t dst_parent_token,
                            const char* dst_data, size_t dst_size, fidl_txn_t* txn);
  zx_status_t DirectoryWatch(uint32_t mask, uint32_t options, zx_handle_t watcher, fidl_txn_t* txn);

  // DirectoryAdmin Operations.
  zx_status_t DirectoryAdminMount(zx_handle_t remote, fidl_txn_t* txn);
  zx_status_t DirectoryAdminMountAndCreate(zx_handle_t remote, const char* name, size_t name_size,
                                           uint32_t flags, fidl_txn_t* txn);
  zx_status_t DirectoryAdminUnmount(fidl_txn_t* txn);
  zx_status_t DirectoryAdminUnmountNode(fidl_txn_t* txn);
  zx_status_t DirectoryAdminQueryFilesystem(fidl_txn_t* txn);
  zx_status_t DirectoryAdminGetDevicePath(fidl_txn_t* txn);

 private:
  // Callback for when new signals arrive on the channel, which could be:
  // readable, peer closed, async teardown request, etc.
  void HandleSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);

  // Closes the connection and unregisters it from the VFS object.
  void Terminate(bool call_close);

  // Sends an explicit close message to the underlying vnode.
  // Only necessary if the handler has not returned ERR_DISPATCHER_DONE
  // and has been opened.
  void CallClose();

  // Dispatches incoming FIDL messages.
  //
  // By default, handles the Node, File, Directory and DirectoryAdmin
  // protocols, dispatching to |HandleFsSpecificMessage| if the ordinal is not recognized.
  zx_status_t HandleMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  bool is_open() const { return wait_.object() != ZX_HANDLE_INVALID; }
  void set_closed() { wait_.set_object(ZX_HANDLE_INVALID); }

  fs::Vfs* const vfs_;
  fbl::RefPtr<fs::Vnode> vnode_;

  // Channel on which the connection is being served.
  zx::channel channel_;

  // Asynchronous wait for incoming messages.
  // The object field is |ZX_HANDLE_INVALID| when not actively waiting.
  async::WaitMethod<Connection, &Connection::HandleSignals> wait_;

  // Client-specified connection options containing flags and rights passed during the
  // |fuchsia.io/Directory.Open| or |fuchsia.io/Node.Clone| FIDL call.
  // Permissions on the underlying Vnode are granted on a per-connection basis,
  // and accessible from |options_.rights|.
  // Importantly, rights are hierarchical over Open/Clone. It is never allowed
  // to derive a Connection with more rights than the originating connection.
  VnodeConnectionOptions options_;

  // Handle to event which allows client to refer to open vnodes in multi-path
  // operations (see: link, rename). Defaults to ZX_HANDLE_INVALID.
  // Validated on the server-side using cookies.
  zx::event token_{};

  // Directory cookie for readdir operations.
  fs::vdircookie_t dircookie_{};

  // Current seek offset.
  size_t offset_{};
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_CONNECTION_H_
