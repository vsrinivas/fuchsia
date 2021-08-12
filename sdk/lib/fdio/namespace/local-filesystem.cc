// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local-filesystem.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fit/defer.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/channel.h>
#include <lib/zxio/types.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>

#include <new>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <sdk/lib/fdio/zxio.h>

#include "local-connection.h"
#include "local-vnode.h"

namespace fio = fuchsia_io;

namespace {

struct ExportState {
  // The minimum size of flat namespace which will contain all the
  // information about this |fdio_namespace|.
  size_t bytes;
  // The total number of entries (path + handle pairs) in this namespace.
  size_t count;
  // A (moving) pointer to start of the next path.
  char* buffer;
  zx_handle_t* handle;
  uint32_t* type;
  char** path;
};

zx_status_t ValidateName(const cpp17::string_view& name) {
  if ((name.length() == 0) || (name.length() > NAME_MAX)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (name == cpp17::string_view(".") || name == cpp17::string_view("..")) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

}  // namespace

fdio_namespace::fdio_namespace() : root_(LocalVnode::Create(nullptr, {}, "")) {}

fdio_namespace::~fdio_namespace() {
  fbl::AutoLock lock(&lock_);
  root_->Unlink();
}

zx_status_t fdio_namespace::WalkLocked(fbl::RefPtr<LocalVnode>* in_out_vn,
                                       const char** in_out_path) const {
  fbl::RefPtr<LocalVnode> vn = *in_out_vn;
  const char* path = *in_out_path;

  // Empty path or "." matches initial node.
  if ((path[0] == 0) || ((path[0] == '.') && (path[1] == 0))) {
    return ZX_OK;
  }

  for (;;) {
    // Find the next path segment.
    const char* name = path;
    const char* next = strchr(path, '/');
    size_t len = next ? static_cast<size_t>(next - path) : strlen(path);

    // Path segments may not be empty.
    if (len == 0) {
      return ZX_ERR_BAD_PATH;
    }

    // "." matches current node.
    if (!((path[0] == '.') && (path[1] == 0))) {
      fbl::RefPtr<LocalVnode> child = vn->Lookup(cpp17::string_view(name, len));
      if (child == nullptr) {
        // If no child exists with this name, we either failed to lookup a node,
        // or we must transmit this request to the remote node.
        if (!vn->Remote().is_valid()) {
          return ZX_ERR_NOT_FOUND;
        }

        *in_out_vn = vn;
        *in_out_path = path;
        return ZX_OK;
      }
      vn = child;
    }

    if (!next) {
      // Lookup has completed successfully for all nodes, and no path remains.
      // Return the requested local node.
      *in_out_vn = vn;
      *in_out_path = ".";
      return ZX_OK;
    }

    // Lookup completed successfully, but more segments exist.
    path = next + 1;
  }
}

// Open |path| relative to |vn|.
//
// |flags| and |mode| are passed to |fuchsia.io.Directory/Open| as |flags| and |mode|, respectively.
//
// If |flags| includes |ZX_FS_FLAG_DESCRIBE|, this function reads the resulting
// |fuchsia.io.Node/OnOpen| event from the newly created channel and creates an
// appropriate object to interact with the remote object.
//
// Otherwise, this function creates a generic "remote" object.
zx::status<fdio_ptr> fdio_namespace::Open(fbl::RefPtr<LocalVnode> vn, const char* path,
                                          uint32_t flags, uint32_t mode) const {
  {
    fbl::AutoLock lock(&lock_);
    zx_status_t status = WalkLocked(&vn, &path);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    if (!vn->Remote().is_valid()) {
      // The Vnode exists, but it has no remote object. Open a local reference.
      return CreateConnection(vn);
    }
  }

  // If we're trying to mkdir over top of a mount point,
  // the correct error is EEXIST
  if ((flags & ZX_FS_FLAG_CREATE) && !strcmp(path, ".")) {
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }

  size_t length;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  zx::status endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  // Active remote connections are immutable, so referencing remote here
  // is safe. We don't want to do a blocking open under the ns lock.
  status = fidl::WireCall(vn->Remote())
               .Open(flags, mode, fidl::StringView::FromExternal(path, length),
                     std::move(endpoints->server))
               .status();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  if (flags & ZX_FS_FLAG_DESCRIBE) {
    return fdio::create_with_on_open(std::move(endpoints->client));
  }

  return fdio_internal::remote::create(std::move(endpoints->client));
}

zx_status_t fdio_namespace::AddInotifyFilter(fbl::RefPtr<LocalVnode> vn, const char* path,
                                             uint32_t mask, uint32_t watch_descriptor,
                                             zx::socket socket) const {
  fbl::AutoLock lock(&lock_);
  zx_status_t status = WalkLocked(&vn, &path);
  if (status != ZX_OK) {
    return status;
  }

  if (!vn->Remote().is_valid()) {
    // The Vnode exists, but it has no remote object.
    // we simply return a ZX_ERR_NOT_SUPPORTED
    // as we do not support inotify for local-namespace filesystem
    // at the time.
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t length;
  status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }

  auto event_mask = static_cast<::fuchsia_io2::wire::InotifyWatchMask>(mask);
  // Active remote connections are immutable, so referencing remote here
  // is safe. But we do not want to do a blocking call under the ns lock.
  return fidl::WireCall(vn->Remote())
      .AddInotifyFilter(fidl::StringView::FromExternal(path, length), event_mask, watch_descriptor,
                        std::move(socket))
      .status();
}

zx_status_t fdio_namespace::Readdir(const LocalVnode& vn, DirentIteratorState* state, void* buffer,
                                    size_t length, zxio_dirent_t** out_entry) const {
  fbl::AutoLock lock(&lock_);

  auto populate_entry = [length](zxio_dirent_t* entry, cpp17::string_view name) {
    if (name.size() > NAME_MAX) {
      return ZX_ERR_INVALID_ARGS;
    }
    uint8_t name_size = static_cast<uint8_t>(name.size());
    *entry = {};
    ZXIO_DIRENT_SET(*entry, protocols, ZXIO_NODE_PROTOCOL_DIRECTORY);
    if (sizeof(zxio_dirent_t) + name_size + 1 > length) {
      return ZX_ERR_INVALID_ARGS;
    }
    entry->name_length = name_size;
    entry->name = reinterpret_cast<char*>(entry) + sizeof(zxio_dirent_t);
    memcpy(entry->name, name.data(), name_size);
    entry->name[name_size] = '\0';
    return ZX_OK;
  };

  if (!state->encountered_dot) {
    auto entry = reinterpret_cast<zxio_dirent_t*>(buffer);
    zx_status_t status = populate_entry(entry, cpp17::string_view("."));
    if (status != ZX_OK) {
      *out_entry = nullptr;
      return status;
    }
    *out_entry = entry;
    state->encountered_dot = true;
    return ZX_OK;
  }
  fbl::RefPtr<LocalVnode> child_vnode;
  vn.Readdir(&state->last_seen, &child_vnode);
  if (!child_vnode) {
    *out_entry = nullptr;
    return ZX_OK;
  }
  auto entry = reinterpret_cast<zxio_dirent_t*>(buffer);
  zx_status_t status = populate_entry(entry, child_vnode->Name());
  if (status != ZX_OK) {
    *out_entry = nullptr;
    return status;
  }
  *out_entry = entry;
  return ZX_OK;
}

zx::status<fdio_ptr> fdio_namespace::CreateConnection(fbl::RefPtr<LocalVnode> vn) const {
  return fdio_internal::CreateLocalConnection(fbl::RefPtr(this), std::move(vn));
}

zx_status_t fdio_namespace::Connect(const char* path, uint32_t flags,
                                    fidl::ClientEnd<fio::Node> client_end) const {
  // Require that we start at /
  if (path[0] != '/') {
    return ZX_ERR_NOT_FOUND;
  }
  path++;

  fbl::RefPtr<LocalVnode> vn;
  {
    fbl::AutoLock lock(&lock_);
    vn = root_;
    zx_status_t status = WalkLocked(&vn, &path);
    if (status != ZX_OK) {
      return status;
    }

    // cannot connect via non-mountpoint nodes
    if (!vn->Remote().is_valid()) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  return fdio_open_at(vn->Remote().channel().get(), path, flags, client_end.channel().release());
}

zx_status_t fdio_namespace::Unbind(const char* path) {
  if ((path == nullptr) || (path[0] != '/')) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Skip leading slash.
  path++;

  if (path[0] == 0) {
    // The path was "/" so we're trying to unbind to the root vnode.
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AutoLock lock(&lock_);
  fbl::RefPtr<LocalVnode> vn = root_;
  // If we remove a vnode, we may create one or more childless intermediate parent nodes.
  // This node denotes the "highest" such node in the filesystem hierarchy.
  fbl::RefPtr<LocalVnode> removable_origin_vn;

  for (;;) {
    const char* next = strchr(path, '/');
    cpp17::string_view name(path, next ? (next - path) : strlen(path));
    zx_status_t status = ValidateName(name);
    if (status != ZX_OK) {
      return status;
    }

    if (vn->Remote().is_valid()) {
      // Since shadowing is disallowed, this must refer to an invalid path.
      return ZX_ERR_NOT_FOUND;
    }

    vn = vn->Lookup(name);
    if (vn == nullptr) {
      return ZX_ERR_NOT_FOUND;
    }

    size_t children_count = 0;
    vn->ForAllChildren([&children_count](const LocalVnode& vn) {
      if (++children_count > 1) {
        return ZX_ERR_STOP;
      }
      return ZX_OK;
    });

    if (children_count > 1) {
      // If this node has multiple children (including something OTHER than the node
      // we're potentially unbinding), we shouldn't try to remove it while deleting
      // childless intermediate nodes.
      removable_origin_vn = nullptr;
    } else if (removable_origin_vn == nullptr) {
      // If this node has one or fewer children, it's a viable candidate for removal.
      // Only set this if it's the "highest" node we've seen satisfying this property.
      removable_origin_vn = vn;
    }

    if (!next) {
      // This is the last segment; we must match.
      if (!vn->Remote().is_valid()) {
        return ZX_ERR_NOT_FOUND;
      }
      // This assertion must hold without shadowing: |vn| should
      // have no children, so at minimum, |removable_origin_vn| = |vn|.
      ZX_DEBUG_ASSERT(removable_origin_vn != nullptr);
      removable_origin_vn->Unlink();
      return ZX_OK;
    }

    path = next + 1;
  }
}

bool fdio_namespace::IsBound(const char* path) {
  if ((path == nullptr) || (path[0] != '/')) {
    return false;
  }
  path++;

  fbl::AutoLock lock(&lock_);
  fbl::RefPtr<LocalVnode> vn = root_;
  zx_status_t status = WalkLocked(&vn, &path);
  if (status != ZX_OK) {
    return false;
  }
  return strcmp(path, ".") == 0 && vn->Remote().is_valid();
}

zx_status_t fdio_namespace::Bind(const char* path, fidl::ClientEnd<fio::Directory> remote) {
  if (!remote.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }
  if ((path == nullptr) || (path[0] != '/')) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Skip leading slash.
  path++;

  fbl::AutoLock lock(&lock_);
  if (path[0] == 0) {
    if (root_->Remote().is_valid()) {
      // Cannot re-bind after initial bind.
      return ZX_ERR_ALREADY_EXISTS;
    }
    if (root_->has_children()) {
      // Overlay remotes are disallowed.
      return ZX_ERR_NOT_SUPPORTED;
    }
    // The path was "/" so we're trying to bind to the root vnode.
    root_ = LocalVnode::Create(nullptr, std::move(remote), "");
    return ZX_OK;
  }

  zx_status_t status = ZX_OK;
  fbl::RefPtr<LocalVnode> vn = root_;
  fbl::RefPtr<LocalVnode> first_new_node = nullptr;

  // If we fail, but leave any intermediate nodes, we need to clean them up
  // before unlocking and returning.
  auto cleanup = fit::defer([&first_new_node]() {
    if (first_new_node != nullptr) {
      first_new_node->Unlink();
    }
  });

  for (;;) {
    const char* next = strchr(path, '/');
    cpp17::string_view name(path, next ? (next - path) : strlen(path));
    status = ValidateName(name);
    if (status != ZX_OK) {
      return status;
    }

    if (vn->Remote().is_valid()) {
      // Shadowing is disallowed.
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (next) {
      // Not the final segment.
      fbl::RefPtr<LocalVnode> child = vn->Lookup(name);
      if (child == nullptr) {
        // Create a new intermediate node.
        vn = LocalVnode::Create(vn, {}, fbl::String(name));

        // Keep track of the first node we create. If any subsequent
        // operation fails during bind, we will need to delete all nodes
        // in this subtree.
        if (first_new_node == nullptr) {
          first_new_node = vn;
        }
      } else {
        // Re-use an existing intermediate node.
        vn = child;
      }
      path = next + 1;
    } else {
      // Final segment. Create the leaf vnode and stop.
      if (vn->Lookup(name) != nullptr) {
        return ZX_ERR_ALREADY_EXISTS;
      }
      vn = LocalVnode::Create(vn, std::move(remote), fbl::String(name));
      break;
    }
  }

  cleanup.cancel();
  return ZX_OK;
}

zx::status<fdio_ptr> fdio_namespace::OpenRoot() const {
  fbl::RefPtr<LocalVnode> vn = [this]() {
    fbl::AutoLock lock(&lock_);
    return root_;
  }();

  if (!vn->Remote().is_valid()) {
    return CreateConnection(vn);
  }

  zx::status endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  zx_status_t status = fidl::WireCall(vn->Remote())
                           .Clone(fio::wire::kCloneFlagSameRights | fio::wire::kOpenFlagDescribe,
                                  std::move(endpoints->server))
                           .status();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return fdio::create_with_on_open(std::move(endpoints->client));
}

zx_status_t fdio_namespace::SetRoot(fdio_t* io) {
  fbl::RefPtr<LocalVnode> vn = fdio_internal::GetLocalNodeFromConnectionIfAny(io);

  if (!vn) {
    fidl::ClientEnd<fio::Directory> client_end;
    zx_status_t status = io->clone(client_end.channel().reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }

    vn = LocalVnode::Create(nullptr, std::move(client_end), "");
  }

  fbl::AutoLock lock(&lock_);
  if (vn == root_) {
    // Nothing to do.
    return ZX_OK;
  }

  vn->UnlinkFromParent();
  std::swap(root_, vn);
  vn->Unlink();
  return ZX_OK;
}

zx_status_t fdio_namespace::Export(fdio_flat_namespace_t** out) const {
  ExportState es;
  es.bytes = sizeof(fdio_flat_namespace_t);
  es.count = 0;

  fbl::RefPtr<LocalVnode> vn = [this]() {
    fbl::AutoLock lock(&lock_);
    return root_;
  }();

  auto count_callback = [&es](const cpp17::string_view& path,
                              const fidl::ClientEnd<fio::Directory>& client_end) {
    // Each entry needs one slot in the handle table,
    // one slot in the type table, and one slot in the
    // path table, plus storage for the path and NUL
    es.bytes += sizeof(zx_handle_t) + sizeof(uint32_t) + sizeof(char**) + path.length() + 1;
    es.count += 1;
    return ZX_OK;
  };
  if (zx_status_t status = fdio_internal::EnumerateRemotes(*vn, count_callback); status != ZX_OK) {
    return status;
  }

  fdio_flat_namespace_t* flat = static_cast<fdio_flat_namespace_t*>(malloc(es.bytes));
  if (flat == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  // We've allocated enough memory for the flat struct
  // followed by count handles, followed by count types,
  // followed by count path ptrs followed by enough bytes
  // for all the path strings.  Point es.* at the right
  // slices of that memory:
  es.handle = reinterpret_cast<zx_handle_t*>(flat + 1);
  es.type = reinterpret_cast<uint32_t*>(es.handle + es.count);
  es.path = reinterpret_cast<char**>(es.type + es.count);
  es.buffer = reinterpret_cast<char*>(es.path + es.count);
  es.count = 0;

  auto export_callback = [&es](const cpp17::string_view& path,
                               const fidl::ClientEnd<fio::Directory>& client_end) {
    zx::channel remote(fdio_service_clone(client_end.channel().get()));
    if (!remote.is_valid()) {
      return ZX_ERR_BAD_STATE;
    }
    strlcpy(es.buffer, path.data(), path.length() + 1);
    es.path[es.count] = es.buffer;
    es.handle[es.count] = remote.release();
    es.type[es.count] = PA_HND(PA_NS_DIR, static_cast<uint32_t>(es.count));
    es.buffer += (path.length() + 1);
    es.count++;
    return ZX_OK;
  };

  if (zx_status_t status = fdio_internal::EnumerateRemotes(*vn, export_callback); status != ZX_OK) {
    zx_handle_close_many(es.handle, es.count);
    free(flat);
    return status;
  }

  flat->count = es.count;
  flat->handle = es.handle;
  flat->type = es.type;
  flat->path = es.path;
  *out = flat;
  return ZX_OK;
}
