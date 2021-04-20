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

#include <fuchsia/io/llcpp/fidl.h>

#include "src/lib/storage/vfs/cpp/mount_channel.h"

namespace fio = fuchsia_io;

#endif  // __Fuchsia__

namespace fs {

Vnode::Vnode(Vfs* vfs) : vfs_(vfs) {
  if (vfs_)  // Vfs pointer is optional.
    vfs_->RegisterVnode(this);
}

Vnode::~Vnode() {
  std::lock_guard lock(mutex_);

  ZX_DEBUG_ASSERT_MSG(inflight_transactions_ == 0, "Inflight transactions in dtor %zu\n",
                      inflight_transactions_);

  if (vfs_)
    vfs_->UnregisterVnode(this);
}

#ifdef __Fuchsia__

zx_status_t Vnode::CreateStream(uint32_t stream_options, zx::stream* out_stream) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::ConnectService(zx::channel channel) { return ZX_ERR_NOT_SUPPORTED; }

void Vnode::HandleFsSpecificMessage(fidl_incoming_msg_t* msg, fidl::Transaction* txn) {
  FidlHandleInfoCloseMany(msg->handles, msg->num_handles);
  txn->Close(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t Vnode::WatchDir(Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher) {
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
    case VnodeProtocol::kPipe:
      ZX_DEBUG_ASSERT(info->is_pipe());
      break;
    case VnodeProtocol::kMemory:
      ZX_DEBUG_ASSERT(info->is_memory());
      break;
    case VnodeProtocol::kDevice:
      ZX_DEBUG_ASSERT(info->is_device());
      break;
    case VnodeProtocol::kTty:
      ZX_DEBUG_ASSERT(info->is_tty());
      break;
    case VnodeProtocol::kDatagramSocket:
      ZX_DEBUG_ASSERT(info->is_datagram_socket());
      break;
    case VnodeProtocol::kStreamSocket:
      ZX_DEBUG_ASSERT(info->is_stream_socket());
      break;
  }
  return ZX_OK;
}

#endif  // __Fuchsia__

void Vnode::Notify(std::string_view name, unsigned event) {}

void Vnode::WillDestroyVfs() {
  std::lock_guard lock(mutex_);

  ZX_DEBUG_ASSERT(vfs_);  // Shouldn't be deleting more than once.
  vfs_ = nullptr;
}

bool Vnode::Supports(VnodeProtocolSet protocols) const {
  return (GetProtocols() & protocols).any();
}

bool Vnode::ValidateRights([[maybe_unused]] Rights rights) { return true; }

auto Vnode::ValidateOptions(VnodeConnectionOptions options)
    -> fit::result<ValidatedOptions, zx_status_t> {
  auto protocols = options.protocols();
  if (!Supports(protocols)) {
    if (protocols == VnodeProtocol::kDirectory) {
      return fit::error(ZX_ERR_NOT_DIR);
    } else {
      return fit::error(ZX_ERR_NOT_FILE);
    }
  }
  if (!ValidateRights(options.rights)) {
    return fit::error(ZX_ERR_ACCESS_DENIED);
  }
  return fit::ok(Validated(options));
}

VnodeProtocol Vnode::Negotiate(VnodeProtocolSet protocols) const {
  auto protocol = protocols.first();
  ZX_DEBUG_ASSERT(protocol.has_value());
  return *protocol;
}

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

  return ZX_OK;
}

zx_status_t Vnode::OpenValidating(VnodeConnectionOptions options,
                                  fbl::RefPtr<Vnode>* out_redirect) {
  auto validated_options = ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.error();
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

#ifdef __Fuchsia__

zx_status_t Vnode::GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::QueryFilesystem(fio::wire::FilesystemInfo* out) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::AttachRemote(MountChannel h) { return ZX_ERR_NOT_SUPPORTED; }

bool Vnode::IsRemote() const { return false; }

fidl::ClientEnd<fuchsia_io::Directory> Vnode::DetachRemote() {
  return fidl::ClientEnd<fuchsia_io::Directory>();
}

fidl::UnownedClientEnd<fuchsia_io::Directory> Vnode::GetRemote() const {
  return fidl::UnownedClientEnd<fuchsia_io::Directory>(ZX_HANDLE_INVALID);
}

void Vnode::SetRemote(fidl::ClientEnd<fuchsia_io::Directory> remote) { ZX_DEBUG_ASSERT(false); }

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
