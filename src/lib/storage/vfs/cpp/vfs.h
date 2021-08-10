// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_VFS_H_
#define SRC_LIB_STORAGE_VFS_CPP_VFS_H_

#include <lib/fdio/vfs.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>
#include <set>
#include <string_view>
#include <utility>
#include <variant>

#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

#ifdef __Fuchsia__
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/io.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>

#include <fbl/intrusive_hash_table.h>

#include "src/lib/storage/vfs/cpp/mount_channel.h"
#endif  // __Fuchsia__

#include <memory>
#include <string_view>
#include <utility>
#include <variant>

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace fs {
#ifdef __Fuchsia__
namespace fio2 = fuchsia_io2;
#endif
namespace internal {

class Connection;

}  // namespace internal

class Vnode;

// A storage class for a vdircookie which is passed to Readdir. Common vnode implementations may use
// this struct as scratch space, or cast it to an alternative structure of the same size (or
// smaller).
//
// TODO(smklein): To implement seekdir and telldir, the size of this vdircookie may need to shrink
// to a 'long'.
struct VdirCookie {
  uint64_t n = 0;
  void* p = nullptr;
};

// The Vfs object contains global per-filesystem state, which may be valid across a collection of
// Vnodes. It dispatches requests to per-file/directory Vnode objects.
//
// This class can be used on a Fuchsia system or on the host computer where the compilation is done
// (the host builds of the filesystems are how system images are created). Normally Fuchsia builds
// will use the ManagedVfs subclass which handles the FIDL-to-vnode connections.
//
// The Vfs object must outlive the Vnodes which it serves. This class is thread-safe.
class Vfs {
 public:
  class OpenResult;
  class TraversePathResult;

  Vfs();

#ifdef __Fuchsia__
  explicit Vfs(async_dispatcher_t* dispatcher);
#endif  // __Fuchsia__

  virtual ~Vfs();

  // Traverse the path to the target vnode, and create / open it using the underlying filesystem
  // functions (lookup, create, open).
  //
  // The return value will suggest the next action to take. Refer to the variants in |OpenResult|
  // for more information.
  OpenResult Open(fbl::RefPtr<Vnode> vn, std::string_view path, VnodeConnectionOptions options,
                  Rights parent_rights, uint32_t mode) __TA_EXCLUDES(vfs_lock_);
  zx_status_t Unlink(fbl::RefPtr<Vnode> vn, std::string_view path, bool must_be_dir)
      __TA_EXCLUDES(vfs_lock_);
  zx_status_t Unlink(fbl::RefPtr<Vnode> vndir, std::string_view path) __TA_EXCLUDES(vfs_lock_);

  // Sets whether this file system is read-only.
  void SetReadonly(bool value) __TA_EXCLUDES(vfs_lock_);

  // Used for inotify filter addition to traverse a vnode, without actually
  // opening it.
  TraversePathResult TraversePathFetchVnode(fbl::RefPtr<Vnode> vndir, std::string_view path)
      __TA_EXCLUDES(vfs_lock_);
#ifdef __Fuchsia__
  using ShutdownCallback = fit::callback<void(zx_status_t status)>;
  using CloseAllConnectionsForVnodeCallback = fit::callback<void()>;

  // Unmounts the underlying filesystem. The result of shutdown is delivered via calling |closure|.
  //
  // |Shutdown| may be synchronous or asynchronous. The closure may be invoked before or after
  // |Shutdown| returns.
  virtual void Shutdown(ShutdownCallback closure) = 0;

  // Identifies if the filesystem is in the process of terminating. May be checked by active
  // connections, which, upon reading new port packets, should ignore them and close immediately.
  virtual bool IsTerminating() const = 0;

  void TokenDiscard(zx::event ios_token) __TA_EXCLUDES(vfs_lock_);
  zx_status_t VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token, zx::event* out)
      __TA_EXCLUDES(vfs_lock_);
  zx_status_t Link(zx::event token, fbl::RefPtr<Vnode> oldparent, std::string_view oldStr,
                   std::string_view newStr) __TA_EXCLUDES(vfs_lock_);
  zx_status_t Rename(zx::event token, fbl::RefPtr<Vnode> oldparent, std::string_view oldStr,
                     std::string_view newStr) __TA_EXCLUDES(vfs_lock_);
  // Calls readdir on the Vnode while holding the vfs_lock, preventing path modification operations
  // for the duration of the operation.
  zx_status_t Readdir(Vnode* vn, VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual)
      __TA_EXCLUDES(vfs_lock_);

  async_dispatcher_t* dispatcher() const { return dispatcher_; }

  void SetDispatcher(async_dispatcher_t* dispatcher);

  // Begins serving VFS messages over the specified channel. If the vnode supports multiple
  // protocols and the client requested more than one of them, it would use |Vnode::Negotiate| to
  // tie-break and obtain the resulting protocol.
  zx_status_t Serve(fbl::RefPtr<Vnode> vnode, fidl::ServerEnd<fuchsia_io::Node> server_end,
                    VnodeConnectionOptions options) __TA_EXCLUDES(vfs_lock_);

  // Begins serving VFS messages over the specified channel. This version takes an |options|
  // that have been validated.
  zx_status_t Serve(fbl::RefPtr<Vnode> vnode, fidl::ServerEnd<fuchsia_io::Node> server_end,
                    Vnode::ValidatedOptions options) __TA_EXCLUDES(vfs_lock_);

  // Adds a inotify filter to the vnode.
  zx_status_t AddInotifyFilterToVnode(fbl::RefPtr<Vnode> vnode, const fbl::RefPtr<Vnode>& parent,
                                      fio2::wire::InotifyWatchMask filter,
                                      uint32_t watch_descriptor, zx::socket socket)
      __TA_EXCLUDES(vfs_lock_);

  // Called by a VFS connection when it is closed remotely. The VFS is now responsible for
  // destroying the connection.
  void OnConnectionClosedRemotely(internal::Connection* connection) __TA_EXCLUDES(vfs_lock_);

  // Serves a Vnode over the specified channel (used for creating new filesystems); the Vnode must
  // be a directory.
  zx_status_t ServeDirectory(fbl::RefPtr<Vnode> vn,
                             fidl::ServerEnd<fuchsia_io::Directory> server_end, Rights rights);

  // Convenience wrapper over |ServeDirectory| with maximum rights.
  zx_status_t ServeDirectory(fbl::RefPtr<Vnode> vn,
                             fidl::ServerEnd<fuchsia_io::Directory> server_end) {
    return ServeDirectory(vn, std::move(server_end), fs::Rights::All());
  }

  // Closes all connections to a Vnode and calls |callback| after all connections are closed. The
  // caller must ensure that no new connections or transactions are created during this point.
  virtual void CloseAllConnectionsForVnode(const Vnode& node,
                                           CloseAllConnectionsForVnodeCallback callback) = 0;

  // Pins a handle to a remote filesystem onto a vnode, if possible.
  zx_status_t InstallRemote(fbl::RefPtr<Vnode> vn, MountChannel h) __TA_EXCLUDES(vfs_lock_);

  // Create and mount a directory with a provided name
  zx_status_t MountMkdir(fbl::RefPtr<Vnode> vn, std::string_view name, MountChannel h,
                         uint32_t flags) __TA_EXCLUDES(vfs_lock_);

  // Unpin a handle to a remote filesystem from a vnode, if one exists.
  zx_status_t UninstallRemote(fbl::RefPtr<Vnode> vn, fidl::ClientEnd<fuchsia_io::Directory>* h)
      __TA_EXCLUDES(vfs_lock_);

  // Forwards an open request to a remote handle. If the remote handle is closed (handing off
  // returns ZX_ERR_PEER_CLOSED), it is automatically unmounted.
  zx_status_t ForwardOpenRemote(fbl::RefPtr<Vnode> vn, fidl::ServerEnd<fuchsia_io::Node> channel,
                                std::string_view path, VnodeConnectionOptions options,
                                uint32_t mode) __TA_EXCLUDES(vfs_lock_);

  // Unpins all remote filesystems in the current filesystem, and waits for the response of each one
  // with the provided deadline.
  zx_status_t UninstallAll(zx::time deadline) __TA_EXCLUDES(vfs_lock_);

  // Shuts down a remote filesystem, by sending a |fuchsia.io/DirectoryAdmin.Unmount| request to the
  // filesystem serving |handle| and awaits a response. |deadline| is the deadline for waiting for
  // response.
  static zx_status_t UnmountHandle(fidl::ClientEnd<fuchsia_io::DirectoryAdmin> handle,
                                   zx::time deadline);

  bool IsTokenAssociatedWithVnode(zx::event token) __TA_EXCLUDES(vfs_lock_);
#endif

  // The VFS tracks all live Vnodes associated with it.
  void RegisterVnode(Vnode* vnode);
  void UnregisterVnode(Vnode* vnode);

 protected:
  // Whether this file system is read-only.
  bool ReadonlyLocked() const __TA_REQUIRES(vfs_lock_) { return readonly_; }

  // Derived classes may want to unregister vnodes differently than this one. This function removes
  // the vnode from the live node map.
  void UnregisterVnodeLocked(Vnode* vnode) __TA_REQUIRES(live_nodes_lock_);

  // A lock which should be used to protect lookup and walk operations
  mutable std::mutex vfs_lock_;

  // A separate lock to protected vnode registration. The vnodes will call into this class according
  // to their lifetimes, and many of these lifetimes are managed from within the VFS lock which can
  // result in reentrant locking. This lock should only be held for very short times when mutating
  // the registered node tracking information.
  mutable std::mutex live_nodes_lock_;

 private:
  // Starting at vnode |vn|, walk the tree described by the path string, until either there is only
  // one path segment remaining in the string or we encounter a vnode that represents a remote
  // filesystem
  //
  // On success,
  // |out| is the vnode at which we stopped searching.
  // |pathout| is the remainder of the path to search.
  zx_status_t Walk(fbl::RefPtr<Vnode> vn, std::string_view path, fbl::RefPtr<Vnode>* out,
                   std::string_view* pathout) __TA_REQUIRES(vfs_lock_);

  OpenResult OpenLocked(fbl::RefPtr<Vnode> vn, std::string_view path,
                        VnodeConnectionOptions options, Rights parent_rights, uint32_t mode)
      __TA_REQUIRES(vfs_lock_);

  TraversePathResult TraversePathFetchVnodeLocked(fbl::RefPtr<Vnode> vndir, std::string_view path)
      __TA_REQUIRES(vfs_lock_);
  // Attempt to create an entry with name |name| within the |vndir| directory.
  //
  // - Upon success, returns a reference to the new vnode via |out_vn|, and return ZX_OK.
  // - Upon recoverable error (e.g. target already exists but |options| did not specify this to be
  //   fatal), attempt to lookup the vnode.
  //
  // In the above two cases, |did_create| will be updated to indicate if an entry was created.
  // Otherwise, a corresponding error code is returned.
  zx_status_t EnsureExists(fbl::RefPtr<Vnode> vndir, std::string_view name,
                           fbl::RefPtr<Vnode>* out_vn, fs::VnodeConnectionOptions options,
                           uint32_t mode, Rights parent_rights, bool* did_create)
      __TA_REQUIRES(vfs_lock_);

  bool readonly_ = false;

  // The live Vnodes associated with this Vfs. Nodes (un)register using [Un]RegisterVnode(). This
  // /list is cleared by ShutdownLiveNodes().
  std::set<Vnode*> live_nodes_ __TA_GUARDED(live_nodes_lock_);

#ifdef __Fuchsia__
  zx_status_t TokenToVnode(zx::event token, fbl::RefPtr<Vnode>* out) __TA_REQUIRES(vfs_lock_);
  zx_status_t InstallRemoteLocked(fbl::RefPtr<Vnode> vn, MountChannel h) __TA_REQUIRES(vfs_lock_);
  zx_status_t UninstallRemoteLocked(fbl::RefPtr<Vnode> vn,
                                    fidl::ClientEnd<fuchsia_io::Directory>* h)
      __TA_REQUIRES(vfs_lock_);

  fbl::HashTable<zx_koid_t, std::unique_ptr<VnodeToken>> vnode_tokens_;

  // Non-intrusive node in linked list of vnodes acting as mount points
  class MountNode final : public fbl::DoublyLinkedListable<std::unique_ptr<MountNode>> {
   public:
    using ListType = fbl::DoublyLinkedList<std::unique_ptr<MountNode>>;
    constexpr MountNode();
    ~MountNode();

    void SetNode(fbl::RefPtr<Vnode> vn);
    fidl::ClientEnd<fuchsia_io::Directory> ReleaseRemote();
    bool VnodeMatch(fbl::RefPtr<Vnode> vn) const;

   private:
    fbl::RefPtr<Vnode> vn_;
  };

  // The mount list is a global static variable, but it only uses constexpr constructors during
  // initialization. As a consequence, the .init_array section of the compiled vfs-mount object file
  // is empty; "remote_list" is a member of the bss section.
  MountNode::ListType remote_list_ __TA_GUARDED(vfs_lock_){};

  async_dispatcher_t* dispatcher_ = nullptr;

 protected:
  // Starts FIDL message dispatching on |channel|, at the same time starts to manage the lifetime of
  // the connection.
  //
  // Implementations must ensure |connection| continues to live on, until |UnregisterConnection| is
  // called on the pointer to destroy it.
  virtual zx_status_t RegisterConnection(std::unique_ptr<internal::Connection> connection,
                                         zx::channel channel) = 0;

  // Destroys a connection.
  virtual void UnregisterConnection(internal::Connection* connection) = 0;

#endif  // ifdef __Fuchsia__
};

