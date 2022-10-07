// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_VNODE_H_
#define SRC_LIB_STORAGE_VFS_CPP_VNODE_H_

#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <map>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_counted_internal.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/ref_counted.h"
#include "src/lib/storage/vfs/cpp/shared_mutex.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

#ifdef __Fuchsia__
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/file-lock/file-lock.h>
#include <lib/zx/channel.h>
#include <lib/zx/stream.h>
#endif  // __Fuchsia__

namespace fs {

class Vfs;
struct VdirCookie;

#ifdef __Fuchsia__
class FuchsiaVfs;
#endif

inline bool IsValidName(std::string_view name) {
  return !name.empty() && name != "." && name != ".." && name.length() <= NAME_MAX &&
         name.find('/') == std::string::npos;
}

// The VFS interface declares a default abstract Vnode class with common operations that may be
// overridden.
//
// The ops are used for dispatch and the lifecycle of Vnodes are owned by RefPtrs.
//
// All names passed to the Vnode class are valid according to "IsValidName".
//
// Memory management
// -----------------
// The Vnode uses the fbl::Recyclable system to allow caching. This kicks in when the reference
// count of the node goes to zero.
//
// fbl::RefPtr uses fbl::internal::has_fbl_recycle_v which checks whether there is an fbl_recycle()
// implementation on the class being pointed to. This does not catch base class implementations!
//
// Each derived class must inherit from fbl::Recyclable<class_name> and implement an fbl_recycle()
// function. These implementations should call the protected virtual function RecycleNode().
//
// Derived classes should override RecycleNode() to implement the desired caching behavior.
class Vnode : public VnodeRefCounted<Vnode>, public fbl::Recyclable<Vnode> {
 public:
  // Define the current Vfs type so Fuchsia-ifdefed code can assume a FuchsiaVfs associated
  // class if vfs_ is non-null.
#ifdef __Fuchsia__
  using PlatformVfs = FuchsiaVfs;
#else
  using PlatformVfs = Vfs;
#endif

  virtual ~Vnode();

  // See class comment above about memory management.
  void fbl_recycle() { RecycleNode(); }

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
  // Implementations should override |GetProtocols| to express which representation(s) are supported
  // by the vnode. Implementations may optionally override |Negotiate| to insert custom tie-breaking
  // behavior when the vnode supports multiple protocols, and the client requested multiple at open
  // time.

  // Returns the set of all protocols supported by the vnode.
  virtual VnodeProtocolSet GetProtocols() const = 0;

  // Returns true iff the vnode supports _any_ protocol requested by |protocols|.
  bool Supports(VnodeProtocolSet protocols) const;

  // To be overridden by implementations to check that it is valid to access the vnode with the
  // given |rights|. The default implementation always returns true. The vnode will only be opened
  // for a particular request if the validation passes.
  virtual bool ValidateRights(Rights rights) const;

  // Ensures that it is valid to access the vnode with given connection options. The vnode will only
  // be opened for a particular request if the validation returns |zx::ok(...)|.
  //
  // The |zx::ok| variant of the return value is a |ValidatedOptions| object that encodes the
  // fact that |options| has been validated. It may be used to call other functions that only
  // accepts validated options.
  //
  // The |zx::error| variant of the return value contains a suitable error code
  // when validation fails.
  zx::status<ValidatedOptions> ValidateOptions(VnodeConnectionOptions options) const;

  // Picks one protocol from |protocols|, when the intersection of the protocols requested by the
  // client and the ones supported by the vnode has more than one elements i.e. tie-breaking is
  // required to determine the resultant protocol.
  //
  // This method is only called when tie-breaking is required. |protocols| is guaranteed to be a
  // subset of the supported protocols. The default implementation performs tie-breaking in the
  // order of element declaration within |VnodeProtocol|.
  virtual VnodeProtocol Negotiate(VnodeProtocolSet protocols) const;

