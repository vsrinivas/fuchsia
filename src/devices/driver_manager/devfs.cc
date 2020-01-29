// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devfs.h"

#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/txn_header.h>
#include <lib/memfs/cpp/vnode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/vfs.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/driver.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <src/storage/deprecated-fs-fidl-handler/fidl-handler.h>

#include "async-loop-owned-rpc-handler.h"
#include "coordinator.h"
#include "lib/fidl/cpp/message_part.h"
#include "log.h"

namespace devmgr {

namespace {

// `OnOpen` event from fuchsia-io.  See zircon/system/fidl/fuchsia-io/io.fidl.
struct OnOpenMsg {
  fuchsia_io_NodeOnOpenEvent primary;
  fuchsia_io_NodeInfo extra;
};

zx_status_t SendOnOpenEvent(zx_handle_t ch, OnOpenMsg msg, zx_handle_t* handles,
                            uint32_t num_handles) {
  msg.primary.hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
  auto contains_nodeinfo = bool(msg.primary.info);
  uint32_t msg_size = contains_nodeinfo ? sizeof(msg) : sizeof(msg.primary);
  fidl::Message fidl_msg(fidl::BytePart(reinterpret_cast<uint8_t*>(&msg), msg_size, msg_size),
                         fidl::HandlePart(handles, num_handles, num_handles));
  return fidl_msg.WriteTransformV1(ch, 0, &fuchsia_io_NodeOnOpenEventTable);
}

uint64_t next_ino = 2;

std::unique_ptr<Devnode> root_devnode;

std::unique_ptr<Devnode> class_devnode;

std::unique_ptr<Devnode> devfs_mkdir(Devnode* parent, const fbl::String& name);

zx::channel g_devfs_root;

}  // namespace

struct Watcher : fbl::DoublyLinkedListable<std::unique_ptr<Watcher>> {
  Watcher(Devnode* dn, zx::channel ch, uint32_t mask);

  Watcher(const Watcher&) = delete;
  Watcher& operator=(const Watcher&) = delete;

  Watcher(Watcher&&) = delete;
  Watcher& operator=(Watcher&&) = delete;

  Devnode* devnode = nullptr;
  zx::channel handle;
  uint32_t mask = 0;
};

Watcher::Watcher(Devnode* dn, zx::channel ch, uint32_t mask)
    : devnode(dn), handle(std::move(ch)), mask(mask) {}

class DcIostate : public fbl::DoublyLinkedListable<DcIostate*>,
                  public AsyncLoopOwnedRpcHandler<DcIostate> {
 public:
  explicit DcIostate(Devnode* dn);
  ~DcIostate();

  // Remove this DcIostate from its devnode
  void DetachFromDevnode();

  // Claims ownership of |*h| on success
  static zx_status_t Create(Devnode* dn, async_dispatcher_t* dispatcher, zx::channel* h);

  static zx_status_t DevfsFidlHandler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie,
                                      async_dispatcher_t* dispatcher);

  static void HandleRpc(std::unique_ptr<DcIostate> ios, async_dispatcher_t* dispatcher,
                        async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

 private:
  uint64_t readdir_ino_ = 0;

  // pointer to our devnode, nullptr if it has been removed
  Devnode* devnode_ = nullptr;
};

// BUG(ZX-2868): We currently never free these after allocating them
struct Devnode : public fbl::DoublyLinkedListable<Devnode*> {
  explicit Devnode(fbl::String name);
  ~Devnode();

  Devnode(const Devnode&) = delete;
  Devnode& operator=(const Devnode&) = delete;

  Devnode(Devnode&&) = delete;
  Devnode& operator=(Devnode&&) = delete;

  fbl::String name;
  uint64_t ino = 0;

  // nullptr if we are a pure directory node,
  // otherwise the device we are referencing
  Device* device = nullptr;

  fbl::DoublyLinkedList<std::unique_ptr<Watcher>> watchers;

  // list of our child devnodes
  fbl::DoublyLinkedList<Devnode*> children;

  // Pointer to our parent, for removing ourselves from its list of
  // children. Our parent must outlive us.
  Devnode* parent = nullptr;

  // list of attached iostates
  fbl::DoublyLinkedList<DcIostate*> iostate;

