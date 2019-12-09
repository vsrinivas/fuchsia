// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_VNODE_H_
#define FS_VNODE_H_

#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/fit/result.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <type_traits>
#include <utility>

#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted_internal.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>
#include <fs/ref_counted.h>
#include <fs/vfs_types.h>

#ifdef __Fuchsia__
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/device/vfs.h>
#include <fs/mount_channel.h>
#endif  // __Fuchsia__

namespace fs {

class Vfs;
typedef struct vdircookie vdircookie_t;

inline bool vfs_valid_name(fbl::StringPiece name) {
  return name.length() <= NAME_MAX && memchr(name.data(), '/', name.length()) == nullptr &&
         name != "." && name != "..";
}

// The VFS interface declares a default abstract Vnode class with
// common operations that may be overridden.
//
// The ops are used for dispatch and the lifecycle of Vnodes are owned
// by RefPtrs.
//
// All names passed to the Vnode class are valid according to "vfs_valid_name".
class Vnode : public VnodeRefCounted<Vnode>, public fbl::Recyclable<Vnode> {
 public:
  virtual ~Vnode();
  virtual void fbl_recycle() { delete this; }

  template <typename T>
  class Validated {
   public:
    Validated(const Validated&) = default;
    Validated& operator=(const Validated&) = default;
    Validated(Validated&&) noexcept = default;
    Validated& operator=(Validated&&) noexcept = default;

    const T& value() const { return value_; }
    const T* operator->() const { return &value(); }
    const T& operator*() const { return value(); }

   private:
    explicit Validated(T value) : value_(value) {}
    friend class Vnode;  // Such that only |Vnode| methods may mint new instances of |Validated<T>|.
    T value_;
  };
  using ValidatedOptions = Validated<VnodeConnectionOptions>;

  // METHODS FOR OPTION VALIDATION AND PROTOCOL NEGOTIATION
  //
  // Implementations should override |GetProtocols| to express which representation(s)
  // are supported by the vnode. Implementations may optionally override |Negotiate| to
  // insert custom tie-breaking behavior when the vnode supports multiple protocols,
  // and the client requested multiple at open time.

  // Returns the set of all protocols supported by the vnode.
  virtual VnodeProtocolSet GetProtocols() const = 0;

  // Returns true iff the vnode supports _any_ protocol requested by |protocols|.
  bool Supports(VnodeProtocolSet protocols) const;

  // To be overridden by implementations to check that it is valid to access the
  // vnode with the given |rights|. The default implementation always returns true.
  // The vnode will only be opened for a particular request if the validation passes.
  virtual bool ValidateRights(Rights rights);

  // Ensures that it is valid to access the vnode with given connection options.
  // The vnode will only be opened for a particular request if the validation
  // returns |fit::ok(...)|.
  // The |fit::ok| variant of the return value is a |ValidatedOptions| object that
  // encodes the fact that |options| has been validated. It may be used to call
  // other functions that only accepts validated options.
  // The |fit::error| variant of the return value contains a suitable error code
  // when validation fails.
  fit::result<ValidatedOptions, zx_status_t> ValidateOptions(VnodeConnectionOptions options);

  // Picks one protocol from |protocols|, when the intersection of the protocols requested
  // by the client and the ones supported by the vnode has more than one elements i.e.
  // tie-breaking is required to determine the resultant protocol.
  //
  // This method is only called when tie-breaking is required.
  // |protocols| is guaranteed to be a subset of the supported protocols.
  // The default implementation performs tie-breaking in the order of element declaration
  // within |VnodeProtocol|.
  virtual VnodeProtocol Negotiate(VnodeProtocolSet protocols) const;

  // Opens the vnode. This is a callback to signal that a new connection is about to be
  // created and I/O operations will follow. In addition, it provides an opportunity to
  // redirect subsequent I/O operations to a different vnode.
  //
  // |options| contain the flags and rights supplied by the client, parsed into a struct
  // with individual fields. It will have already been validated by |ValidateOptions|.
  // Open is never invoked if |options.flags| includes |node_reference|.
  //
  // If the implementation of |Open()| sets |out_redirect| to a non-null value,
  // all following I/O operations on the opened object will be redirected to the
  // indicated vnode instead of being handled by this instance. This is useful
  // when implementing lazy files/pseudo files, where a different vnode may be
  // used for each new connection to a file. Note that the |out_redirect| vnode is not
  // |Open()|ed further for the purpose of creating this connection. Furthermore, the
  // redirected vnode must support the same set of protocols as the original vnode.
  virtual zx_status_t Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect);