class Vfs::OpenResult {
 public:
  // When this variant is active, the indicated error occurred.
  using Error = zx_status_t;

  // When this variant is active, the path being opened contains a remote node. |path| is the
  // remaining portion of the path yet to be traversed. The caller should forward the remainder of
  // this open request to that vnode.
  //
  // Used only on Fuchsia.
  struct Remote {
    fbl::RefPtr<Vnode> vnode;
    std::string_view path;
  };

  // When this variant is active, the path being opened is a remote node itself. The caller should
  // clone the connection associated with this vnode.
  //
  // Used only on Fuchsia.
  struct RemoteRoot {
    fbl::RefPtr<Vnode> vnode;
  };

  // When this variant is active, |Open| has successfully reached a vnode under this filesystem.
  // |validated_options| contains options to be used on the new connection, potentially adjusted for
  // posix-flag rights expansion.
  struct Ok {
    fbl::RefPtr<Vnode> vnode;
    Vnode::ValidatedOptions validated_options;
  };

  // Forwards the constructor arguments into the underlying |std::variant|. This allows |OpenResult|
  // to be constructed directly from one of the variants, e.g.
  //
  //     OpenResult r = OpenResult::Error{ZX_ERR_ACCESS_DENIED};
  //
  template <typename T>
  OpenResult(T&& v) : variants_(std::forward<T>(v)) {}