  // Opens the vnode. This is a callback to signal that a new connection is about to be created and
  // I/O operations will follow. In addition, it provides an opportunity to redirect subsequent I/O.
  // If the open fails, the file will be deemed to be not opened and Close() will not be called.
  //
  // Vnode implementations should override OpenNode() which this function calls after some
  // bookeeping.
  //
  // |options| contain the flags and rights supplied by the client, parsed into a struct with
  // individual fields. It will have already been validated by |ValidateOptions|.
  //
  // Open is never invoked if |options.flags| includes |node_reference|. This behavior corresponds
  // to Posix open()'s O_PATH flag which will create a thing representing the path to the file
  // without giving the ability to do most operations like read or write. In the future, we may want
  // the ability to track these connections, in which case we should add a Connect()/Disconnect()
  // pair that would surround the Open()/Close() for the normal case, but would be called regardless
  // of the flags to cover the node-reference case.
  //
  // If the implementation of |Open()| sets |out_redirect| to a non-null value, all following I/O
  // operations on the opened object will be redirected to the indicated vnode instead of being
  // handled by this instance. This is useful when implementing lazy files/pseudo files, where a
  // different vnode may be used for each new connection to a file. Note that the |out_redirect|
  // vnode is not |Open()|ed further for the purpose of creating this connection. Furthermore, the
  // redirected vnode must support the same set of protocols as the original vnode.
  zx_status_t Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect)
      __TA_EXCLUDES(mutex_);

  // Same as |Open|, but calls |ValidateOptions| on |options| automatically. Errors from
  // |ValidateOptions| are propagated via the return value. This is convenient when serving a
  // connection with the validated options is unnecessary e.g. when used from a non-Fuchsia
  // operating system.
  zx_status_t OpenValidating(VnodeConnectionOptions options, fbl::RefPtr<Vnode>* out_redirect)
      __TA_EXCLUDES(mutex_);

  // TODO(https://fxbug.dev/101092): Remove this when either:
  //
  // - Connecting to services no longer requires rw rights.
  //
  // - All devfs clients request rw rights.
  virtual bool IsSkipRightsEnforcementDevfsOnlyDoNotUse() const { return false; }

  // METHODS FOR OPENED NODES
  //
  // The following operations will not be invoked unless the Vnode has been "Open()"-ed
  // successfully.
  //
  // For files opened with O_PATH (as a file descriptor only) the base classes' implementation of
  // some of these functions may be invoked anyway.

#ifdef __Fuchsia__
  // Serves a custom FIDL protocol over the specified |channel|, when the node protocol is
  // |VnodeProtocol::kConnector|.
  //
  // The default implementation returns |ZX_ERR_NOT_SUPPORTED|.
  // Subclasses may override this behavior to serve custom protocols over the channel.
  virtual zx_status_t ConnectService(zx::channel channel);

  // Dispatches incoming FIDL messages which aren't recognized by |Connection::HandleMessage|.
  //
  // The default implementation just closes the connection through |txn|.
  //
  // This implementation may be overridden to support additional non-fuchsia.io FIDL protocols.
  virtual void HandleFsSpecificMessage(fidl::IncomingHeaderAndMessage& msg, fidl::Transaction* txn);

  // Extract handle, type, and extra info from a vnode.
  //
  // The |protocol| argument specifies which protocol the connection is negotiated to speak. For
  // vnodes which only support a single protocol, the method may safely ignore this argument.
  // Callers should make sure to supply one of the supported protocols, or call |GetNodeInfo| if the
  // vnode is know to support a single protocol.
  //
  // The |rights| argument contain the access rights requested by the client, and should determine
  // corresponding access rights on the returned handles if applicable.
  //
  // The returned variant in |info| should correspond to the |protocol|.
  virtual zx_status_t GetNodeInfoForProtocol(VnodeProtocol protocol, Rights rights,
                                             VnodeRepresentation* info) = 0;

  // Extract handle, type, and extra info from a vnode. This version differs from
  // |GetNodeInfoForProtocol| that it is a convenience wrapper for vnodes which only support a
  // single protocol. If the vnode supports multiple protocols, clients should always call
  // |GetNodeInfoForProtocol| and specify a protocol.
  //
  // The |rights| argument contain the access rights requested by the client, and should determine
  // corresponding access rights on the returned handles if applicable.
  //
  // The returned variant in |info| should correspond to the |protocol|.
  zx_status_t GetNodeInfo(Rights rights, VnodeRepresentation* info);