  // Same as |Open|, but calls |ValidateOptions| on |options| automatically.
  // Errors from |ValidateOptions| are propagated via the return value.
  // This is convenient when serving a connection with the validated options is unnecessary
  // e.g. when used from a non-Fuchsia operating system.
  zx_status_t OpenValidating(VnodeConnectionOptions options, fbl::RefPtr<Vnode>* out_redirect);

  // METHODS FOR OPENED NODES
  //
  // The following operations will not be invoked unless the Vnode has
  // been "Open()"-ed successfully.
  //
  // For files opened with O_PATH (as a file descriptor only) the base
  // classes' implementation of some of these functions may be invoked anyway.

#ifdef __Fuchsia__
  // Serves a custom FIDL protocol over the specified |channel|, when the node protocol is
  // |VnodeProtocol::kConnector|.
  //
  // The default implementation returns |ZX_ERR_NOT_SUPPORTED|.
  // Subclasses may override this behavior to serve custom protocols over the channel.
  virtual zx_status_t ConnectService(zx::channel channel);

  // Dispatches incoming FIDL messages which aren't recognized by |Connection::HandleMessage|.
  //
  // Takes ownership of the FIDL message's handles.
  // The default implementation just closes these handles.
  //
  // This implementation may be overridden to support additional non-fuchsia.io FIDL protocols.
  virtual void HandleFsSpecificMessage(fidl_msg_t* msg, fidl::Transaction* txn);

  // Extract handle, type, and extra info from a vnode.
  //
  // The |protocol| argument specifies which protocol the connection is negotiated to speak.
  // For vnodes which only support a single protocol, the method may safely ignore this argument.
  // Callers should make sure to supply one of the supported protocols, or call |GetNodeInfo|
  // if the vnode is know to support a single protocol.
  //
  // The |rights| argument contain the access rights requested by the client, and should determine
  // corresponding access rights on the returned handles if applicable.
  //
  // The returned variant in |info| should correspond to the |protocol|.
  virtual zx_status_t GetNodeInfoForProtocol(VnodeProtocol protocol, Rights rights,
                                             VnodeRepresentation* info) = 0;

  // Extract handle, type, and extra info from a vnode. This version differs from
  // |GetNodeInfoForProtocol| that it is a convenience wrapper for vnodes which only support
  // a single protocol. If the vnode supports multiple protocols, clients should always call
  // |GetNodeInfoForProtocol| and specify a protocol.
  //
  // The |rights| argument contain the access rights requested by the client, and should determine
  // corresponding access rights on the returned handles if applicable.
  //
  // The returned variant in |info| should correspond to the |protocol|.
  zx_status_t GetNodeInfo(Rights rights, VnodeRepresentation* info);

  virtual zx_status_t WatchDir(Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher);
#endif

  // Closes the vnode. Will be called once for each successful Open().
  //
  // Typically, most Vnodes simply return "ZX_OK".
  virtual zx_status_t Close();

  // Read data from the vnode at offset.
  //
  // If successful, returns the number of bytes read in |out_actual|. This must be
  // less than or equal to |len|.
  virtual zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual);

  // Write |len| bytes of |data| to the file, starting at |offset|.
  //
  // If successful, returns the number of bytes written in |out_actual|. This must be
  // less than or equal to |len|.
  virtual zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual);

  // Write |len| bytes of |data| to the end of the file.
  //
  // If successful, returns the number of bytes written in |out_actual|, and
  // returns the new end of file offset in |out_end|.
  virtual zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual);

  // Change the size of the vnode.
  virtual zx_status_t Truncate(size_t len);

#ifdef __Fuchsia__
  // Acquire a vmo from a vnode.
  //
  // At the moment, mmap can only map files from read-only filesystems,
  // since (without paging) there is no mechanism to update either
  // 1) The file by writing to the mapping, or
  // 2) The mapping by writing to the underlying file.
  virtual zx_status_t GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size);