  // used to assign unique small device numbers
  // for class device links
  uint32_t seqcount = 0;
};

namespace {

struct ProtocolInfo {
  const char* name;
  Devnode* devnode;
  uint32_t id;
  uint32_t flags;
};

ProtocolInfo proto_infos[] = {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) {name, nullptr, val, flags},
#include <ddk/protodefs.h>
};

Devnode* proto_dir(uint32_t id) {
  for (const auto& info : proto_infos) {
    if (info.id == id) {
      return info.devnode;
    }
  }
  return nullptr;
}

void prepopulate_protocol_dirs() {
  class_devnode = devfs_mkdir(root_devnode.get(), "class");
  for (auto& info : proto_infos) {
    if (!(info.flags & PF_NOPUB)) {
      info.devnode = devfs_mkdir(class_devnode.get(), info.name).release();
    }
  }
}

void describe_error(zx::channel h, zx_status_t status) {
  OnOpenMsg msg;
  memset(&msg, 0, sizeof(msg));
  fidl_init_txn_header(&msg.primary.hdr, 0, fuchsia_io_NodeOnOpenOrdinal);
  msg.primary.s = status;
  SendOnOpenEvent(h.get(), msg, nullptr, 0);
}

// A devnode is a directory (from stat's perspective) if
// it has children, or if it doesn't have a device, or if
// its device has no rpc handle
bool devnode_is_dir(const Devnode* dn) {
  if (dn->children.is_empty()) {
    return (dn->device == nullptr) || (!dn->device->device_controller().channel().is_valid()) ||
           (!dn->device->channel()->is_valid());
  }
  return true;
}

// Local devnodes are ones that we should not hand off OPEN
// RPCs to the underlying devhost
bool devnode_is_local(Devnode* dn) {
  if (dn->device == nullptr) {
    return true;
  }
  if (!dn->device->device_controller().channel().get()) {
    return true;
  }
  if (dn->device->flags & DEV_CTX_MUST_ISOLATE) {
    return true;
  }
  return false;
}

// Notify a single watcher about the given operation and path.  On failure,
// frees the watcher.  This can only be called on a watcher that has not yet
// been added to a Devnode's watchers list.
void devfs_notify_single(std::unique_ptr<Watcher>* watcher, const fbl::String& name, unsigned op) {
  size_t len = name.length();
  if (!*watcher || len > fuchsia_io_MAX_FILENAME) {
    return;
  }

  ZX_ASSERT(!(*watcher)->InContainer());

  uint8_t msg[fuchsia_io_MAX_FILENAME + 2];
  const uint32_t msg_len = static_cast<uint32_t>(len + 2);
  msg[0] = static_cast<uint8_t>(op);
  msg[1] = static_cast<uint8_t>(len);
  memcpy(msg + 2, name.c_str(), len);

  // convert to mask
  op = (1u << op);

  if (!((*watcher)->mask & op)) {
    return;
  }

  if ((*watcher)->handle.write(0, msg, msg_len, nullptr, 0) != ZX_OK) {
    watcher->reset();
  }
}

void devfs_notify(Devnode* dn, const fbl::String& name, unsigned op) {
  if (dn->watchers.is_empty()) {
    return;
  }

  size_t len = name.length();
  if (len > fuchsia_io_MAX_FILENAME) {
    return;
  }

  uint8_t msg[fuchsia_io_MAX_FILENAME + 2];
  const uint32_t msg_len = static_cast<uint32_t>(len + 2);
  msg[0] = static_cast<uint8_t>(op);
  msg[1] = static_cast<uint8_t>(len);
  memcpy(msg + 2, name.c_str(), len);

  // convert to mask
  op = (1u << op);

  for (auto itr = dn->watchers.begin(); itr != dn->watchers.end();) {
    auto& cur = *itr;
    // Advance the iterator now instead of at the end of the loop because we
    // may erase the current element from the list.
    ++itr;

    if (!(cur.mask & op)) {
      continue;
    }

    if (cur.handle.write(0, msg, msg_len, nullptr, 0) != ZX_OK) {
      dn->watchers.erase(cur);
      // The Watcher is free'd here
    }
  }
}

}  // namespace