  // Invoked by the VFS layer whenever files are added or removed.
  virtual void Notify(std::string_view name, fuchsia_io::wire::WatchEvent event);

  virtual zx_status_t WatchDir(Vfs* vfs, fuchsia_io::wire::WatchMask mask, uint32_t options,
                               fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher);

  // Create a |zx::stream| for reading and writing this vnode.
  //
  // If this function returns |ZX_OK|, then all |Read|, |Write|, and |Append| operations will be
  // directed to the stream returned via |out_stream| rather than to the |Read|, |Write|, and
  // |Append| methods on the vnode. The |zx::stream| might be transported to a remote process to
  // improve performance.
  //
  // If the client modifies the underlying data for this node via the returned |zx::stream|, the
  // node will be notified via |DidModifyStream|.
  //
  // Implementations should pass the given |stream_options| as the options to |zx::stream::create|.
  // These options ensure that the created |zx::stream| object has the appropriate rights for the
  // given connection.
  //
  // If the vnode does not support reading and writing using a |zx::stream|, return
  // ZX_ERR_NOT_SUPPORTED, which will cause |Read|, |Write|, and |Append| operations to be called as
  // methods on the vnode. Other errors are considered fatal and will terminate the connection.
  virtual zx_status_t CreateStream(uint32_t stream_options, zx::stream* out_stream);

  zx_status_t InsertInotifyFilter(fuchsia_io::wire::InotifyWatchMask filter,
                                  uint32_t watch_descriptor, zx::socket socket)
      __TA_EXCLUDES(gInotifyLock);
#endif

  // Closes the vnode. Will be called once for each successful Open().
  //
  // Vnode implementations should override CloseNode() which this function calls after some
  // bookkeeping.
  zx_status_t Close() __TA_EXCLUDES(mutex_);

  // Read data from the vnode at offset.
  //
  // If successful, returns the number of bytes read in |out_actual|. This must be less than or
  // equal to |len|.
  //
  // See |CreateStream| for a mechanism to offload |Read| to a |zx::stream| object.
  virtual zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual);

  // Write |len| bytes of |data| to the file, starting at |offset|.
  //
  // If successful, returns the number of bytes written in |out_actual|. This must be less than or
  // equal to |len|.
  //
  // See |CreateStream| for a mechanism to offload |Write| to a |zx::stream| object.
  virtual zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual);

  // Write |len| bytes of |data| to the end of the file.
  //
  // If successful, returns the number of bytes written in |out_actual|, and
  // returns the new end of file offset in |out_end|.
  //
  // See |CreateStream| for a mechanism to offload |Append| to a |zx::stream| object.
  virtual zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual);

  // The data for this node was modified via the |zx::stream| returned by |CreateStream|.
  //
  // When a client writes to the |zx::stream| returned by |CreateStream|, there is currently no
  // mechanism for the node to observe this modification and update its internal state (e.g., the
  // modification time of the file represented by this node). This method provides that notification
  // for the time being. In the future, we might switch to using a usermode pager to provide that
  // notification.
  virtual void DidModifyStream();

  // Change the size of the vnode.
  virtual zx_status_t Truncate(size_t len);

#ifdef __Fuchsia__
  // Acquire a vmo from a vnode.
  //
  // At the moment, mmap can only map files from read-only filesystems, since (without paging) there
  // is no mechanism to update either
  // 1) The file by writing to the mapping, or
  // 2) The mapping by writing to the underlying file.
  virtual zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo);
