// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/errors.h>

#include <utility>

#include <fs/vfs_types.h>
#include <fs/vnode.h>

#ifdef __Fuchsia__

#include <fuchsia/io/llcpp/fidl.h>

#include <fs/mount_channel.h>

namespace fio = ::llcpp::fuchsia::io;

#endif  // __Fuchsia__

namespace fs {

Vnode::Vnode() = default;

Vnode::~Vnode() {
  ZX_DEBUG_ASSERT_MSG(inflight_transactions_.load() == 0, "Inflight transactions in dtor %zu\n",
                      inflight_transactions_.load());
}

#ifdef __Fuchsia__

zx_status_t Vnode::CreateStream(uint32_t stream_options, zx::stream* out_stream) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::ConnectService(zx::channel channel) { return ZX_ERR_NOT_SUPPORTED; }

void Vnode::HandleFsSpecificMessage(fidl_msg_t* msg, fidl::Transaction* txn) {
  zx_handle_close_many(msg->handles, msg->num_handles);
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

void Vnode::Notify(fbl::StringPiece name, unsigned event) {}

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

zx_status_t Vnode::Close() { return ZX_OK; }

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

zx_status_t Vnode::Lookup(fbl::StringPiece name, fbl::RefPtr<Vnode>* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::GetAttributes(VnodeAttributes* a) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::SetAttributes(VnodeAttributesUpdate a) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::Readdir(vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Create(fbl::StringPiece name, uint32_t mode, fbl::RefPtr<Vnode>* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Unlink(fbl::StringPiece name, bool must_be_dir) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::Truncate(size_t len) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::Rename(fbl::RefPtr<Vnode> newdir, fbl::StringPiece oldname,
                          fbl::StringPiece newname, bool src_must_be_dir, bool dst_must_be_dir) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Link(fbl::StringPiece name, fbl::RefPtr<Vnode> target) {
  return ZX_ERR_NOT_SUPPORTED;
}

void Vnode::Sync(SyncCallback closure) { closure(ZX_ERR_NOT_SUPPORTED); }

#ifdef __Fuchsia__

zx_status_t Vnode::GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::QueryFilesystem(fio::FilesystemInfo* out) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Vnode::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::AttachRemote(MountChannel h) { return ZX_ERR_NOT_SUPPORTED; }

bool Vnode::IsRemote() const { return false; }

zx::channel Vnode::DetachRemote() { return zx::channel(); }

zx_handle_t Vnode::GetRemote() const { return ZX_HANDLE_INVALID; }

void Vnode::SetRemote(zx::channel remote) { ZX_DEBUG_ASSERT(false); }

#endif  // __Fuchsia__

DirentFiller::DirentFiller(void* ptr, size_t len)
    : ptr_(static_cast<char*>(ptr)), pos_(0), len_(len) {}

zx_status_t DirentFiller::Next(fbl::StringPiece name, uint8_t type, uint64_t ino) {
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
