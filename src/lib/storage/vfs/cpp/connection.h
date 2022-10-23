// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_CONNECTION_H_
#define SRC_LIB_STORAGE_VFS_CPP_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/result.h>
#include <zircon/fidl.h>

#include <memory>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fs {

constexpr zx_signals_t kLocalTeardownSignal = ZX_USER_SIGNAL_1;

namespace internal {

class FidlTransaction;
class Binding;

zx::result<VnodeRepresentation> Describe(const fbl::RefPtr<Vnode>& vnode, VnodeProtocol protocol,
                                         VnodeConnectionOptions options);

// Perform basic flags sanitization.
// Returns false if the flags combination is invalid.
bool PrevalidateFlags(fuchsia_io::wire::OpenFlags flags);

zx_status_t EnforceHierarchicalRights(Rights parent_rights, VnodeConnectionOptions child_options,
                                      VnodeConnectionOptions* out_options);

// Connection is a base class representing an open connection to a Vnode (the server-side component
// of a file descriptor). It contains the logic to synchronize connection teardown with the vfs, as
// well as shared utilities such as connection cloning and enforcement of connection rights.
// Connections will be managed in a |fbl::DoublyLinkedList|.
//
// This class does not implement any FIDL generated C++ interfaces per se. Rather, each
// |fuchsia.io/{Node, File, Directory, ...}| protocol is handled by a separate corresponding
// subclass, potentially delegating shared functionalities back here.
//
// The Vnode's methods will be invoked in response to FIDL protocol messages received over the
// channel.
//
// This class is thread-safe.
class Connection : public fbl::DoublyLinkedListable<std::unique_ptr<Connection>> {
 public:
  // Closes the connection.
  //
  // The connection must not be destroyed if its wait handler is running concurrently on another
  // thread.
  //
  // In practice, this means the connection must have already been remotely closed, or it must be
  // destroyed on the wait handler's dispatch thread to prevent a race.
  virtual ~Connection();

  // Sets a signal on the channel which causes the dispatcher to asynchronously close, tear down,
  // and unregister this connection from the Vfs object.
  void AsyncTeardown();

  // Explicitly teardown and close the connection synchronously, unregistering it from the Vfs
  // object.
  void SyncTeardown();

  // Begins waiting for messages on the channel. |channel| is the channel on which the FIDL protocol
  // will be served.
  //
  // Before calling this function, the connection ownership must be transferred to the Vfs through
  // |RegisterConnection|. Cannot be called more than once in the lifetime of the connection.
  zx_status_t StartDispatching(zx::channel channel);

  // Drains one FIDL message from the channel and handles it. This should only be called when new
  // messages arrive on the channel. In practice, this implies it should be used by a |Binding|.
  // Returns if the handling succeeded. In event of failure, the caller should synchronously
  // teardown the connection.
  bool OnMessage();

  void RegisterInflightTransaction() { vnode_->RegisterInflightTransaction(); }
  void UnregisterInflightTransaction() { vnode_->UnregisterInflightTransaction(); }

  // For AdvisoryLocking - the KOID of the incoming FIDL channel acts as the identifier
  // (or owner) for the remote file or directory.
  zx_koid_t GetChannelOwnerKoid();

  fbl::RefPtr<fs::Vnode>& vnode() { return vnode_; }

 protected:
  // Subclasses of |Connection| should implement a particular |fuchsia.io| protocol. This is a
  // utility for creating corresponding message dispatch functions which decodes a FIDL message and
  // invokes a handler on |protocol_impl|. In essence, it partially-applies the |impl| argument in
  // the LLCPP |TryDispatch| function.
  class FidlProtocol {
   public:
    // Factory function to create a |FidlProtocol|.
    // |Protocol| should be an LLCPP generated class e.g. |fuchsia_io::File|.
    // |protocol_impl| should be the |this| pointer when used from a subclass.
    template <typename Protocol>
    static FidlProtocol Create(typename fidl::WireServer<Protocol>* protocol_impl) {
      return FidlProtocol(
          static_cast<void*>(protocol_impl),
          [](void* impl, fidl::IncomingHeaderAndMessage& msg, fidl::Transaction* txn) {
            return fidl::WireTryDispatch<Protocol>(
                static_cast<typename fidl::WireServer<Protocol>*>(impl), msg, txn);
          });
    }

    // Dispatches |message| on |Protocol|. The function consumes the message and returns true if the
    // method was recognized by the protocol. Otherwise, it leaves the message intact and returns
    // false.
    ::fidl::DispatchResult TryDispatch(fidl::IncomingHeaderAndMessage& message,
                                       fidl::Transaction* transaction) {
      return dispatch_fn_(protocol_impl_, message, transaction);
    }

    FidlProtocol(const FidlProtocol&) = default;
    FidlProtocol(FidlProtocol&&) = default;
    FidlProtocol& operator=(const FidlProtocol&) = delete;
    FidlProtocol& operator=(FidlProtocol&&) = delete;
    ~FidlProtocol() = default;

   private:
    using TypeErasedDispatchFn = ::fidl::DispatchResult (*)(void* impl,
                                                            fidl::IncomingHeaderAndMessage&,
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
  // |protocol| is the (potentially negotiated) vnode protocol that will be used to interact with
  //            the vnode over this connection.
  // |options| are client-specified options for this connection, converted from the flags and
  //           rights passed during the |fuchsia.io/Directory.Open| or |fuchsia.io/Node.Clone| FIDL
  //           call.
  Connection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
             VnodeConnectionOptions options, FidlProtocol fidl_protocol);