#endif  // __Fuchsia__

  // Syncs the vnode with its underlying storage.
  //
  // Returns the result status through a closure. The closure may be executed on a different thread
  // than called the Sync() function, or reentrantly from the same thread.
  using SyncCallback = fit::callback<void(zx_status_t status)>;
  virtual void Sync(SyncCallback closure);

  // Read directory entries of vn, error if not a directory. FS-specific Cookie must be a buffer of
  // VdirCookie size or smaller. Cookie must be zero'd before first call and will be used by the
  // readdir implementation to maintain state across calls. To "rewind" and start from the
  // beginning, cookie may be zero'd.
  virtual zx_status_t Readdir(VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual);

  // METHODS FOR OPENED OR UNOPENED NODES
  //
  // The following operations may be invoked on a Vnode, even if it has not been "Open()"-ed.

  // Attempt to find child of vn, child returned on success. Name is len bytes long, and does not
  // include a null terminator.
  virtual zx_status_t Lookup(std::string_view name, fbl::RefPtr<Vnode>* out);

  // Read attributes of the vnode.
  virtual zx_status_t GetAttributes(fs::VnodeAttributes* a);

  // Set attributes of the vnode.
  virtual zx_status_t SetAttributes(VnodeAttributesUpdate a);

  // Create a new node under vn. The vfs layer assumes that upon success, the |out| vnode has been
  // already opened i.e. |Open()| is not called again on the created vnode. Name is len bytes long,
  // and does not include a null terminator. Mode specifies the type of entity to create.
  virtual zx_status_t Create(std::string_view name, uint32_t mode, fbl::RefPtr<Vnode>* out);

  // Removes name from directory vn
  virtual zx_status_t Unlink(std::string_view name, bool must_be_dir);

  // Renames the path at oldname in olddir to the path at newname in newdir. Called on the "olddir"
  // vnode.
  //
  // Unlinks any prior newname if it already exists.
  virtual zx_status_t Rename(fbl::RefPtr<Vnode> newdir, std::string_view oldname,
                             std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir);

  // Creates a hard link to the 'target' vnode with a provided name in vndir
  virtual zx_status_t Link(std::string_view name, fbl::RefPtr<Vnode> target);

  // Called when the Vfs associated with this node is shutting down. The associated VFS will still
  // be valid at the time of the call.
  //
  // Derived classes can implement this to do cleanup that requires the Vfs. Because Vnodes are
  // reference-counted, they can outlive their associated Vfs.
  //
  // The default implementation will clear the vfs_ back-pointer, it should always be called by
  // overridden implementations.
  virtual void WillDestroyVfs();

  // Returns true if this is a remote filesystem mount point. This is only relevant on Fuchsia
  // builds (the remote handling below is all Fuchsia-only) but this can exist and just return false
  // on host builds to simplify platform handling.
  virtual bool IsRemote() const;

  // Returns true if this node is a service.  One implication of this is that read/write connections
  // will be allowed (services are typically connected in this way using fdio_connect_service) to
  // this node even if the filesystem is considered read-only (although the parent connection rights
  // are still honored).
  virtual bool IsService() const { return false; }

#ifdef __Fuchsia__
  // Return information about the underlying filesystem, if desired.
  virtual zx_status_t QueryFilesystem(fuchsia_io::wire::FilesystemInfo* out);

  // Returns the name of the device backing the filesystem, if one exists.
  virtual zx::status<std::string> GetDevicePath() const;

  // Implements fuchsia.io/Openable.Open by forwarding requests to the remote end. Supported iff
  // `IsRemote()`.
  virtual zx_status_t OpenRemote(fuchsia_io::OpenFlags, uint32_t, fidl::StringView,
                                 fidl::ServerEnd<fuchsia_io::Node>) const;

  // Check existing inotify watches and issue inotify events.
  zx_status_t CheckInotifyFilterAndNotify(fuchsia_io::wire::InotifyWatchMask event)
      __TA_EXCLUDES(gInotifyLock);
