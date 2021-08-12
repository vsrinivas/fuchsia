// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local-connection.h"

#include <fcntl.h>
#include <lib/zxio/null.h>
#include <lib/zxio/zxio.h>
#include <zircon/types.h>

#include <atomic>
#include <new>

#include <fbl/ref_ptr.h>

#include "local-filesystem.h"
#include "local-vnode.h"

namespace fdio_internal {
namespace {

namespace fio = fuchsia_io;

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
  LocalVnode* vn;
};

static_assert(offsetof(LocalConnection, io) == 0, "LocalConnection must be castable to zxio_t");
static_assert(offsetof(zxio_storage_t, io) == 0, "LocalConnection must be castable to zxio_t");

static_assert(sizeof(LocalConnection) <= sizeof(zxio_storage_t),
              "LocalConnection must fit inside zxio_storage_t.");

struct local_connection : public base {
  LocalConnection& local_dir() { return *reinterpret_cast<LocalConnection*>(&zxio_storage().io); }

  zx_status_t close() override {
    auto& dir = local_dir();
    // Reclaim a strong reference to |fs| which was leaked during
    // |CreateLocalConnection()|
    __UNUSED auto fs = fbl::ImportFromRawPtr<const fdio_namespace>(dir.fs);
    __UNUSED auto vn = fbl::ImportFromRawPtr<const LocalVnode>(dir.vn);
    dir.fs = nullptr;
    dir.vn = nullptr;
    return ZX_OK;
  }

  zx_status_t clone(zx_handle_t* out_handle) override { return ZX_ERR_NOT_SUPPORTED; }

  // Expects a canonical path (no ..) with no leading
  // slash and no trailing slash
  zx::status<fdio_ptr> open(const char* path, uint32_t flags, uint32_t mode) override {
    auto& dir = local_dir();
    return dir.fs->Open(fbl::RefPtr(dir.vn), path, flags, mode);
  }

  zx_status_t add_inotify_filter(const char* path, uint32_t mask, uint32_t watch_descriptor,
                                 zx::socket socket) override {
    auto& dir = local_dir();
    return dir.fs->AddInotifyFilter(fbl::RefPtr(dir.vn), path, mask, watch_descriptor,
                                    std::move(socket));
  }

  zx_status_t get_attr(zxio_node_attributes_t* out) override {
    zxio_node_attributes_t attr = {};
    ZXIO_NODE_ATTR_SET(attr, protocols, ZXIO_NODE_PROTOCOL_DIRECTORY);
    ZXIO_NODE_ATTR_SET(
        attr, abilities,
        ZXIO_OPERATION_ENUMERATE | ZXIO_OPERATION_TRAVERSE | ZXIO_OPERATION_GET_ATTRIBUTES);
    ZXIO_NODE_ATTR_SET(attr, link_count, 1);
    *out = attr;
    return ZX_OK;
  }

  zx_status_t dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory) override {
    auto* dir_iterator = new (iterator) local_dir_dirent_iterator;
    size_t capacity_of_one = sizeof(zxio_dirent_t) + fio::wire::kMaxFilename + 1;
    dir_iterator->buffer = malloc(capacity_of_one);
    dir_iterator->capacity = capacity_of_one;
    return ZX_OK;
  }

  zx_status_t dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                   zxio_dirent_t** out_entry) override {
    auto& dir = local_dir();
    auto* dir_iterator = reinterpret_cast<local_dir_dirent_iterator*>(iterator);
    zx_status_t status = dir.fs->Readdir(*dir.vn, &dir_iterator->iterator_state,
                                         dir_iterator->buffer, dir_iterator->capacity, out_entry);
    if (*out_entry == nullptr && status == ZX_OK) {
      return ZX_ERR_NOT_FOUND;
    }
    return status;
  }

  void dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) override {
    auto* dir_iterator = reinterpret_cast<local_dir_dirent_iterator*>(iterator);
    free(dir_iterator->buffer);
  }

  zx_status_t unlink(const char* name, size_t len, int flags) override {
    return ZX_ERR_UNAVAILABLE;
  }

  bool is_local_dir() override { return true; }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<local_connection>;
  friend class fbl::RefPtr<local_connection>;

  local_connection() = default;
  ~local_connection() override = default;

 private:
  struct local_dir_dirent_iterator {
    // Buffer for storing dirents.
    void* buffer;

    // Size of |buffer|.
    size_t capacity;

    // Used by |Readdir| to resume from the middle of a directory.
    DirentIteratorState iterator_state;
  };
};

}  // namespace

zx::status<fdio_ptr> CreateLocalConnection(fbl::RefPtr<const fdio_namespace> fs,
                                           fbl::RefPtr<LocalVnode> vn) {
  fdio_ptr io = fbl::MakeRefCounted<local_connection>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  // Invoke placement new on the new LocalConnection. Since the object is trivially
  // destructible, we can avoid invoking the destructor.
  static_assert(std::is_trivially_destructible<LocalConnection>::value,
                "LocalConnection must have trivial destructor");
  zxio_storage_t& storage = io->zxio_storage();
  auto* dir = new (&storage) LocalConnection();
  zxio_null_init(&storage.io);

  // Leak a strong reference to |this| which will be reclaimed
  // in |zxio_dir_close()|.
  dir->fs = fbl::ExportToRawPtr(&fs);
  dir->vn = fbl::ExportToRawPtr(&vn);
  return zx::ok(io);
}

fbl::RefPtr<LocalVnode> GetLocalNodeFromConnectionIfAny(fdio_t* io) {
  if (!io->is_local_dir()) {
    return nullptr;
  }
  return fbl::RefPtr<LocalVnode>(reinterpret_cast<local_connection*>(io)->local_dir().vn);
}

}  // namespace fdio_internal
