// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local-connection.h"

#include <fcntl.h>
#include <lib/fdio/namespace.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <zircon/device/vfs.h>
#include <zircon/types.h>

#include <atomic>
#include <new>

#include <fbl/ref_ptr.h>

#include "../internal.h"
#include "local-filesystem.h"
#include "local-vnode.h"

namespace fdio_internal {
namespace {

namespace fio = ::llcpp::fuchsia::io;

// The directory represents a local directory (either "/" or
// some directory between "/" and a mount point), so it has
// to emulate directory behavior.
struct LocalConnection {
  // Hack to embed LocalConnection in the |storage| field of the |fdio_t| struct.
  // See definition of |zxio_storage_t|, where |zxio_t io| is also the first element.
  // This allows us to track extra state related to the local connection, as
  // witnessed by the fields below.
  zxio_t io;

  // For the following two fields, although these are raw pointers for
  // C compatibility, they are actually strong references to both the
  // namespace and vnode object.
  //
  // On close, they must be destroyed.

  // The namespace instance, containing a directory tree terminating
  // with various remote channels.
  const fdio_namespace* fs;

  // The vnode corresponding to this directory. |vn| references some
  // directory in |fs|.
  const LocalVnode* vn;
};

static_assert(offsetof(LocalConnection, io) == 0, "LocalConnection must be castable to zxio_t");
static_assert(offsetof(zxio_storage_t, io) == 0, "LocalConnection must be castable to zxio_t");

static_assert(sizeof(LocalConnection) <= sizeof(zxio_storage_t),
              "LocalConnection must fit inside zxio_storage_t.");

LocalConnection* fdio_get_local_dir(fdio_t* io) {
  return reinterpret_cast<LocalConnection*>(fdio_get_zxio(io));
}

zx_status_t local_dir_close(fdio_t* io) {
  LocalConnection* dir = fdio_get_local_dir(io);
  // Reclaim a strong reference to |fs| which was leaked during
  // |CreateLocalConnection()|
  __UNUSED auto fs = fbl::ImportFromRawPtr<const fdio_namespace>(dir->fs);
  __UNUSED auto vn = fbl::ImportFromRawPtr<const LocalVnode>(dir->vn);
  dir->fs = nullptr;
  dir->vn = nullptr;
  return ZX_OK;
}

// Expects a canonical path (no ..) with no leading
// slash and no trailing slash
zx_status_t local_dir_open(fdio_t* io, const char* path, uint32_t flags, uint32_t mode,
                           fdio_t** out) {
  LocalConnection* dir = fdio_get_local_dir(io);

  return dir->fs->Open(fbl::RefPtr(dir->vn), path, flags, mode, out);
}

zx_status_t local_dir_get_attr(fdio_t* io, zxio_node_attributes_t* attr) {
  *attr = {};
  ZXIO_NODE_ATTR_SET(*attr, protocols, ZXIO_NODE_PROTOCOL_DIRECTORY);
  ZXIO_NODE_ATTR_SET(
      *attr, abilities,
      ZXIO_OPERATION_ENUMERATE | ZXIO_OPERATION_TRAVERSE | ZXIO_OPERATION_GET_ATTRIBUTES);
  ZXIO_NODE_ATTR_SET(*attr, link_count, 1);
  return ZX_OK;
}

uint32_t local_dir_convert_to_posix_mode(fdio_t* io, zxio_node_protocols_t protocols,
                                         zxio_abilities_t abilities) {
  return zxio_node_protocols_to_posix_type(protocols) |
         zxio_abilities_to_posix_permissions_for_directory(abilities);
}

zx_status_t local_dir_unlink(fdio_t* io, const char* path, size_t len) {
  return ZX_ERR_UNAVAILABLE;
}

struct local_dir_dirent_iterator {
  // Buffer for storing dirents.
  void* buffer;

  // Size of |buffer|.
  size_t capacity;

