// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_FUCHSIA_VFS_H_
#define SRC_LIB_STORAGE_VFS_CPP_FUCHSIA_VFS_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/io.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/mount_channel.h"
#include "src/lib/storage/vfs/cpp/vfs.h"

namespace fs {

namespace internal {
class Connection;
}  // namespace internal

// Vfs specialization that adds Fuchsia-specific
class FuchsiaVfs : public Vfs {
 public:
  explicit FuchsiaVfs(async_dispatcher_t* dispatcher = nullptr);
  ~FuchsiaVfs() override;

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

  // Vfs overrides.
  zx_status_t UnlinkValidated(fbl::RefPtr<Vnode> vn, std::string_view path,
                              bool must_be_dir) override __TA_EXCLUDES(vfs_lock_);

  void TokenDiscard(zx::event ios_token) __TA_EXCLUDES(vfs_lock_);
  zx_status_t VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token, zx::event* out)
      __TA_EXCLUDES(vfs_lock_);
  zx_status_t Link(zx::event token, fbl::RefPtr<Vnode> oldparent, std::string_view oldStr,
                   std::string_view newStr) __TA_EXCLUDES(vfs_lock_);
  zx_status_t Rename(zx::event token, fbl::RefPtr<Vnode> oldparent, std::string_view oldStr,
                     std::string_view newStr) __TA_EXCLUDES(vfs_lock_);

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
                                      fuchsia_io2::wire::InotifyWatchMask filter,
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
    return ServeDirectory(std::move(vn), std::move(server_end), fs::Rights::All());
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

 protected:
  // Vfs protected overrides.
  zx_status_t EnsureExists(fbl::RefPtr<Vnode> vndir, std::string_view name,
                           fbl::RefPtr<Vnode>* out_vn, fs::VnodeConnectionOptions options,
                           uint32_t mode, Rights parent_rights, bool* did_create) override
      __TA_REQUIRES(vfs_lock_);

  // Starts FIDL message dispatching on |channel|, at the same time starts to manage the lifetime of
  // the connection.
  //
  // Implementations must ensure |connection| continues to live on, until |UnregisterConnection| is
  // called on the pointer to destroy it.
  virtual zx_status_t RegisterConnection(std::unique_ptr<internal::Connection> connection,
                                         zx::channel channel) = 0;

  // Destroys a connection.
  virtual void UnregisterConnection(internal::Connection* connection) = 0;

 private:
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
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_FUCHSIA_VFS_H_