  // Applies the |visitor| function to the variant payload. It simply forwards the visitor into the
  // underlying |std::variant|. Returns the return value of |visitor|. Refer to C++ documentation
  // for |std::visit|.
  template <class Visitor>
  constexpr auto visit(Visitor&& visitor) -> decltype(visitor(std::declval<zx_status_t>())) {
    return std::visit(std::forward<Visitor>(visitor), variants_);
  }

  Ok& ok() { return std::get<Ok>(variants_); }
  bool is_ok() const { return std::holds_alternative<Ok>(variants_); }

  Error& error() { return std::get<Error>(variants_); }
  bool is_error() const { return std::holds_alternative<Error>(variants_); }

  Remote& remote() { return std::get<Remote>(variants_); }
  bool is_remote() const { return std::holds_alternative<Remote>(variants_); }

  RemoteRoot& remote_root() { return std::get<RemoteRoot>(variants_); }
  bool is_remote_root() const { return std::holds_alternative<RemoteRoot>(variants_); }

 private:
  using Variants = std::variant<Error, Remote, RemoteRoot, Ok>;

  Variants variants_ = {};
};

class Vfs::TraversePathResult {
 public:
  // When this variant is active, the indicated error occurred.
  using Error = zx_status_t;

  // When this variant is active, the path being traversed contains a remote node. |path| is the
  // remaining portion of the path yet to be traversed. The caller should forward the remainder of
  // this request to that vnode.
  //
  // Used only on Fuchsia.
  struct Remote {
    fbl::RefPtr<Vnode> vnode;
    std::string_view path;
  };

