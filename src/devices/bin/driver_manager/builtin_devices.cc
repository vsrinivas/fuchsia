// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/builtin_devices.h"

#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace {
namespace fio = fuchsia_io;
BuiltinDevices* instance = nullptr;
}  // namespace

zx_status_t BuiltinDevVnode::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  if (null_) {
    // /dev/null implementation.
    *out_actual = 0;
    return ZX_OK;
  } else {
    // /dev/zero implementation.
    memset(data, 0, len);
    *out_actual = len;
    return ZX_OK;
  }
}

zx_status_t BuiltinDevVnode::Write(const void* data, size_t len, size_t off, size_t* out_actual) {
  if (null_) {
    *out_actual = len;
    return ZX_OK;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t BuiltinDevVnode::GetAttributes(fs::VnodeAttributes* a) {
  a->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
  a->content_size = 0;
  a->link_count = 1;
  return ZX_OK;
}

void BuiltinDevVnode::HandleFsSpecificMessage(fidl::IncomingMessage& msg, fidl::Transaction* txn) {
  fidl::WireDispatch(this, std::move(msg), txn);
}

fs::VnodeProtocolSet BuiltinDevVnode::GetProtocols() const {
  return fs::VnodeProtocol::kDevice | fs::VnodeProtocol::kDirectory;
}

fs::VnodeProtocol BuiltinDevVnode::Negotiate(fs::VnodeProtocolSet protocols) const {
  if ((protocols & fs::VnodeProtocol::kDevice).any()) {
    return fs::VnodeProtocol::kDevice;
  }
  return fs::VnodeProtocol::kDirectory;
}
zx_status_t BuiltinDevVnode::GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                                    fs::VnodeRepresentation* info) {
  if (protocol == fs::VnodeProtocol::kDevice) {
    *info = fs::VnodeRepresentation::Device{};
    return ZX_OK;
  }
  if (protocol == fs::VnodeProtocol::kDirectory) {
    *info = fs::VnodeRepresentation::Directory{};
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void BuiltinDevVnode::Open(OpenRequestView request, OpenCompleter::Sync& completer) {
  if (request->path.get() == ".") {
    // We need to support opening ourselves again, because this is the mechanism used by
    // V1 components when they route /dev/null or /dev/zero to themselves.
    // It's safe to access instance directly, because |BuiltinDevVnode| is only instantiated through
    // it.
    ZX_DEBUG_ASSERT(instance != nullptr);
    instance->HandleOpen(request->flags, std::move(request->object),
                         null_ ? kNullDevName : kZeroDevName);
    return;
  }
  LOGF(ERROR, "%s: not implemented for builtin device", __func__);
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}
void BuiltinDevVnode::ReadDirents(ReadDirentsRequestView request,
                                  ReadDirentsCompleter::Sync& completer) {
  LOGF(ERROR, "%s: not implemented for builtin device", __func__);
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}
void BuiltinDevVnode::Rewind(RewindRequestView request, RewindCompleter::Sync& completer) {
  LOGF(ERROR, "%s: not implemented for builtin device", __func__);
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}
void BuiltinDevVnode::GetToken(GetTokenRequestView request, GetTokenCompleter::Sync& completer) {
  LOGF(ERROR, "%s: not implemented for builtin device", __func__);
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}
void BuiltinDevVnode::Link(LinkRequestView request, LinkCompleter::Sync& completer) {
  LOGF(ERROR, "%s: not implemented for builtin device", __func__);
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}
void BuiltinDevVnode::Watch(WatchRequestView request, WatchCompleter::Sync& completer) {
  LOGF(ERROR, "%s: not implemented for builtin device", __func__);
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}

BuiltinDevices* BuiltinDevices::Get(async_dispatcher_t* dispatcher) {
  if (instance == nullptr) {
    instance = new BuiltinDevices(dispatcher);
  }
  return instance;
}

void BuiltinDevices::Reset() {
  delete instance;
  instance = nullptr;
}

zx_status_t BuiltinDevices::HandleOpen(fio::OpenFlags flags, fidl::ServerEnd<fio::Node> request,
                                       std::string_view name) {
  auto options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);

  fbl::RefPtr<fs::Vnode> vnode;
  if (name == kNullDevName) {
    vnode = null_dev_;
  } else if (name == kZeroDevName) {
    vnode = zero_dev_;
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<fs::Vnode> target;
  if (!options.flags.node_reference) {
    zx_status_t status = vnode->OpenValidating(options, &target);
    if (status != ZX_OK) {
      return status;
    }
  }
  if (target == nullptr) {
    target = vnode;
  }

  return vfs_.Serve(std::move(target), request.TakeChannel(), options);
}