#endif  // __Fuchsia__

  // Invoked by internal Connections to account transactions
  void RegisterInflightTransaction() __TA_EXCLUDES(mutex_);
  void UnregisterInflightTransaction() __TA_EXCLUDES(mutex_);

  // Number of FIDL messages issued on this vnode that have been dispatched, but for which a reply
  // has not been made.
  size_t GetInflightTransactions() const __TA_EXCLUDES(mutex_);

 protected:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Vnode);

  // Implementation of fbl_recycle(). Normal fbl_recycle() use is non-virtual and requires different
  // inheritance paths to fbl::Recyclable. This virtual implementation allows there to be one
  // implementation.
  //
  // This function is called when the object reference count drops to 0. This default implementation
  // just deletes the object to get "normal" reference counting. Derived classes can override to
  // implement caching if desired.
  //
  // See the class comment above on recycling, this is subtle.
  virtual void RecycleNode() { delete this; }

  // Opens/Closes the vnode. These are the callbacks that the Vnode implementation overrides to do
  // the open and close work. They are called by the public Open() and Close() functions which
  // handles bookeeping for the base class.
  //
  // The open_count() will be updated BEFORE each call. If OpenNode fails, the open count will be
  // rolled back.
  //
  // See Open() above for documentation.
  virtual zx_status_t OpenNode(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect)
      __TA_EXCLUDES(mutex_) {
    return ZX_OK;
  }
  virtual zx_status_t CloseNode() __TA_EXCLUDES(mutex_) { return ZX_OK; }

  // The associated Vfs pointer is optional. Subclasses should require this if they need to access
  // the Vfs, but can leave null if not. See vfs() getter for more.
  explicit Vnode(PlatformVfs* vfs = nullptr);

  // Mutex for the data of this vnode. This is a shared mutex to support derived classes
  // implementing multiple simultaneous readers if desired.
  mutable SharedMutex mutex_;

  // The Vfs associated with this node, if any.
  //
  // The Vfs doesn't need to be set. It is tracked on the Vnode because some subclasses need it,
  // but it is not directly used by the Vnode. Therefore, subclasses should enforce through their
  // own constructors whether the vfs_ is set or not during construction.
  //
  // Additionally, this will be null when the Vfs is destroyed (since Vnodes are reference-counted
  // they can outlive the Vfs). Uses should always be inside the mutex_.
  PlatformVfs* vfs() __TA_REQUIRES_SHARED(mutex_) { return vfs_; }

  // Returns the number of open connections, not counting node_reference connections. See Open().
  size_t open_count() const __TA_REQUIRES_SHARED(mutex_) { return open_count_; }

#ifdef __Fuchsia__
 public:
  // Instead of adding a |file_lock::FileLock| member variable to |Vnode|,
  // maintain a map from |this| to the lock objects. This is done, because
  // file locking only applies to regular files, so we want to avoid the
  // memory overhead for all other |Vnode| types.
  std::shared_ptr<file_lock::FileLock> GetVnodeFileLock();
  bool DeleteFileLock(zx_koid_t owner);
  // This is the same as |DeleteFileLock|, but if there is no
  // lock, do not acquire |gLockAccess|.
  bool DeleteFileLockInTeardown(zx_koid_t owner);
#endif

 private:
  PlatformVfs* vfs_ __TA_GUARDED(mutex_) = nullptr;  // Possibly null, see getter above.
  size_t inflight_transactions_ __TA_GUARDED(mutex_) = 0;
  size_t open_count_ __TA_GUARDED(mutex_) = 0;
#ifdef __Fuchsia__
  static std::mutex gLockAccess;
  static std::map<const Vnode*, std::shared_ptr<file_lock::FileLock>> gLockMap;
  struct InotifyFilter {
    fuchsia_io::wire::InotifyWatchMask filter_;
    uint32_t watch_descriptor_;
    zx::socket socket_;
    InotifyFilter(fuchsia_io::wire::InotifyWatchMask filter, uint32_t wd, zx::socket socket)
        : filter_{filter}, watch_descriptor_{wd} {
      socket_ = std::move(socket);
    }
  };
  static std::mutex gInotifyLock;
  static std::map<const Vnode*, std::vector<InotifyFilter>> gInotifyMap;
#endif
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
  zx_status_t Next(std::string_view name, uint8_t type, uint64_t ino);

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

#endif  // SRC_LIB_STORAGE_VFS_CPP_VNODE_H_