#endif  // __Fuchsia__

  // Syncs the vnode with its underlying storage.
  //
  // Returns the result status through a closure.
  using SyncCallback = fbl::Function<void(zx_status_t status)>;
  virtual void Sync(SyncCallback closure);

  // Read directory entries of vn, error if not a directory.
  // FS-specific Cookie must be a buffer of vdircookie_t size or smaller.
  // Cookie must be zero'd before first call and will be used by
  // the readdir implementation to maintain state across calls.
  // To "rewind" and start from the beginning, cookie may be zero'd.
  virtual zx_status_t Readdir(vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual);

  // METHODS FOR OPENED OR UNOPENED NODES
  //
  // The following operations may be invoked on a Vnode, even if it has
  // not been "Open()"-ed.

  // Attempt to find child of vn, child returned on success.
  // Name is len bytes long, and does not include a null terminator.
  virtual zx_status_t Lookup(fbl::RefPtr<Vnode>* out, fbl::StringPiece name);

  // Read attributes of the vnode.
  virtual zx_status_t GetAttributes(fs::VnodeAttributes* a);

  // Set attributes of the vnode.
  virtual zx_status_t SetAttributes(VnodeAttributesUpdate a);

  // Create a new node under vn. The vfs layer assumes that upon success, the |out| vnode
  // has been already opened i.e. |Open()| is not called again on the created vnode.
  // Name is len bytes long, and does not include a null terminator.
  // Mode specifies the type of entity to create.
  virtual zx_status_t Create(fbl::RefPtr<Vnode>* out, fbl::StringPiece name, uint32_t mode);

  // Removes name from directory vn
  virtual zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir);

  // Renames the path at oldname in olddir to the path at newname in newdir.
  // Called on the "olddir" vnode.
  // Unlinks any prior newname if it already exists.
  virtual zx_status_t Rename(fbl::RefPtr<Vnode> newdir, fbl::StringPiece oldname,
                             fbl::StringPiece newname, bool src_must_be_dir, bool dst_must_be_dir);

  // Creates a hard link to the 'target' vnode with a provided name in vndir
  virtual zx_status_t Link(fbl::StringPiece name, fbl::RefPtr<Vnode> target);

  // Invoked by the VFS layer whenever files are added or removed.
  virtual void Notify(fbl::StringPiece name, unsigned event);

#ifdef __Fuchsia__
  // Return information about the underlying filesystem, if desired.
  virtual zx_status_t QueryFilesystem(llcpp::fuchsia::io::FilesystemInfo* out);

  // Returns the name of the device backing the filesystem, if one exists.
  virtual zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len);

  // Attaches a handle to the vnode, if possible. Otherwise, returns an error.
  virtual zx_status_t AttachRemote(MountChannel h);

  // The following methods are required to mount sub-filesystems. The logic
  // (and storage) necessary to implement these functions exists within the
  // "RemoteContainer" class, which may be composed inside Vnodes that wish
  // to act as mount points.

  // The vnode is acting as a mount point for a remote filesystem or device.
  virtual bool IsRemote() const;
  virtual zx::channel DetachRemote();
  virtual zx_handle_t GetRemote() const;
  virtual void SetRemote(zx::channel remote);
#endif  // __Fuchsia__

 protected:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Vnode);
  Vnode();
};

// Opens a vnode by reference.
// The |vnode| reference is updated in-place if redirection occurs.
inline zx_status_t OpenVnode(Vnode::ValidatedOptions options, fbl::RefPtr<Vnode>* vnode) {
  fbl::RefPtr<Vnode> redirect;
  zx_status_t status = (*vnode)->Open(options, &redirect);
  if (status == ZX_OK && redirect != nullptr) {
    ZX_DEBUG_ASSERT((*vnode)->GetProtocols() == redirect->GetProtocols());
    *vnode = std::move(redirect);
  }
  return status;
}

// Helper class used to fill direntries during calls to Readdir.
class DirentFiller {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(DirentFiller);

  DirentFiller(void* ptr, size_t len);

  // Attempts to add the name to the end of the dirent buffer
  // which is returned by readdir.
  zx_status_t Next(fbl::StringPiece name, uint8_t type, uint64_t ino);

  zx_status_t BytesFilled() const { return static_cast<zx_status_t>(pos_); }

 private:
  char* ptr_;
  size_t pos_;
  const size_t len_;
};

// Helper class to track outstanding operations associated to a
// particular Vnode.
class VnodeToken : public fbl::SinglyLinkedListable<std::unique_ptr<VnodeToken>> {
 public:
  VnodeToken(zx_koid_t koid, fbl::RefPtr<Vnode> vnode) : koid_(koid), vnode_(std::move(vnode)) {}

  zx_koid_t get_koid() const { return koid_; }
  fbl::RefPtr<Vnode> get_vnode() const { return vnode_; }

  // Trait implementation for fbl::HashTable
  zx_koid_t GetKey() const { return koid_; }
  static size_t GetHash(zx_koid_t koid) { return koid; }

 private:
  zx_koid_t koid_;
  fbl::RefPtr<Vnode> vnode_;
};

}  // namespace fs

#endif  // FS_VNODE_H_