  // Used by |Readdir| to resume from the middle of a directory.
  DirentIteratorState iterator_state;
};

zx_status_t local_dir_dirent_iterator_init(fdio_t* io, zxio_dirent_iterator_t* iterator,
                                           zxio_t* directory) {
  auto dir_iterator = new (iterator) local_dir_dirent_iterator;
  size_t capacity_of_one = sizeof(zxio_dirent_t) + fio::MAX_FILENAME + 1;
  dir_iterator->buffer = malloc(capacity_of_one);
  dir_iterator->capacity = capacity_of_one;
  return ZX_OK;
}

zx_status_t local_dir_dirent_iterator_next(fdio_t* io, zxio_dirent_iterator_t* iterator,
                                           zxio_dirent_t** out_entry) {
  LocalConnection* dir = fdio_get_local_dir(io);
  auto dir_iterator = reinterpret_cast<local_dir_dirent_iterator*>(iterator);
  zx_status_t status = dir->fs->Readdir(*dir->vn, &dir_iterator->iterator_state,
                                        dir_iterator->buffer, dir_iterator->capacity, out_entry);
  if (*out_entry == nullptr && status == ZX_OK) {
    return ZX_ERR_NOT_FOUND;
  }
  return status;
}

void local_dir_dirent_iterator_destroy(fdio_t* io, zxio_dirent_iterator_t* iterator) {
  auto dir_iterator = reinterpret_cast<local_dir_dirent_iterator*>(iterator);
  free(dir_iterator->buffer);
  static_assert(std::is_trivially_destructible<local_dir_dirent_iterator>::value,
                "local_dir_dirent_iterator must have trivial destructor");
}

constexpr fdio_ops_t kLocalConnectionOps = []() {
  fdio_ops_t ops = {};
  ops.get_attr = local_dir_get_attr;
  ops.close = local_dir_close;
  ops.open = local_dir_open;
  ops.clone = fdio_default_clone;
  ops.wait_begin = fdio_default_wait_begin;
  ops.wait_end = fdio_default_wait_end;
  ops.unwrap = fdio_default_unwrap;
  ops.borrow_channel = fdio_default_borrow_channel;
  ops.posix_ioctl = fdio_default_posix_ioctl;
  ops.get_token = fdio_default_get_token;
  ops.set_attr = fdio_default_set_attr;
  ops.convert_to_posix_mode = local_dir_convert_to_posix_mode;
  ops.dirent_iterator_init = local_dir_dirent_iterator_init;
  ops.dirent_iterator_next = local_dir_dirent_iterator_next;
  ops.dirent_iterator_destroy = local_dir_dirent_iterator_destroy;
  ops.unlink = local_dir_unlink;
  ops.truncate = fdio_default_truncate;
  ops.rename = fdio_default_rename;
  ops.link = fdio_default_link;
  ops.get_flags = fdio_default_get_flags;
  ops.set_flags = fdio_default_set_flags;
  ops.recvmsg = fdio_default_recvmsg;
  ops.sendmsg = fdio_default_sendmsg;
  ops.shutdown = fdio_default_shutdown;
  return ops;
}();

}  // namespace

fdio_t* CreateLocalConnection(fbl::RefPtr<const fdio_namespace> fs,
                              fbl::RefPtr<const LocalVnode> vn) {
  fdio_t* io = fdio_alloc(&kLocalConnectionOps);
  if (io == nullptr) {
    return nullptr;
  }
  // Invoke placement new on the new LocalConnection. Since the object is trivially
  // destructible, we can avoid invoking the destructor.
  static_assert(std::is_trivially_destructible<LocalConnection>::value,
                "LocalConnection must have trivial destructor");
  char* storage = reinterpret_cast<char*>(fdio_get_local_dir(io));
  LocalConnection* dir = new (storage) LocalConnection();
  zxio_null_init(&(fdio_get_zxio_storage(io)->io));

  // Leak a strong reference to |this| which will be reclaimed
  // in |zxio_dir_close()|.
  dir->fs = fbl::ExportToRawPtr(&fs);
  dir->vn = fbl::ExportToRawPtr(&vn);
  return io;
}

}  // namespace fdio_internal
