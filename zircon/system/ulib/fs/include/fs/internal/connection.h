// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_CONNECTION_H_
#define FS_INTERNAL_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
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

namespace internal {

class FidlTransaction;
class Binding;

fit::result<VnodeRepresentation, zx_status_t> Describe(const fbl::RefPtr<Vnode>& vnode,
                                                       VnodeProtocol protocol,
                                                       VnodeConnectionOptions options);

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
  // Closes the connection.
  //
  // The connection must not be destroyed if its wait handler is running
  // concurrently on another thread.
  //
  // In practice, this means the connection must have already been remotely
  // closed, or it must be destroyed on the wait handler's dispatch thread
  // to prevent a race.
  virtual ~Connection();

  // Sets a signal on the channel which causes the dispatcher to asynchronously
  // close, tear down, and unregister this connection from the Vfs object.
  void AsyncTeardown();

  // Explicitly teardown and close the connection synchronously,
  // unregistering it from the Vfs object.
  void SyncTeardown();

  // Begins waiting for messages on the channel.
  // |channel| is the channel on which the FIDL protocol will be served.
  //
  // Before calling this function, the connection ownership must be transferred
  // to the Vfs through |RegisterConnection|.
  // Cannot be called more than once in the lifetime of the connection.
  zx_status_t StartDispatching(zx::channel channel);

  // Drains one FIDL message from the channel and handles it.
  // This should only be called when new messages arrive on the channel.
  // In practice, this implies it should be used by a |Binding|.
  // Returns if the handling succeeded. In event of failure, the caller should
  // synchronously teardown the connection.
  bool OnMessage();

  void RegisterInflightTransaction() { vnode_->RegisterInflightTransaction(); }
  void UnregisterInflightTransaction() { vnode_->UnregisterInflightTransaction(); }

  fbl::RefPtr<fs::Vnode>& vnode() { return vnode_; }

 protected:
  // Subclasses of |Connection| should implement a particular |fuchsia.io| protocol.
  // This is a utility for creating corresponding message dispatch functions which
  // decodes a FIDL message and invokes a handler on |protocol_impl|. In essence, it
  // partially-applies the |impl| argument in the LLCPP |TryDispatch| function.
  class FidlProtocol {
   public:
    // Factory function to create a |FidlProtocol|.
    // |Protocol| should be an LLCPP generated class e.g. |llcpp::fuchsia::io::File|.
    // |protocol_impl| should be the |this| pointer when used from a subclass.
    template <typename Protocol>
    static FidlProtocol Create(typename Protocol::Interface* protocol_impl) {
      return FidlProtocol(static_cast<void*>(protocol_impl),
                          [](void* impl, fidl_incoming_msg_t* msg, fidl::Transaction* txn) {
                            return Protocol::TryDispatch(
                                static_cast<typename Protocol::Interface*>(impl), msg, txn);
                          });
    }

    // Dispatches |message| on |Protocol|.
    // The function consumes the message and returns true if the method was
    // recognized by the protocol. Otherwise, it leaves the message intact
    // and returns false.
    ::fidl::DispatchResult TryDispatch(fidl_incoming_msg_t* message,
                                       fidl::Transaction* transaction) {
      return dispatch_fn_(protocol_impl_, message, transaction);
    }

    FidlProtocol(const FidlProtocol&) = default;
    FidlProtocol(FidlProtocol&&) = default;
    FidlProtocol& operator=(const FidlProtocol&) = delete;
    FidlProtocol& operator=(FidlProtocol&&) = delete;
    ~FidlProtocol() = default;

   private:
    using TypeErasedDispatchFn = ::fidl::DispatchResult (*)(void* impl, fidl_incoming_msg_t*,
                                                            fidl::Transaction*);

    FidlProtocol() = delete;
    FidlProtocol(void* protocol_impl, TypeErasedDispatchFn dispatch_fn)
        : protocol_impl_(protocol_impl), dispatch_fn_(dispatch_fn) {}

    // Pointer to the FIDL protocol implementation.
    // Note that this is not necessarily the address of the |Connection| instance
    // due to multiple inheritance.
    void* const protocol_impl_;

    // The FIDL method dispatch function corresponding to the specific FIDL
    // protocol implemented by a subclass of |Connection|.
    TypeErasedDispatchFn const dispatch_fn_;
  };