zx_status_t devfs_watch(Devnode* dn, zx::channel h, uint32_t mask) {
  auto watcher = std::make_unique<Watcher>(dn, std::move(h), mask);
  if (watcher == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  // If the watcher has asked for all existing entries, send it all of them
  // followed by the end-of-existing marker (IDLE).
  if (mask & fuchsia_io_WATCH_MASK_EXISTING) {
    for (const auto& child : dn->children) {
      if (child.device && (child.device->flags & DEV_CTX_INVISIBLE)) {
        continue;
      }
      // TODO: send multiple per write
      devfs_notify_single(&watcher, child.name, fuchsia_io_WATCH_EVENT_EXISTING);
    }
    devfs_notify_single(&watcher, "", fuchsia_io_WATCH_EVENT_IDLE);
  }

  // Watcher may have been freed by devfs_notify_single, so check before
  // adding.
  if (watcher) {
    dn->watchers.push_front(std::move(watcher));
  }
  return ZX_OK;
}

namespace {

std::unique_ptr<Devnode> devfs_mknode(const fbl::RefPtr<Device>& dev, const fbl::String& name) {
  auto dn = std::make_unique<Devnode>(name);
  if (!dn) {
    return nullptr;
  }
  dn->ino = next_ino++;
  // TODO(teisenbe): This should probably be refcounted
  dn->device = dev.get();
  return dn;
}

std::unique_ptr<Devnode> devfs_mkdir(Devnode* parent, const fbl::String& name) {
  std::unique_ptr<Devnode> dn = devfs_mknode(nullptr, name);
  if (dn == nullptr) {
    return nullptr;
  }
  dn->parent = parent;
  parent->children.push_back(dn.get());
  return dn;
}

Devnode* devfs_lookup(Devnode* parent, const fbl::String& name) {
  for (auto& child : parent->children) {
    if (name == child.name) {
      return &child;
    }
  }
  return nullptr;
}

zx_status_t fill_dirent(vdirent_t* de, size_t delen, uint64_t ino, const fbl::String& name,
                        uint8_t type) {
  size_t len = name.length();
  size_t sz = sizeof(vdirent_t) + len;

  if (sz > delen || len > NAME_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }
  de->ino = ino;
  de->size = static_cast<uint8_t>(len);
  de->type = type;
  memcpy(de->name, name.c_str(), len);
  return static_cast<zx_status_t>(sz);
}

zx_status_t devfs_readdir(Devnode* dn, uint64_t* ino_inout, void* data, size_t len) {
  char* ptr = static_cast<char*>(data);
  uint64_t ino = *ino_inout;

  for (const auto& child : dn->children) {
    if (child.ino <= ino) {
      continue;
    }
    if (child.device == nullptr) {
      // "pure" directories (like /dev/class/$NAME) do not show up
      // if they have no children, to avoid clutter and confusion.
      // They remain openable, so they can be watched.
      if (child.children.is_empty()) {
        continue;
      }
    } else {
      // invisible devices also do not show up
      if (child.device->flags & DEV_CTX_INVISIBLE) {
        continue;
      }
    }
    ino = child.ino;
    auto vdirent = reinterpret_cast<vdirent_t*>(ptr);
    zx_status_t r = fill_dirent(vdirent, len, ino, child.name, VTYPE_TO_DTYPE(V_TYPE_DIR));
    if (r < 0) {
      break;
    }
    ptr += r;
    len -= r;
  }

  *ino_inout = ino;
  return static_cast<zx_status_t>(ptr - static_cast<char*>(data));
}

zx_status_t devfs_walk(Devnode** dn_inout, char* path) {
  Devnode* dn = *dn_inout;

again:
  if ((path == nullptr) || (path[0] == 0)) {
    *dn_inout = dn;
    return ZX_OK;
  }
  char* name = path;
  if ((path = strchr(path, '/')) != nullptr) {
    *path++ = 0;
  }
  if (name[0] == 0) {
    return ZX_ERR_BAD_PATH;
  }
  for (auto& child : dn->children) {
    if (!strcmp(child.name.c_str(), name)) {
      if (child.device && (child.device->flags & DEV_CTX_INVISIBLE)) {
        continue;
      }
      dn = &child;
      goto again;
    }
  }
  // The path only partially matched.
  return ZX_ERR_NOT_FOUND;
}

void devfs_open(Devnode* dirdn, async_dispatcher_t* dispatcher, zx_handle_t h, char* path,
                uint32_t flags) {
  zx::channel ipc(h);
  h = ZX_HANDLE_INVALID;

  if (!strcmp(path, ".")) {
    path = nullptr;
  }

  Devnode* dn = dirdn;
  zx_status_t r = devfs_walk(&dn, path);

  bool describe = flags & ZX_FS_FLAG_DESCRIBE;
  if (r != ZX_OK) {
    if (describe) {
      describe_error(std::move(ipc), r);
    }
    return;
  }

  // If we are a local-only node, or we are asked to not go remote, or we are asked to
  // open-as-a-directory, open locally:
  if (devnode_is_local(dn) || flags & (ZX_FS_FLAG_NOREMOTE | ZX_FS_FLAG_DIRECTORY)) {
    zx::unowned_channel unowned_ipc(ipc);
    if ((r = DcIostate::Create(dn, dispatcher, &ipc)) != ZX_OK) {
      if (describe) {
        describe_error(std::move(ipc), r);
      }
      return;
    }
    if (describe) {
      OnOpenMsg msg;
      memset(&msg, 0, sizeof(msg));
      fidl_init_txn_header(&msg.primary.hdr, 0, fuchsia_io_NodeOnOpenOrdinal);
      msg.primary.hdr.flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
      msg.primary.s = ZX_OK;
      msg.primary.info = (fuchsia_io_NodeInfo*)FIDL_ALLOC_PRESENT;
      msg.extra.tag = fuchsia_io_NodeInfoTag_directory;

      // Writing to unowned_ipc is safe because this is executing on the same
      // thread as the DcAsyncLoop(), so the handle can't be closed underneath us.
      SendOnOpenEvent(unowned_ipc->get(), msg, nullptr, 0);
    }
    return;
  }

  // Otherwise we will pass the request on to the remote.
  fuchsia_io_DirectoryOpen(dn->device->device_controller().channel().get(), flags, 0, ".", 1,
                           ipc.release());
}

void devfs_remove(Devnode* dn) {
  if (dn->InContainer()) {
    dn->parent->children.erase(*dn);
  }

  // detach all connected iostates
  while (!dn->iostate.is_empty()) {
    dn->iostate.front().DetachFromDevnode();
  }

  // notify own file watcher
  if ((dn->device == nullptr) || !(dn->device->flags & DEV_CTX_INVISIBLE)) {
    devfs_notify(dn, "", fuchsia_io_WATCH_EVENT_DELETED);
  }

  // disconnect from device and notify parent/link directory watchers
  if (dn->device != nullptr) {
    if (dn->device->self == dn) {
      dn->device->self = nullptr;

      if ((dn->device->parent() != nullptr) && (dn->device->parent()->self != nullptr) &&
          !(dn->device->flags & DEV_CTX_INVISIBLE)) {
        devfs_notify(dn->device->parent()->self, dn->name, fuchsia_io_WATCH_EVENT_REMOVED);
      }
    }
    if (dn->device->link == dn) {
      dn->device->link = nullptr;

      if (!(dn->device->flags & DEV_CTX_INVISIBLE)) {
        Devnode* dir = proto_dir(dn->device->protocol_id());
        devfs_notify(dir, dn->name, fuchsia_io_WATCH_EVENT_REMOVED);
      }
    }
    dn->device = nullptr;
  }

  // destroy all watchers
  dn->watchers.clear();

  // detach children
  // They will be unpublished when the devices they're associated with are
  // eventually destroyed.
  dn->children.clear();
}

}  // namespace

Devnode::Devnode(fbl::String name) : name(std::move(name)) {}

Devnode::~Devnode() { devfs_remove(this); }

DcIostate::DcIostate(Devnode* dn) : devnode_(dn) { devnode_->iostate.push_back(this); }

DcIostate::~DcIostate() { DetachFromDevnode(); }

void DcIostate::DetachFromDevnode() {
  if (devnode_ != nullptr) {
    devnode_->iostate.erase(*this);
    devnode_ = nullptr;
  }
  set_channel(zx::channel());
}

zx_status_t DcIostate::Create(Devnode* dn, async_dispatcher_t* dispatcher, zx::channel* ipc) {
  auto ios = std::make_unique<DcIostate>(dn);
  if (ios == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  ios->set_channel(std::move(*ipc));
  zx_status_t status = DcIostate::BeginWait(&ios, dispatcher);
  if (status != ZX_OK) {
    // Take the handle back from |ios| so it doesn't close it when it's
    // destroyed
    *ipc = ios->set_channel(zx::channel());
  }
  return status;
}

void devfs_advertise(const fbl::RefPtr<Device>& dev) {
  if (dev->link) {
    Devnode* dir = proto_dir(dev->protocol_id());
    devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_ADDED);
  }
  if (dev->self->parent) {
    devfs_notify(dev->self->parent, dev->self->name, fuchsia_io_WATCH_EVENT_ADDED);
  }
}

// TODO: generate a MODIFIED event rather than back to back REMOVED and ADDED
void devfs_advertise_modified(const fbl::RefPtr<Device>& dev) {
  if (dev->link) {
    Devnode* dir = proto_dir(dev->protocol_id());
    devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_REMOVED);
    devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_ADDED);
  }
  if (dev->self->parent) {
    devfs_notify(dev->self->parent, dev->self->name, fuchsia_io_WATCH_EVENT_REMOVED);
    devfs_notify(dev->self->parent, dev->self->name, fuchsia_io_WATCH_EVENT_ADDED);
  }
}