  // When this variant is active, the path being traversed is a remote node itself.
  //
  // Used only on Fuchsia.
  struct RemoteRoot {
    fbl::RefPtr<Vnode> vnode;
  };

  // When this variant is active, we have successfully traversed and reached a vnode under this
  // filesystem.
  struct Ok {
    fbl::RefPtr<Vnode> vnode;
  };

  // Forwards the constructor arguments into the underlying |std::variant|. This allows
  // |TraversePathResult| to be constructed directly from one of the variants, e.g.
  //
  // TraversePathResult r = TraversePathResult::Error{ZX_ERR_ACCESS_DENIED};
  //
  template <typename T>
  TraversePathResult(T&& v) : variants_(std::forward<T>(v)) {}

  // Applies the |visitor| function to the variant payload. It simply forwards the visitor into the
  // underlying |std::variant|. Returns the return value of |visitor|. Refer to C++ documentation
  // for |std::visit|.
  template <class Visitor>
  constexpr auto visit(Visitor&& visitor) -> decltype(visitor(std::declval<zx_status_t>())) {
    return std::visit(std::forward<Visitor>(visitor), variants_);
  }

  Ok& ok() { return std::get<Ok>(variants_); }
  bool is_ok() const { return std::holds_alternative<Ok>(variants_); }

  Error& error() { return std::get<Error>(variants_); }
  bool is_error() const { return std::holds_alternative<Error>(variants_); }

  Remote& remote() { return std::get<Remote>(variants_); }
  bool is_remote() const { return std::holds_alternative<Remote>(variants_); }

  RemoteRoot& remote_root() { return std::get<RemoteRoot>(variants_); }
  bool is_remote_root() const { return std::holds_alternative<RemoteRoot>(variants_); }

 private:
  using Variants = std::variant<Error, Remote, RemoteRoot, Ok>;

  Variants variants_ = {};
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_VFS_H_