  // Create a connection bound to a particular vnode.
  //
  // The VFS will be notified when remote side closes the connection.
  //
  // |vfs| is the VFS which is responsible for dispatching operations to the vnode.
  // |vnode| is the vnode which will handle I/O requests.
  // |protocol| is the (potentially negotiated) vnode protocol that will be used to
  //            interact with the vnode over this connection.
  // |options| are client-specified options for this connection, converted from the
  //           flags and rights passed during the |fuchsia.io/Directory.Open| or
  //           |fuchsia.io/Node.Clone| FIDL call.
  Connection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
             VnodeConnectionOptions options, FidlProtocol fidl_protocol);

  VnodeProtocol protocol() const { return protocol_; }

  const VnodeConnectionOptions& options() const { return options_; }

  void set_append(bool append) { options_.flags.append = append; }

  Vfs* vfs() const { return vfs_; }

  zx::event& token() { return token_; }

  // Flags which can be modified by SetFlags.
  constexpr static uint32_t kSettableStatusFlags = llcpp::fuchsia::io::OPEN_FLAG_APPEND;

  // All flags which indicate state of the connection (excluding rights).
  constexpr static uint32_t kStatusFlags =
      kSettableStatusFlags | llcpp::fuchsia::io::OPEN_FLAG_NODE_REFERENCE;

  // |Result| is a result type used as the return value of the shared Node methods.
  // The |zx_status_t| indicates application error i.e. in case of error, it should be
  // returned via the FIDL method return value. The connection is never closed except
  // from |NodeClose|.
  // Note that |fit::result| has an extra |pending| state apart from |ok| and |error|;
  // the pending state shall never be returned from any of these |Node_*| methods.
  //
  // If the operation is asynchronous, the corresponding function should take in a
  // callback and return |void|.
  template <typename T = void>
  using Result = fit::result<T, zx_status_t>;

  inline Result<> FromStatus(zx_status_t status) {
    if (status == ZX_OK) {
      return fit::ok();
    } else {
      return fit::error(status);
    }
  }

  // Node operations. Note that these provide the shared implementation
  // of |fuchsia.io/Node| methods, used by all connection subclasses.
  //
  // To simplify ownership handling, prefer using the |Vnode_*| types in return
  // values, while using the generated FIDL types in method arguments. This is
  // because return values must recursively own any child objects and handles to
  // avoid a dangling reference.

  void NodeClone(uint32_t flags, zx::channel channel);
  Result<> NodeClose();
  Result<VnodeRepresentation> NodeDescribe();
  void NodeSync(fit::callback<void(zx_status_t)> callback);
  Result<VnodeAttributes> NodeGetAttr();
  Result<> NodeSetAttr(uint32_t flags, const llcpp::fuchsia::io::NodeAttributes& attributes);
  Result<uint32_t> NodeNodeGetFlags();
  Result<> NodeNodeSetFlags(uint32_t flags);

  // Implements |fuchsia.io/DirectoryAdmin.Unmount|.
  void UnmountAndShutdown(fit::callback<void(zx_status_t)> callback);

 private:
  // The contract of the Vnode API is that there should be a balancing |Close| call
  // for every |Open| call made on a vnode.
  // Calls |Close| on the underlying vnode explicitly if necessary.
  zx_status_t EnsureVnodeClosed();

  bool vnode_is_open_;

  // If we have received a |Node.Close| call on this connection.
  bool closing_ = false;

  // The Vfs instance which owns this connection. Connections must not outlive
  // the Vfs, hence this borrowing is safe.
  fs::Vfs* const vfs_;

  fbl::RefPtr<fs::Vnode> vnode_;

  // State related to FIDL message dispatching. See |Binding|.
  std::shared_ptr<Binding> binding_;

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

  // See documentation on |FidlProtocol|.
  FidlProtocol fidl_protocol_;
};

// |Binding| contains state related to FIDL message dispatching.
// After starting FIDL message dispatching, each |Connection| maintains
// one corresponding binding instance in |binding_|. When processing an
// in-flight request, the binding is borrowed via a |std::weak_ptr| by the
// in-flight transaction, and no more message dispatching will happen until
// the transaction goes out of scope, when binding is again exclusively owned
// by the connection.
//
// This object contains an |async::WaitMethod| struct to wait for signals.
class Binding final {
 public:
  Binding(Connection* connection, async_dispatcher_t* dispatcher, zx::channel channel);
  ~Binding();

  Binding(const Binding&) = delete;
  Binding& operator=(const Binding&) = delete;
  Binding(Binding&&) = delete;
  Binding& operator=(Binding&&) = delete;

  // Begins waiting for messages on the channel.
  zx_status_t StartDispatching();

  // Stops waiting for messages on the channel.
  void CancelDispatching();

  // Keeps the |channel_| alive but stops ever waiting for further messages on it.
  // After calling this method, in-progress waits are cancelled, and
  // |StartDispatching| will become a no-op.
  // Useful for halting message dispatch but keeping the ability to respond on the
  // channel, as part of filesystem shutdown.
  void DetachFromConnection();

  void AsyncTeardown();

  zx::channel& channel() { return channel_; }

  void RegisterInflightTransaction() {
    ZX_ASSERT(connection_ != nullptr);
    connection_->RegisterInflightTransaction();
  }
  void UnregisterInflightTransaction() {
    // The only way this condition isn't true is when making a reply to
    // fuchsia.io/DirectoryAdmin.Unmount.
    if (connection_ != nullptr) {
      connection_->UnregisterInflightTransaction();
    }
  }

 private:
  // Callback for when new signals arrive on the channel, which could be:
  // readable, peer closed, async teardown request, etc.
  void HandleSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);

  async::WaitMethod<Binding, &Binding::HandleSignals> wait_;

  // The connection which owns this binding.
  // If the connection object is about to be destroyed but intentionally
  // want the binding to live on, it must invalidate this reference
  // by calling |DetachFromConnection| on the binding.
  Connection* connection_;

  // The dispatcher for reading messages and handling FIDL requests.
  async_dispatcher_t* dispatcher_;

  // Channel on which the connection is being served.
  zx::channel channel_;
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_CONNECTION_H_