zx_status_t devfs_publish(const fbl::RefPtr<Device>& parent, const fbl::RefPtr<Device>& dev) {
  if ((parent->self == nullptr) || (dev->self != nullptr) || (dev->link != nullptr)) {
    return ZX_ERR_INTERNAL;
  }

  std::unique_ptr<Devnode> dnself = devfs_mknode(dev, dev->name());
  if (dnself == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((dev->protocol_id() == ZX_PROTOCOL_TEST_PARENT) ||
      (dev->protocol_id() == ZX_PROTOCOL_MISC_PARENT) || (dev->protocol_id() == ZX_PROTOCOL_MISC)) {
    // misc devices are singletons, not a class
    // in the sense of other device classes.
    // They do not get aliases in /dev/class/misc/...
    // instead they exist only under their parent
    // device.
    goto done;
  }

  // Create link in /dev/class/... if this id has a published class
  Devnode* dir;
  dir = proto_dir(dev->protocol_id());
  if (dir != nullptr) {
    char tmp[32];
    const char* name = dev->name().data();

    if (dev->protocol_id() != ZX_PROTOCOL_CONSOLE) {
      for (unsigned n = 0; n < 1000; n++) {
        snprintf(tmp, sizeof(tmp), "%03u", (dir->seqcount++) % 1000);
        if (devfs_lookup(dir, tmp) == nullptr) {
          name = tmp;
          goto got_name;
        }
      }
      return ZX_ERR_ALREADY_EXISTS;
    }

  got_name:
    std::unique_ptr<Devnode> dnlink = devfs_mknode(dev, name);
    if (dnlink == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }

    // add link node to class directory
    dnlink->parent = dir;
    dir->children.push_back(dnlink.get());
    dev->link = dnlink.release();
  }

done:
  // add self node to parent directory
  dnself->parent = parent->self;
  parent->self->children.push_back(dnself.get());
  dev->self = dnself.release();

  if (!(dev->flags & DEV_CTX_INVISIBLE)) {
    devfs_advertise(dev);
  }
  return ZX_OK;
}

// TODO(teisenbe): Ideally this would take a RefPtr, but currently this is
// invoked in the dtor for Device.
void devfs_unpublish(Device* dev) {
  if (dev->self != nullptr) {
    delete dev->self;
    dev->self = nullptr;
  }
  if (dev->link != nullptr) {
    delete dev->link;
    dev->link = nullptr;
  }
}

zx_status_t devfs_connect(const Device* dev, zx::channel client_remote) {
  if (!client_remote.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }
  fuchsia_io_DirectoryOpen(dev->device_controller().channel().get(), 0 /* flags */, 0 /* mode */,
                           ".", 1, client_remote.release());
  return ZX_OK;
}

// Helper macros for |DevfsFidlHandler| which make it easier
// avoid typing generated names.

// Decode the incoming request, returning an error and consuming
// all handles on error.
#define DECODE_REQUEST(MSG, METHOD)                                                         \
  do {                                                                                      \
    zx_status_t r;                                                                          \
    if ((r = fidl_decode_msg(&fuchsia_io_##METHOD##RequestTable, msg, nullptr)) != ZX_OK) { \
      return r;                                                                             \
    }                                                                                       \
  } while (0);

// Define a variable |request| from the incoming method, of
// the requested type.
#define DEFINE_REQUEST(MSG, METHOD) \
  fuchsia_io_##METHOD##Request* request = (fuchsia_io_##METHOD##Request*)MSG->bytes;

zx_status_t DcIostate::DevfsFidlHandler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie,
                                        async_dispatcher_t* dispatcher) {
  auto ios = static_cast<DcIostate*>(cookie);
  Devnode* dn = ios->devnode_;
  if (dn == nullptr) {
    return ZX_ERR_PEER_CLOSED;
  }

  auto hdr = static_cast<fidl_message_header_t*>(msg->bytes);

  zx_status_t r;
  // This is an if statement because, depending on the state of the ordinal
  // migration, GenOrdinal and Ordinal may be the same value.  See FIDL-524.
  uint64_t ordinal = hdr->ordinal;
  if (ordinal == fuchsia_io_NodeCloneOrdinal || ordinal == fuchsia_io_NodeCloneGenOrdinal) {
    DECODE_REQUEST(msg, NodeClone);
    DEFINE_REQUEST(msg, NodeClone);
    zx_handle_t h = request->object;
    uint32_t flags = request->flags;
    if (request->flags & ZX_FS_FLAG_CLONE_SAME_RIGHTS) {
      flags |= ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE;
    }
    char path[] = ".";
    devfs_open(dn, dispatcher, h, path, flags | ZX_FS_FLAG_NOREMOTE);
    return ZX_OK;
  } else if (ordinal == fuchsia_io_NodeDescribeOrdinal ||
             ordinal == fuchsia_io_NodeDescribeGenOrdinal) {
    DECODE_REQUEST(msg, NodeDescribe);
    fuchsia_io_NodeInfo info;
    memset(&info, 0, sizeof(info));
    info.tag = fuchsia_io_NodeInfoTag_directory;
    return fuchsia_io_NodeDescribe_reply(txn, &info);
  } else if (ordinal == fuchsia_io_DirectoryOpenOrdinal ||
             ordinal == fuchsia_io_DirectoryOpenGenOrdinal) {
    DECODE_REQUEST(msg, DirectoryOpen);
    DEFINE_REQUEST(msg, DirectoryOpen);
    uint32_t len = static_cast<uint32_t>(request->path.size);
    zx_handle_t h = request->object;
    uint32_t flags = request->flags;
    if (len == 0 || len > fuchsia_io_MAX_PATH) {
      zx_handle_close(h);
    } else {
      char path[fuchsia_io_MAX_PATH + 1];
      memcpy(path, request->path.data, len);
      path[len] = 0;
      devfs_open(dn, dispatcher, h, path, flags);
    }
    return ZX_OK;
  } else if (ordinal == fuchsia_io_NodeGetAttrOrdinal ||
             ordinal == fuchsia_io_NodeGetAttrGenOrdinal) {
    DECODE_REQUEST(msg, NodeGetAttr);
    uint32_t mode;
    if (devnode_is_dir(dn)) {
      mode = V_TYPE_DIR | V_IRUSR | V_IWUSR;
    } else {
      mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    }

    fuchsia_io_NodeAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.mode = mode;
    attributes.content_size = 0;
    attributes.link_count = 1;
    attributes.id = dn->ino;
    return fuchsia_io_NodeGetAttr_reply(txn, ZX_OK, &attributes);
  } else if (ordinal == fuchsia_io_DirectoryRewindOrdinal ||
             ordinal == fuchsia_io_DirectoryRewindGenOrdinal) {
    DECODE_REQUEST(msg, DirectoryRewind);
    ios->readdir_ino_ = 0;
    return fuchsia_io_DirectoryRewind_reply(txn, ZX_OK);
  } else if (ordinal == fuchsia_io_DirectoryReadDirentsOrdinal ||
             ordinal == fuchsia_io_DirectoryReadDirentsGenOrdinal) {
    DECODE_REQUEST(msg, DirectoryReadDirents);
    DEFINE_REQUEST(msg, DirectoryReadDirents);

    if (request->max_bytes > fuchsia_io_MAX_BUF) {
      return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
    }

    uint8_t data[fuchsia_io_MAX_BUF];
    size_t actual = 0;
    r = devfs_readdir(dn, &ios->readdir_ino_, data, request->max_bytes);
    if (r >= 0) {
      actual = r;
      r = ZX_OK;
    }
    return fuchsia_io_DirectoryReadDirents_reply(txn, r, data, actual);
  } else if (ordinal == fuchsia_io_DirectoryWatchOrdinal ||
             ordinal == fuchsia_io_DirectoryWatchGenOrdinal) {
    DECODE_REQUEST(msg, DirectoryWatch);
    DEFINE_REQUEST(msg, DirectoryWatch);
    zx::channel watcher(request->watcher);

    request->watcher = ZX_HANDLE_INVALID;
    if (request->mask & (~fuchsia_io_WATCH_MASK_ALL) || request->options != 0) {
      return fuchsia_io_DirectoryWatch_reply(txn, ZX_ERR_INVALID_ARGS);
    }
    r = devfs_watch(dn, std::move(watcher), request->mask);
    return fuchsia_io_DirectoryWatch_reply(txn, r);
  } else if (ordinal == fuchsia_io_DirectoryAdminQueryFilesystemOrdinal ||
             ordinal == fuchsia_io_DirectoryAdminQueryFilesystemGenOrdinal) {
    DECODE_REQUEST(msg, DirectoryAdminQueryFilesystem);
    fuchsia_io_FilesystemInfo info;
    memset(&info, 0, sizeof(info));
    strlcpy((char*)info.name, "devfs", fuchsia_io_MAX_FS_NAME_BUFFER);
    return fuchsia_io_DirectoryAdminQueryFilesystem_reply(txn, ZX_OK, &info);
  }

  // close inbound handles so they do not leak
  zx_handle_close_many(msg->handles, msg->num_handles);
  return ZX_ERR_NOT_SUPPORTED;
}

void DcIostate::HandleRpc(std::unique_ptr<DcIostate> ios, async_dispatcher_t* dispatcher,
                          async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    log(ERROR, "driver_manager: DcIostate::HandleRpc aborting, saw status %d\n", status);
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    status = fs::ReadMessage(
        wait->object(), [&ios, dispatcher](fidl_msg_t* msg, fs::FidlConnection* txn) {
          return DcIostate::DevfsFidlHandler(msg, txn->Txn(), ios.get(), dispatcher);
        });
    if (status == ZX_OK) {
      ios->BeginWait(std::move(ios), dispatcher);
      return;
    }
  } else if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    fs::CloseMessage([&ios, dispatcher](fidl_msg_t* msg, fs::FidlConnection* txn) {
      return DcIostate::DevfsFidlHandler(msg, txn->Txn(), ios.get(), dispatcher);
    });
  } else {
    log(ERROR, "driver_manager: DcIostate::HandleRpc: invalid signals %x\n", signal->observed);
    abort();
  }
  // Do not start waiting again, and destroy |ios|
}

