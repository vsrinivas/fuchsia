// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/vnode.h"

#include <zircon/assert.h>
#include <zircon/errors.h>

#include <string_view>
#include <utility>

#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

#ifdef __Fuchsia__

#include <fidl/fuchsia.io/cpp/wire.h>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace fio = fuchsia_io;

#endif  // __Fuchsia__

namespace fs {

#ifdef __Fuchsia__
std::mutex Vnode::gInotifyLock;
std::map<const Vnode*, std::vector<Vnode::InotifyFilter>> Vnode::gInotifyMap;
std::mutex Vnode::gLockAccess;
std::map<const Vnode*, std::shared_ptr<file_lock::FileLock>> Vnode::gLockMap;
#endif

Vnode::Vnode(PlatformVfs* vfs) : vfs_(vfs) {
  if (vfs_)  // Vfs pointer is optional.
    vfs_->RegisterVnode(this);
}

Vnode::~Vnode() {
  std::lock_guard lock(mutex_);

  ZX_DEBUG_ASSERT_MSG(inflight_transactions_ == 0, "Inflight transactions in dtor %zu\n",
                      inflight_transactions_);

#ifdef __Fuchsia__
  ZX_DEBUG_ASSERT_MSG(gLockMap.find(this) == gLockMap.end(),
                      "lock entry in gLockMap not cleaned up for Vnode");
#endif

  if (vfs_)
    vfs_->UnregisterVnode(this);
}

#ifdef __Fuchsia__

zx_status_t Vnode::CreateStream(uint32_t stream_options, zx::stream* out_stream) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::ConnectService(zx::channel channel) { return ZX_ERR_NOT_SUPPORTED; }

void Vnode::HandleFsSpecificMessage(fidl::IncomingHeaderAndMessage& msg, fidl::Transaction* txn) {
  txn->Close(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t Vnode::WatchDir(Vfs* vfs, fio::wire::WatchMask mask, uint32_t options,
                            fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::GetNodeInfo(Rights rights, VnodeRepresentation* info) {
  auto maybe_protocol = GetProtocols().which();
  ZX_DEBUG_ASSERT(maybe_protocol.has_value());
  VnodeProtocol protocol = *maybe_protocol;
  zx_status_t status = GetNodeInfoForProtocol(protocol, rights, info);
  if (status != ZX_OK) {
    return status;
  }
  switch (protocol) {
    case VnodeProtocol::kConnector:
      ZX_DEBUG_ASSERT(info->is_connector());
      break;
    case VnodeProtocol::kFile:
      ZX_DEBUG_ASSERT(info->is_file());
      break;
    case VnodeProtocol::kDirectory:
      ZX_DEBUG_ASSERT(info->is_directory());
      break;
    case VnodeProtocol::kTty:
      ZX_DEBUG_ASSERT(info->is_tty());
      break;
  }
  return ZX_OK;
}

void Vnode::Notify(std::string_view name, fuchsia_io::wire::WatchEvent event) {}

#endif  // __Fuchsia__

void Vnode::WillDestroyVfs() {
  std::lock_guard lock(mutex_);

  ZX_DEBUG_ASSERT(vfs_);  // Shouldn't be deleting more than once.
  vfs_ = nullptr;
}

bool Vnode::Supports(VnodeProtocolSet protocols) const {
  return (GetProtocols() & protocols).any();
}

bool Vnode::ValidateRights([[maybe_unused]] Rights rights) const { return true; }

zx::status<Vnode::ValidatedOptions> Vnode::ValidateOptions(VnodeConnectionOptions options) const {
  auto protocols = options.protocols();
  if (!Supports(protocols)) {
    if (protocols == VnodeProtocol::kDirectory) {
      return zx::error(ZX_ERR_NOT_DIR);
    }
    return zx::error(ZX_ERR_NOT_FILE);
  }
  if (!ValidateRights(options.rights)) {
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }
  return zx::ok(Validated(options));
}

VnodeProtocol Vnode::Negotiate(VnodeProtocolSet protocols) const {
  auto protocol = protocols.first();
  ZX_DEBUG_ASSERT(protocol.has_value());
  return *protocol;
}

#ifdef __Fuchsia__
zx_status_t Vnode::InsertInotifyFilter(fio::wire::InotifyWatchMask filter,
                                       uint32_t watch_descriptor, zx::socket socket) {
  // TODO add basic checks for filter and watch_descriptor.
  std::lock_guard lock_access(gInotifyLock);
  auto inotify_filter_list = gInotifyMap.find(this);
  // No filters exist for this Vnode.
  if (inotify_filter_list == gInotifyMap.end()) {
    auto inserted = gInotifyMap.emplace(std::pair(this, std::vector<Vnode::InotifyFilter>()));
    if (inserted.second) {
      auto vnode_filter = inserted.first;
      vnode_filter->second.push_back(InotifyFilter(filter, watch_descriptor, std::move(socket)));
    } else {
      return ZX_ERR_NO_RESOURCES;
    }
  } else {
    inotify_filter_list->second.push_back(
        InotifyFilter(filter, watch_descriptor, std::move(socket)));
  }
  return ZX_OK;
}

zx_status_t Vnode::CheckInotifyFilterAndNotify(fio::wire::InotifyWatchMask event) {
  std::lock_guard lock(gInotifyLock);
  auto inotify_filter_list = gInotifyMap.find(this);
  if (inotify_filter_list == gInotifyMap.end()) {
    // No filters on this Vnode.
    return ZX_OK;
  }
  // Filter list found. Iterate list to check if we have a filter for the desired event.
  for (auto iter = inotify_filter_list->second.begin(); iter != inotify_filter_list->second.end();
       ++iter) {
    uint32_t incoming_event = static_cast<uint32_t>(event);
    incoming_event &= static_cast<uint32_t>(iter->filter_);
    if (incoming_event) {
      // filter found, we need to send event on the socket.
      fio::wire::InotifyEvent inotify_event{.watch_descriptor = iter->watch_descriptor_,
                                            .mask = event,
                                            .cookie = 0,
                                            .len = 0,
                                            .filename = {}};
      size_t actual;
      zx_status_t status = iter->socket_.write(0, &inotify_event, sizeof(inotify_event), &actual);
      if (status != ZX_OK) {
        // TODO(fxbug.dev/83035) Report IN_Q_OVERFLOW if the socket buffer is full.
      }
    }
  }
  return ZX_OK;
}
#endif

zx_status_t Vnode::Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) {
  {
    std::lock_guard lock(mutex_);
    open_count_++;
  }

  if (zx_status_t status = OpenNode(options, out_redirect); status != ZX_OK) {
    // Roll back the open count since we won't get a close for it.
    std::lock_guard lock(mutex_);
    open_count_--;
    return status;
  }
#ifdef __Fuchsia__
  // Traverse the inotify list for open event filter and send event back to clients.
  CheckInotifyFilterAndNotify(fio::wire::InotifyWatchMask::kOpen);
#endif
  return ZX_OK;
}

zx_status_t Vnode::OpenValidating(VnodeConnectionOptions options,
                                  fbl::RefPtr<Vnode>* out_redirect) {
  auto validated_options = ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.status_value();
  }
  // The documentation on Vnode::Open promises it will never be called if options includes
  // vnode_reference.
  ZX_DEBUG_ASSERT(!validated_options.value()->flags.node_reference);
  return Open(validated_options.value(), out_redirect);
}

zx_status_t Vnode::Close() {
  {
    std::lock_guard lock(mutex_);
    open_count_--;
  }
#ifdef __Fuchsia__
  // Traverse the inotify list for close event filter and send event back to clients.
  CheckInotifyFilterAndNotify(fuchsia_io::wire::kCloseAll);
#endif
  return CloseNode();
}

zx_status_t Vnode::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

void Vnode::DidModifyStream() {}

zx_status_t Vnode::Lookup(std::string_view name, fbl::RefPtr<Vnode>* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::GetAttributes(VnodeAttributes* a) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::SetAttributes(VnodeAttributesUpdate a) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::Readdir(VdirCookie* cookie, void* dirents, size_t len, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Create(std::string_view name, uint32_t mode, fbl::RefPtr<Vnode>* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Unlink(std::string_view name, bool must_be_dir) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::Truncate(size_t len) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::Rename(fbl::RefPtr<Vnode> newdir, std::string_view oldname,
                          std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Link(std::string_view name, fbl::RefPtr<Vnode> target) {
  return ZX_ERR_NOT_SUPPORTED;
}

void Vnode::Sync(SyncCallback closure) { closure(ZX_ERR_NOT_SUPPORTED); }

bool Vnode::IsRemote() const { return false; }

#ifdef __Fuchsia__

zx_status_t Vnode::GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::QueryFilesystem(fuchsia_io::wire::FilesystemInfo* out) {
  *out = {};
  std::lock_guard lock(mutex_);

  if (!vfs_)
    return ZX_ERR_NOT_SUPPORTED;

  zx::status<FilesystemInfo> info_or = vfs_->GetFilesystemInfo();
  if (info_or.is_error()) {
    return info_or.status_value();
  }

  *out = info_or.value().ToFidl();
  return ZX_OK;
}

zx::status<std::string> Vnode::GetDevicePath() const { return zx::error(ZX_ERR_NOT_SUPPORTED); }

fidl::ClientEnd<fuchsia_io::Directory> Vnode::DetachRemote() {
  return fidl::ClientEnd<fuchsia_io::Directory>();
}

fidl::UnownedClientEnd<fuchsia_io::Directory> Vnode::GetRemote() const {
  return fidl::UnownedClientEnd<fuchsia_io::Directory>(ZX_HANDLE_INVALID);
}

std::shared_ptr<file_lock::FileLock> Vnode::GetVnodeFileLock() {
  std::lock_guard lock_access(gLockAccess);
  auto lock = gLockMap.find(this);
  if (lock == gLockMap.end()) {
    auto inserted = gLockMap.emplace(std::pair(this, std::make_shared<file_lock::FileLock>()));
    if (inserted.second) {
      lock = inserted.first;
    } else {
      return nullptr;
    }
  }
  return lock->second;
}
bool Vnode::DeleteFileLock(zx_koid_t owner) {
  std::lock_guard lock_access(gLockAccess);
  bool deleted = false;
  auto lock = gLockMap.find(this);
  if (lock != gLockMap.end()) {
    deleted = lock->second->Forget(owner);
    if (lock->second->NoLocksHeld()) {
      gLockMap.erase(this);
    }
  }
  return deleted;
}

// There is no guard here, as the connection is in teardown.
bool Vnode::DeleteFileLockInTeardown(zx_koid_t owner) {
  if (gLockMap.find(this) == gLockMap.end()) {
    return false;
  }
  return DeleteFileLock(owner);
}

#endif  // __Fuchsia__

void Vnode::RegisterInflightTransaction() {
  std::lock_guard lock(mutex_);
  inflight_transactions_++;
}

void Vnode::UnregisterInflightTransaction() {
  std::lock_guard lock(mutex_);
  inflight_transactions_--;
}

size_t Vnode::GetInflightTransactions() const {
  SharedLock lock(mutex_);
  return inflight_transactions_;
}

DirentFiller::DirentFiller(void* ptr, size_t len)
    : ptr_(static_cast<char*>(ptr)), pos_(0), len_(len) {}

zx_status_t DirentFiller::Next(std::string_view name, uint8_t type, uint64_t ino) {
  vdirent_t* de = reinterpret_cast<vdirent_t*>(ptr_ + pos_);
  size_t sz = sizeof(vdirent_t) + name.length();

  if (sz > len_ - pos_ || name.length() > NAME_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }
  de->ino = ino;
  de->size = static_cast<uint8_t>(name.length());
  de->type = type;
  memcpy(de->name, name.data(), name.length());
  pos_ += sz;
  return ZX_OK;
}

}  // namespace fs
