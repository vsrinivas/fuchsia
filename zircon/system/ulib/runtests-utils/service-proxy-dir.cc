// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/vfs.h>
#include <zircon/status.h>

#include <string>

#include <runtests-utils/service-proxy-dir.h>

namespace fio = ::llcpp::fuchsia::io;

namespace runtests {

ServiceProxyDir::ServiceProxyDir(zx::channel proxy_dir) : proxy_dir_(std::move(proxy_dir)) {}

void ServiceProxyDir::AddEntry(std::string name, fbl::RefPtr<fs::Vnode> node) {
  entries_[name] = node;
}

zx_status_t ServiceProxyDir::GetAttributes(fs::VnodeAttributes* attr) {
  *attr = fs::VnodeAttributes();
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->inode = fio::INO_UNKNOWN;
  attr->link_count = 1;
  return ZX_OK;
}

zx_status_t ServiceProxyDir::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                                    [[maybe_unused]] fs::Rights rights,
                                                    fs::VnodeRepresentation* info) {
  *info = fs::VnodeRepresentation::Directory();
  return ZX_OK;
}

fs::VnodeProtocolSet ServiceProxyDir::GetProtocols() const { return fs::VnodeProtocol::kDirectory; }

zx_status_t ServiceProxyDir::Lookup(fbl::StringPiece name, fbl::RefPtr<fs::Vnode>* out) {
  std::unique_lock lock(lock_);
  auto entry_name = std::string(name.data(), name.length());

  auto entry = entries_.find(entry_name);
  if (entry != entries_.end()) {
    *out = entry->second;
    return ZX_OK;
  }

  entries_.emplace(
      entry_name, *out = fbl::MakeRefCounted<fs::Service>([this, entry_name](zx::channel request) {
        return fio::Directory::Call::Open(zx::unowned_channel(proxy_dir_),
                                          fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, 0755,
                                          fidl::unowned_str(entry_name), std::move(request))
            .status();
      }));

  return ZX_OK;
}

}  // namespace runtests