zx::unowned_channel devfs_root_borrow() { return zx::unowned_channel(g_devfs_root); }

zx::channel devfs_root_clone() { return zx::channel(fdio_service_clone(g_devfs_root.get())); }

void devfs_init(const fbl::RefPtr<Device>& device, async_dispatcher_t* dispatcher) {
  root_devnode = std::make_unique<Devnode>("");
  if (!root_devnode) {
    printf("driver_manager: failed to allocate devfs root node\n");
    return;
  }
  root_devnode->ino = 1;

  prepopulate_protocol_dirs();

  // TODO(teisenbe): Should this take a reference?
  root_devnode->device = device.get();
  root_devnode->device->self = root_devnode.get();

  zx::channel h0, h1;
  if (zx::channel::create(0, &h0, &h1) != ZX_OK) {
    return;
  } else if (DcIostate::Create(root_devnode.get(), dispatcher, &h0) != ZX_OK) {
    return;
  }

  g_devfs_root = std::move(h1);
  // This is actually owned by |device| and will be freed in unpublish
  __UNUSED auto ptr = root_devnode.release();
}

zx_status_t devfs_walk(Devnode* dn, const char* path, fbl::RefPtr<Device>* dev) {
  Devnode* inout = dn;

  char path_copy[PATH_MAX];
  if (strlen(path) + 1 > sizeof(path_copy)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  strcpy(path_copy, path);

  zx_status_t status = devfs_walk(&inout, path_copy);
  if (status != ZX_OK) {
    return status;
  }
  *dev = fbl::RefPtr(inout->device);
  return ZX_OK;
}

}  // namespace devmgr
