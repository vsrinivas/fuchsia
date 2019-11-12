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

void Describe(const fbl::RefPtr<Vnode>& vn, VnodeProtocol protocol, VnodeConnectionOptions options,
              OnOpenMsg* response, zx_handle_t* handle);

void WriteDescribeError(zx::channel channel, zx_status_t status);

// Perform basic flags sanitization.
// Returns false if the flags combination is invalid.
bool PrevalidateFlags(uint32_t flags);

zx_status_t EnforceHierarchicalRights(Rights parent_rights, VnodeConnectionOptions child_options,
                                      VnodeConnectionOptions* out_options);

// Connection is a base class representing an open connection to a Vnode (the server-side
// component of a file descriptor). It contains the logic to synchronize connection
// teardown with the vfs, as well as shared utilities such as connection cloning and
// enforcement of connection rights. Connections will be managed in a
// |fbl::DoublyLinkedList|.
//
// This class does not implement any FIDL generated C++ interfaces per se. Rather, each
// |fuchsia.io/{Node, File, Directory, ...}| protocol is handled by a separate
// corresponding subclass, potentially delegating shared functionalities back here.
//
// The Vnode's methods will be invoked in response to FIDL protocol messages
// received over the channel.
//
// This class is thread-safe.
class Connection : public fbl::DoublyLinkedListable<std::unique_ptr<Connection>> {
 public:
  // Create a connection bound to a particular vnode.
  //
  // The VFS will be notified when remote side closes the connection.
  //
  // |vfs| is the VFS which is responsible for dispatching operations to the vnode.
  // |vnode| is the vnode which will handle I/O requests.
  // |channel| is the channel on which the FIDL protocol will be served.
  // |protocol| is the (potentially negotiated) vnode protocol that will be used to
  //            interact with the vnode over this connection.
  // |options| are client-specified options for this connection, converted from the
  //           flags and rights passed during the |fuchsia.io/Directory.Open| or
  //           |fuchsia.io/Node.Clone| FIDL call.
  Connection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel channel,
             VnodeProtocol protocol, VnodeConnectionOptions options);

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
  zx_status_t StartDispatching();

 protected:
  // Callback to dispatch incoming FIDL messages.
  virtual zx_status_t HandleMessage(fidl_msg_t* msg, fidl_txn_t* txn) = 0;

  VnodeProtocol protocol() const { return protocol_; }

  const VnodeConnectionOptions& options() const { return options_; }

  void set_append(bool append) { options_.flags.append = append; }

  Vfs* vfs() const { return vfs_; }

  fbl::RefPtr<fs::Vnode>& vnode() { return vnode_; }

  zx::event& token() { return token_; }

  // Flags which can be modified by SetFlags.
  constexpr static uint32_t kSettableStatusFlags = fuchsia_io_OPEN_FLAG_APPEND;

  // All flags which indicate state of the connection (excluding rights).
  constexpr static uint32_t kStatusFlags =
      kSettableStatusFlags | fuchsia_io_OPEN_FLAG_NODE_REFERENCE;

  // Closes the connection and unregisters it from the VFS object.
  void Terminate(bool call_close);

  // Implements |fuchsia.io/DirectoryAdmin.Unmount|.
  zx_status_t UnmountAndShutdown(fidl_txn_t* txn);

  // Node operations. Note that these provide the shared implementation
  // of |fuchsia.io/Node| methods, used by all connection subclasses.

  zx_status_t NodeClone(uint32_t flags, zx_handle_t object);
  zx_status_t NodeClose(fidl_txn_t* txn);
  zx_status_t NodeDescribe(fidl_txn_t* txn);
  zx_status_t NodeSync(fidl_txn_t* txn);
  zx_status_t NodeGetAttr(fidl_txn_t* txn);
  zx_status_t NodeSetAttr(uint32_t flags, const fuchsia_io_NodeAttributes* attributes,
                          fidl_txn_t* txn);
  zx_status_t NodeNodeGetFlags(fidl_txn_t* txn);
  zx_status_t NodeNodeSetFlags(uint32_t flags, fidl_txn_t* txn);

 private:
  // Callback for when new signals arrive on the channel, which could be:
  // readable, peer closed, async teardown request, etc.
  void HandleSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);

  // Sends an explicit close message to the underlying vnode.
  // Only necessary if the handler has not returned ERR_DISPATCHER_DONE
  // and has been opened.
  void CallClose();

  bool is_open() const { return wait_.object() != ZX_HANDLE_INVALID; }
  void set_closed() { wait_.set_object(ZX_HANDLE_INVALID); }

  fs::Vfs* const vfs_;
  fbl::RefPtr<fs::Vnode> vnode_;

  // Channel on which the connection is being served.
  zx::channel channel_;

  // Asynchronous wait for incoming messages.
  // The object field is |ZX_HANDLE_INVALID| when not actively waiting.
  async::WaitMethod<Connection, &Connection::HandleSignals> wait_;

  // The operational protocol that is used to interact with the vnode over this connection.
  // It provides finer grained information than the FIDL protocol, e.g. both a regular file
  // and a vmo-file could speak |fuchsia.io/File|.
  VnodeProtocol protocol_;

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
  zx::event token_ = {};
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_CONNECTION_H_