  VnodeProtocol protocol() const { return protocol_; }

  const VnodeConnectionOptions& options() const { return options_; }

  void set_append(bool append) { options_.flags.append = append; }

  FuchsiaVfs* vfs() const { return vfs_; }

  zx::event& token() { return token_; }

  virtual void OnTeardown() {}

  // Flags which can be modified by SetFlags.
  constexpr static fuchsia_io::wire::OpenFlags kSettableStatusFlags =
      fuchsia_io::wire::OpenFlags::kAppend;

  // All flags which indicate state of the connection (excluding rights).
  constexpr static fuchsia_io::wire::OpenFlags kStatusFlags =
      kSettableStatusFlags | fuchsia_io::wire::OpenFlags::kNodeReference;

  // Node operations. Note that these provide the shared implementation of |fuchsia.io/Node|
  // methods, used by all connection subclasses.
  //
  // To simplify ownership handling, prefer using the |Vnode_*| types in return values, while using
  // the generated FIDL types in method arguments. This is because return values must recursively
  // own any child objects and handles to avoid a dangling reference.

  void NodeClone(fuchsia_io::wire::OpenFlags flags, fidl::ServerEnd<fuchsia_io::Node> server_end);
  zx::result<> NodeClose();
  fidl::VectorView<uint8_t> NodeQuery();
  zx::result<VnodeRepresentation> NodeDescribe();
  void NodeSync(fit::callback<void(zx_status_t)> callback);
  zx::result<VnodeAttributes> NodeGetAttr();
  zx::result<> NodeSetAttr(fuchsia_io::wire::NodeAttributeFlags flags,
                           const fuchsia_io::wire::NodeAttributes& attributes);
  zx::result<fuchsia_io::wire::OpenFlags> NodeGetFlags();
  zx::result<> NodeSetFlags(fuchsia_io::wire::OpenFlags flags);
  zx::result<fuchsia_io::wire::FilesystemInfo> NodeQueryFilesystem();

 private:
  // The contract of the Vnode API is that there should be a balancing |Close| call for every |Open|
  // call made on a vnode. Calls |Close| on the underlying vnode explicitly if necessary.
  zx_status_t EnsureVnodeClosed();

  bool vnode_is_open_;

  // If we have received a |Node.Close| call on this connection.
  bool closing_ = false;

  // The Vfs instance which owns this connection. Connections must not outlive the Vfs, hence this
  // borrowing is safe.
  fs::FuchsiaVfs* const vfs_;

  fbl::RefPtr<fs::Vnode> vnode_;

  // State related to FIDL message dispatching. See |Binding|.
  std::shared_ptr<Binding> binding_;

  // The operational protocol that is used to interact with the vnode over this connection. It
  // provides finer grained information than the FIDL protocol, e.g. both a regular file and a
  // vmo-file could speak |fuchsia.io/File|.
  VnodeProtocol protocol_;

  // Client-specified connection options containing flags and rights passed during the
  // |fuchsia.io/Directory.Open| or |fuchsia.io/Node.Clone| FIDL call. Permissions on the underlying
  // Vnode are granted on a per-connection basis, and accessible from |options_.rights|.
  // Importantly, rights are hierarchical over Open/Clone. It is never allowed to derive a
  // Connection with more rights than the originating connection.
  VnodeConnectionOptions options_;

  // Handle to event which allows client to refer to open vnodes in multi-path operations (see:
  // link, rename). Defaults to ZX_HANDLE_INVALID. Validated on the server-side using cookies.
  zx::event token_ = {};

  // See documentation on |FidlProtocol|.
  FidlProtocol fidl_protocol_;
};

// |Binding| contains state related to FIDL message dispatching. After starting FIDL message
// dispatching, each |Connection| maintains one corresponding binding instance in |binding_|. When
// processing an in-flight request, the binding is borrowed via a |std::weak_ptr| by the in-flight
// transaction, and no more message dispatching will happen until the transaction goes out of scope,
// when binding is again exclusively owned by the connection.
//
// This object contains an |async::WaitMethod| struct to wait for signals.
class Binding final {
 public:
  Binding(Connection& connection, async_dispatcher_t* dispatcher, zx::channel channel);
  ~Binding();

  Binding(const Binding&) = delete;
  Binding& operator=(const Binding&) = delete;
  Binding(Binding&&) = delete;
  Binding& operator=(Binding&&) = delete;

  // Begins waiting for messages on the channel.
  zx_status_t StartDispatching();

  // Stops waiting for messages on the channel.
  void CancelDispatching();

  void AsyncTeardown();

  zx::channel& channel() { return channel_; }

  void RegisterInflightTransaction() { connection_.RegisterInflightTransaction(); }
  void UnregisterInflightTransaction() { connection_.UnregisterInflightTransaction(); }

 private:
  // Callback for when new signals arrive on the channel, which could be: readable, peer closed,
  // async teardown request, etc.
  void HandleSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);

  async::WaitMethod<Binding, &Binding::HandleSignals> wait_;

  // The connection which owns this binding.
  Connection& connection_;

  // The dispatcher for reading messages and handling FIDL requests.
  async_dispatcher_t* dispatcher_;

  // Channel on which the connection is being served.
  zx::channel channel_;
};

}  // namespace internal

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_CONNECTION_H_
