// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devfs.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/txn_header.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <string.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <fbl/string_buffer.h>

#include "src/devices/bin/driver_manager/builtin_devices.h"
#include "src/devices/bin/driver_manager/coordinator.h"

namespace fio = fuchsia_io;

constexpr std::string_view kDiagnosticsDirName = "diagnostics";

bool Devnode::is_invisible() const {
  return (device && device->flags & DEV_CTX_INVISIBLE) ||
         (service_options & fuchsia_device_fs::wire::ExportOptions::kInvisible);
}

struct Watcher : fbl::DoublyLinkedListable<std::unique_ptr<Watcher>,
                                           fbl::NodeOptions::AllowRemoveFromContainer> {
  Watcher(Devnode* dn, fidl::ServerEnd<fio::DirectoryWatcher> server_end, fio::wire::WatchMask mask)
      : devnode(dn), server_end(std::move(server_end)), mask(mask) {}

  Watcher(const Watcher&) = delete;
  Watcher& operator=(const Watcher&) = delete;

  Watcher(Watcher&&) = delete;
  Watcher& operator=(Watcher&&) = delete;

  void HandleChannelClose(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);

  Devnode* devnode = nullptr;
  fidl::ServerEnd<fio::DirectoryWatcher> server_end;
  fio::wire::WatchMask mask;
  async::WaitMethod<Watcher, &Watcher::HandleChannelClose> channel_close_wait{this};
};

void Watcher::HandleChannelClose(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                 zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_OK) {
    if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
      RemoveFromContainer();
    }
  }
}

class DcIostate : public fbl::DoublyLinkedListable<DcIostate*>,
                  public fidl::WireServer<fio::Directory> {
 public:
  explicit DcIostate(Devnode* dn, async_dispatcher_t* dispatcher);
  ~DcIostate() override;

  static void Bind(std::unique_ptr<DcIostate> ios, fidl::ServerEnd<fio::Directory> request);
  // Remove this DcIostate from its devnode
  void DetachFromDevnode();

  void AdvisoryLock(AdvisoryLockRequestView request,
                    AdvisoryLockCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) override;
  void Close(CloseCompleter::Sync& completer) override;
  void Query(QueryCompleter::Sync& completer) override;
  void DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) override;
  void GetConnectionInfo(GetConnectionInfoCompleter::Sync& completer) override;
  void Sync(SyncCompleter::Sync& completer) override { completer.ReplyError(ZX_ERR_NOT_SUPPORTED); }
  void GetAttr(GetAttrCompleter::Sync& completer) override;
  void SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }

  void Open(OpenRequestView request, OpenCompleter::Sync& completer) override;
  void AddInotifyFilter(AddInotifyFilterRequestView request,
                        AddInotifyFilterCompleter::Sync& completer) override {}
  void Unlink(UnlinkRequestView request, UnlinkCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void ReadDirents(ReadDirentsRequestView request, ReadDirentsCompleter::Sync& completer) override;
  void Rewind(RewindCompleter::Sync& completer) override;
  void GetToken(GetTokenCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, zx::handle());
  }
  void Rename(RenameRequestView request, RenameCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void Link(LinkRequestView request, LinkCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }
  void Watch(WatchRequestView request, WatchCompleter::Sync& completer) override;
  void GetFlags(GetFlagsCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }
  void SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }
  void QueryFilesystem(QueryFilesystemCompleter::Sync& completer) override;

 private:
  // pointer to our devnode, nullptr if it has been removed
  Devnode* devnode_;

  std::optional<fidl::ServerBindingRef<fio::Directory>> binding_;

  async_dispatcher_t* dispatcher_;

  uint64_t readdir_ino_ = 0;
};

namespace {

// A devnode is a directory (from stat's perspective) if
// it has children, or if it doesn't have a device, or if
// its device has no rpc handle
bool devnode_is_dir(const Devnode* dn) {
  if (dn->children.is_empty()) {
    return (dn->device == nullptr) || (!dn->device->device_controller().is_valid()) ||
           (!dn->device->coordinator_binding().has_value());
  }
  return true;
}

// Local devnodes are ones that we should not hand off OPEN
// RPCs to the underlying driver_host
bool devnode_is_local(Devnode* dn) {
  if (dn->service_dir) {
    return false;
  }
  if (dn->device == nullptr) {
    return true;
  }
  if (!dn->device->device_controller().is_valid()) {
    return true;
  }
  if (dn->device->flags & DEV_CTX_BUS_DEVICE || dn->device->flags & DEV_CTX_IMMORTAL) {
    return true;
  }
  return false;
}

// Notify a single watcher about the given operation and path.  On failure,
// frees the watcher.  This can only be called on a watcher that has not yet
// been added to a Devnode's watchers list.
void devfs_notify_single(std::unique_ptr<Watcher>* watcher, const fbl::String& name,
                         fio::wire::WatchEvent op) {
  size_t len = name.length();
  if (!*watcher || len > fio::wire::kMaxFilename) {
    return;
  }

  ZX_ASSERT(!(*watcher)->InContainer());

  uint8_t msg[fio::wire::kMaxFilename + 2];
  const uint32_t msg_len = static_cast<uint32_t>(len + 2);
  msg[0] = static_cast<uint8_t>(op);
  msg[1] = static_cast<uint8_t>(len);
  memcpy(msg + 2, name.c_str(), len);

  // convert to mask
  fio::wire::WatchMask mask = static_cast<fio::wire::WatchMask>(1u << static_cast<uint8_t>(op));

  if (!((*watcher)->mask & mask)) {
    return;
  }

  if ((*watcher)->server_end.channel().write(0, msg, msg_len, nullptr, 0) != ZX_OK) {
    watcher->reset();
  }
}

}  // namespace

void Devnode::notify(std::string_view name, fio::wire::WatchEvent op) {
  if (watchers.is_empty()) {
    return;
  }

  size_t len = name.length();
  if (len > fio::wire::kMaxFilename) {
    return;
  }

  uint8_t msg[fio::wire::kMaxFilename + 2];
  const uint32_t msg_len = static_cast<uint32_t>(len + 2);
  msg[0] = static_cast<uint8_t>(op);
  msg[1] = static_cast<uint8_t>(len);
  memcpy(msg + 2, name.data(), len);

  // convert to mask
  fio::wire::WatchMask mask = static_cast<fio::wire::WatchMask>(1u << static_cast<uint8_t>(op));

  for (auto itr = watchers.begin(); itr != watchers.end();) {
    auto& cur = *itr;
    // Advance the iterator now instead of at the end of the loop because we
    // may erase the current element from the list.
    ++itr;

    if (!(cur.mask & mask)) {
      continue;
    }

    if (cur.server_end.channel().write(0, msg, msg_len, nullptr, 0) != ZX_OK) {
      watchers.erase(cur);
      // The Watcher is free'd here
    }
  }
}

Devnode* Devfs::proto_node(uint32_t protocol_id) const {
  const auto it = proto_info_nodes.find(protocol_id);
  if (it == proto_info_nodes.end()) {
    return nullptr;
  }
  auto& [key, value] = *it;
  return value.get();
}

zx_status_t Devnode::watch(async_dispatcher_t* dispatcher,
                           fidl::ServerEnd<fio::DirectoryWatcher> server_end,
                           fio::wire::WatchMask mask) {
  auto watcher = std::make_unique<Watcher>(this, std::move(server_end), mask);
  if (watcher == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  // If the watcher has asked for all existing entries, send it all of them
  // followed by the end-of-existing marker (IDLE).
  if (mask & fio::wire::WatchMask::kExisting) {
    for (const auto& child : children) {
      if (child.is_invisible()) {
        continue;
      }
      // TODO: send multiple per write
      devfs_notify_single(&watcher, child.name, fio::wire::WatchEvent::kExisting);
    }
    devfs_notify_single(&watcher, {}, fio::wire::WatchEvent::kIdle);
  }

  // Watcher may have been freed by devfs_notify_single, so check before
  // adding.
  if (watcher) {
    watchers.push_front(std::move(watcher));

    Watcher& watcher_ref = watchers.front();
    watcher_ref.channel_close_wait.set_object(watcher_ref.server_end.channel().get());
    watcher_ref.channel_close_wait.set_trigger(ZX_CHANNEL_PEER_CLOSED);
    watcher_ref.channel_close_wait.Begin(dispatcher);
  }
  return ZX_OK;
}

bool Devnode::has_watchers() const { return !watchers.is_empty(); }

bool Devnode::is_special_ino(uint64_t ino) const {
  if (ino == devfs_.diagnostics_devnode->ino) {
    return true;
  }
  if (ino == devfs_.null_devnode->ino) {
    return true;
  }
  if (ino == devfs_.zero_devnode->ino) {
    return true;
  }
  return false;
}

std::unique_ptr<Devnode> Devfs::mkdir(Devnode* parent, std::string_view name) {
  std::unique_ptr<Devnode> dn = std::make_unique<Devnode>(*this, nullptr, name);
  if (dn == nullptr) {
    return nullptr;
  }
  dn->parent = parent;
  parent->children.push_back(dn.get());
  return dn;
}

namespace {

Devnode* devfs_lookup(Devnode* parent, std::string_view name) {
  for (auto& child : parent->children) {
    if (name == static_cast<std::string_view>(child.name)) {
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

}  // namespace

zx_status_t Devnode::readdir(uint64_t* ino_inout, void* data, size_t len) {
  char* ptr = static_cast<char*>(data);
  uint64_t ino = *ino_inout;

  for (const auto& child : children) {
    if (child.ino <= ino) {
      continue;
    }
    if (child.device == nullptr) {
      // "pure" directories (like /dev/class/$NAME) do not show up
      // if they have no children, to avoid clutter and confusion.
      // They remain openable, so they can be watched.
      //
      // An exception being /dev/diagnostics which is served by different VFS
      // and should be listed even though it has no devnode children.
      //
      // Another exception is when the devnode is for a remote service.
      if (child.children.is_empty() && !is_special_ino(ino) && !child.service_dir) {
        continue;
      }
    } else {
      // invisible devices also do not show up
      if (child.is_invisible()) {
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

zx::status<Devnode*> Devnode::walk(std::string_view path) {
  Devnode* dn = this;

  while (!path.empty()) {
    const size_t i = path.find('/');
    if (i == 0) {
      return zx::error(ZX_ERR_BAD_PATH);
    }
    std::string_view name = path;
    if (i != std::string::npos) {
      name = path.substr(0, i);
      path = path.substr(i + 1);
    } else {
      path = {};
    }
    auto it = std::find_if(dn->children.begin(), dn->children.end(), [name](const Devnode& child) {
      if (std::string_view{child.name} != name) {
        return false;
      }
      if (child.is_invisible()) {
        return false;
      }
      return true;
    });
    if (it == dn->children.end()) {
      // The path only partially matched.
      return zx::error(ZX_ERR_NOT_FOUND);
    }
    dn = &*it;
  }
  return zx::ok(dn);
}

void Devnode::open(async_dispatcher_t* dispatcher, fidl::ServerEnd<fio::Node> ipc,
                   std::string_view path, fio::OpenFlags flags) {
  // Filter requests for diagnostics path and pass it on to diagnostics vfs server.
  if (cpp20::starts_with(path, kDiagnosticsDirName)) {
    std::string_view dir_path = path.substr(kDiagnosticsDirName.size());
    if (cpp20::starts_with(dir_path, '/') || dir_path.empty()) {
      if (cpp20::starts_with(dir_path, '/')) {
        dir_path = dir_path.substr(1);
      }
      if (dir_path.empty()) {
        dir_path = ".";
      }
      __UNUSED const fidl::WireResult result =
          fidl::WireCall(devfs_.diagnostics_channel.value())
              ->Open(flags, 0, fidl::StringView::FromExternal(dir_path), std::move(ipc));
      return;
    }
  }

  if (path == kNullDevName || path == kZeroDevName) {
    BuiltinDevices::Get(dispatcher)->HandleOpen(flags, std::move(ipc), path);
    return;
  }

  if (path == ".") {
    path = {};
  }

  auto describe = [&ipc, describe = flags & fio::wire::OpenFlags::kDescribe](
                      zx::status<fio::wire::NodeInfoDeprecated> node_info) {
    if (describe) {
      __UNUSED auto result = fidl::WireSendEvent(ipc)->OnOpen(
          node_info.status_value(),
          node_info.is_ok() ? std::move(node_info.value()) : fio::wire::NodeInfoDeprecated());
    }
  };

  zx::status dn_result = walk(path);
  if (dn_result.is_error()) {
    describe(zx::error(dn_result.status_value()));
    return;
  }
  Devnode* dn = dn_result.value();

  // If we are a local-only node, or we are asked to open-as-a-directory, open locally:
  if (devnode_is_local(dn) || (flags & fio::wire::OpenFlags::kDirectory)) {
    auto ios = std::make_unique<DcIostate>(dn, dispatcher);
    if (ios == nullptr) {
      describe(zx::error(ZX_ERR_NO_MEMORY));
      return;
    }
    describe(zx::ok(fio::wire::NodeInfoDeprecated::WithDirectory({})));
    DcIostate::Bind(std::move(ios), fidl::ServerEnd<fio::Directory>{ipc.TakeChannel()});
    return;
  }
  if (dn->service_dir) {
    __UNUSED const fidl::WireResult result =
        fidl::WireCall(dn->service_dir)
            ->Open(flags, 0, fidl::StringView::FromExternal(dn->service_path), std::move(ipc));
    return;
  }
  __UNUSED auto result = dn->device->device_controller()->Open(flags, 0, ".", std::move(ipc));
}

Devnode::Devnode(Devfs& devfs, const fbl::RefPtr<Device>& dev, std::string_view name)
    : name(std::move(name)), ino(devfs.next_ino++), device(dev.get()), devfs_(devfs) {}

Devnode::~Devnode() {
  if (InContainer()) {
    parent->children.erase(*this);
  }

  // detach all connected iostates
  while (!iostate.is_empty()) {
    iostate.front().DetachFromDevnode();
  }

  // notify own file watcher
  if (!is_invisible()) {
    notify({}, fio::wire::WatchEvent::kDeleted);
  }

  // disconnect from device and notify parent/link directory watchers
  if (device != nullptr) {
    if (device->self == this) {
      device->self = nullptr;

      if ((device->parent() != nullptr) && (device->parent()->self != nullptr) && !is_invisible()) {
        device->parent()->self->notify(name, fio::wire::WatchEvent::kRemoved);
      }
    }
    if (device->link == this) {
      device->link = nullptr;

      if (!is_invisible()) {
        Devnode* dir = devfs_.proto_node(device->protocol_id());
        dir->notify(name, fio::wire::WatchEvent::kRemoved);
      }
    }
    device = nullptr;
  }

  // destroy all watchers
  watchers.clear();

  // detach children
  // They will be unpublished when the devices they're associated with are
  // eventually destroyed.
  children.clear();
}

DcIostate::DcIostate(Devnode* dn, async_dispatcher_t* dispatcher)
    : devnode_(dn), dispatcher_(dispatcher) {
  devnode_->iostate.push_back(this);
}

DcIostate::~DcIostate() { DetachFromDevnode(); }

void DcIostate::Bind(std::unique_ptr<DcIostate> ios, fidl::ServerEnd<fio::Directory> request) {
  // Grab a reference before we move the container.
  auto& binding = ios->binding_;
  binding = fidl::BindServer(ios->dispatcher_, std::move(request), std::move(ios));
}

void DcIostate::DetachFromDevnode() {
  if (devnode_ != nullptr) {
    devnode_->iostate.erase(*this);
    devnode_ = nullptr;
  }
  if (std::optional binding = std::exchange(binding_, std::nullopt); binding.has_value()) {
    binding.value().Unbind();
  }
}

void Devfs::advertise(const fbl::RefPtr<Device>& dev) {
  if (dev->link) {
    Devnode* dir = proto_node(dev->protocol_id());
    dir->notify(dev->link->name, fio::wire::WatchEvent::kAdded);
  }
  if (dev->self->parent) {
    dev->self->parent->notify(dev->self->name, fio::wire::WatchEvent::kAdded);
  }
}

// TODO: generate a MODIFIED event rather than back to back REMOVED and ADDED
void Devfs::advertise_modified(const fbl::RefPtr<Device>& dev) {
  if (dev->link) {
    Devnode* dir = proto_node(dev->protocol_id());
    dir->notify(dev->link->name, fio::wire::WatchEvent::kRemoved);
    dir->notify(dev->link->name, fio::wire::WatchEvent::kAdded);
  }
  if (dev->self->parent) {
    dev->self->parent->notify(dev->self->name, fio::wire::WatchEvent::kRemoved);
    dev->self->parent->notify(dev->self->name, fio::wire::WatchEvent::kAdded);
  }
}

zx_status_t devfs_seq_name(Devnode* dir, char* data, size_t size) {
  for (unsigned n = 0; n < 1000; n++) {
    snprintf(data, size, "%03u", (dir->seqcount++) % 1000);
    if (devfs_lookup(dir, data) == nullptr) {
      return ZX_OK;
    }
  }
  return ZX_ERR_ALREADY_EXISTS;
}

zx_status_t Devfs::publish(const fbl::RefPtr<Device>& parent, const fbl::RefPtr<Device>& dev) {
  if ((parent->self == nullptr) || (dev->self != nullptr) || (dev->link != nullptr)) {
    return ZX_ERR_INTERNAL;
  }

  std::unique_ptr<Devnode> dnself = std::make_unique<Devnode>(*this, dev, dev->name());
  if (dnself == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((dev->protocol_id() == ZX_PROTOCOL_TEST_PARENT) || (dev->protocol_id() == ZX_PROTOCOL_MISC)) {
    // misc devices are singletons, not a class
    // in the sense of other device classes.
    // They do not get aliases in /dev/class/misc/...
    // instead they exist only under their parent
    // device.
    goto done;
  }

  // Create link in /dev/class/... if this id has a published class
  if (auto dir = proto_node(dev->protocol_id()); dir != nullptr) {
    char buf[4] = {};
    auto name = dev->name();

    if (dev->protocol_id() != ZX_PROTOCOL_CONSOLE) {
      zx_status_t status = devfs_seq_name(dir, buf, sizeof(buf));
      if (status != ZX_OK) {
        return status;
      }
      name = buf;
    }

    std::unique_ptr<Devnode> dnlink = std::make_unique<Devnode>(*this, dev, name);
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
    advertise(dev);
  }
  return ZX_OK;
}

// TODO(teisenbe): Ideally this would take a RefPtr, but currently this is
// invoked in the dtor for Device.
void Devfs::unpublish(Device* dev) {
  if (dev->self != nullptr) {
    delete dev->self;
    dev->self = nullptr;
  }
  if (dev->link != nullptr) {
    delete dev->link;
    dev->link = nullptr;
  }
}

zx_status_t devfs_connect(const Device* dev, fidl::ServerEnd<fio::Node> client_remote) {
  const fidl::Status result = dev->device_controller()->Open({}, 0, ".", std::move(client_remote));
  return result.status();
}

void Devfs::connect_diagnostics(fidl::ClientEnd<fio::Directory> h) {
  diagnostics_channel = std::move(h);
}

void DcIostate::Open(OpenRequestView request, OpenCompleter::Sync& completer) {
  devnode_->open(dispatcher_, std::move(request->object), request->path.get(), request->flags);
}

void DcIostate::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  if (request->flags & fio::wire::OpenFlags::kCloneSameRights) {
    request->flags |= fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightWritable;
  }
  devnode_->open(dispatcher_, std::move(request->object), ".",
                 request->flags | fio::wire::OpenFlags::kDirectory);
}

void DcIostate::QueryFilesystem(QueryFilesystemCompleter::Sync& completer) {
  fio::wire::FilesystemInfo info;
  strlcpy(reinterpret_cast<char*>(info.name.data()), "devfs", fio::wire::kMaxFsNameBuffer);
  completer.Reply(ZX_OK, fidl::ObjectView<fio::wire::FilesystemInfo>::FromExternal(&info));
}

void DcIostate::Watch(WatchRequestView request, WatchCompleter::Sync& completer) {
  zx_status_t status;
  if (!request->mask || request->options != 0) {
    status = ZX_ERR_INVALID_ARGS;
  } else {
    status = devnode_->watch(dispatcher_, std::move(request->watcher), request->mask);
  }
  completer.Reply(status);
}

void DcIostate::Rewind(RewindCompleter::Sync& completer) {
  readdir_ino_ = 0;
  completer.Reply(ZX_OK);
}

void DcIostate::ReadDirents(ReadDirentsRequestView request, ReadDirentsCompleter::Sync& completer) {
  if (request->max_bytes > fio::wire::kMaxBuf) {
    completer.Reply(ZX_ERR_INVALID_ARGS, fidl::VectorView<uint8_t>());
    return;
  }

  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;
  zx_status_t status = devnode_->readdir(&readdir_ino_, data, request->max_bytes);
  if (status >= 0) {
    actual = status;
    status = ZX_OK;
  }
  completer.Reply(status, fidl::VectorView<uint8_t>::FromExternal(data, actual));
}

void DcIostate::GetAttr(GetAttrCompleter::Sync& completer) {
  uint32_t mode;
  if (devnode_is_dir(devnode_)) {
    mode = V_TYPE_DIR | V_IRUSR | V_IWUSR;
  } else {
    mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
  }

  fio::wire::NodeAttributes attributes;
  attributes.mode = mode;
  attributes.content_size = 0;
  attributes.link_count = 1;
  attributes.id = devnode_->ino;
  completer.Reply(ZX_OK, attributes);
}

void DcIostate::DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) {
  fio::wire::DirectoryObject directory;
  completer.Reply(fio::wire::NodeInfoDeprecated::WithDirectory(directory));
}

void DcIostate::GetConnectionInfo(GetConnectionInfoCompleter::Sync& completer) {
  fio::wire::ConnectionInfo connection_info;
  completer.Reply(connection_info);
}

void DcIostate::Close(CloseCompleter::Sync& completer) {
  completer.ReplySuccess();
  completer.Close(ZX_OK);
}

void DcIostate::Query(QueryCompleter::Sync& completer) {
  const std::string_view kProtocol = fio::wire::kDirectoryProtocolName;
  uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
  completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
}

zx::status<fidl::ClientEnd<fuchsia_io::Directory>> Devfs::Connect(async_dispatcher_t* dispatcher) {
  zx::status endpoints = fidl::CreateEndpoints<fio::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto& [client, server] = endpoints.value();
  std::unique_ptr ios = std::make_unique<DcIostate>(root_devnode_, dispatcher);
  DcIostate::Bind(std::move(ios), std::move(endpoints->server));
  return zx::ok(std::move(client));
}

Devfs::Devfs(const fbl::RefPtr<Device>& device) {
  std::unique_ptr root_devnode = std::make_unique<Devnode>(*this, device, "");
  if (!root_devnode) {
    return;
  }

  class_devnode = mkdir(root_devnode.get(), "class");
  if (!class_devnode) {
    return;
  }
  // Pre-populate the class directories.
  {
    Devnode* dn = class_devnode.get();
    for (const auto& info : proto_infos) {
      if (!(info.flags & PF_NOPUB)) {
        const auto [it, inserted] = proto_info_nodes.try_emplace(info.id, mkdir(dn, info.name));
        const auto& [key, value] = *it;
        ZX_ASSERT_MSG(inserted, "duplicate protocol with id %d (%s and %s)", info.id,
                      value->name.c_str(), info.name);
      }
    }
  }

  // Create dummy diagnostics devnode, so that the directory is listed.
  diagnostics_devnode = mkdir(root_devnode.get(), "diagnostics");
  // Create dummy null devnode, so that the directory is listed.
  null_devnode = mkdir(root_devnode.get(), "null");
  // Create dummy zero devnode, so that the directory is listed.
  zero_devnode = mkdir(root_devnode.get(), "zero");

  // Some tests are lazy about this and pass nullptr.
  if (device != nullptr) {
    // This is actually owned by |device| and will be freed in unpublish
    device->self = root_devnode.release();
    root_devnode_ = device->self;
  } else {
    root_devnode_ = nullptr;
  }
}

zx_status_t Devfs::export_dir(Devnode* dn, fidl::ClientEnd<fio::Directory> service_dir,
                              std::string_view service_path, std::string_view devfs_path,
                              uint32_t protocol_id, fuchsia_device_fs::wire::ExportOptions options,
                              std::vector<std::unique_ptr<Devnode>>& out) {
  // Check if the `devfs_path` provided is valid.
  const auto is_valid_path = [](std::string_view path) {
    return !path.empty() && path.front() != '/' && path.back() != '/';
  };
  if (!is_valid_path(service_path) || !is_valid_path(devfs_path)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Walk the `devfs_path` and call `process` on each part of the path.
  //
  // e.g. If devfs_path is "platform/acpi/acpi-pwrbtn", then process will be
  // called with "platform", then "acpi", then "acpi-pwrbtn".
  std::string_view::size_type begin = 0, end = 0;
  const auto walk = [&devfs_path, &begin, &end](auto process) {
    do {
      // Consume excess separators.
      while (devfs_path[begin] == '/') {
        ++begin;
      }
      end = devfs_path.find('/', begin);
      auto size = end == std::string_view::npos ? end : end - begin;
      if (!process(devfs_path.substr(begin, size))) {
        break;
      }
      begin = end + 1;
    } while (end != std::string_view::npos);
  };

  // Walk `devfs_path` and find the last Devnode that exists, so we can create
  // the rest of the path underneath it.
  Devnode* prev_dn = nullptr;
  walk([&dn, &prev_dn](std::string_view name) {
    prev_dn = dn;
    dn = devfs_lookup(dn, name);
    return dn != nullptr;
  });

  // The full path described by `devfs_path` already exists.
  if (dn != nullptr) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  // Create Devnodes for the remainder of the path, and set `service_dir` and
  // `service_path` on the leaf Devnode.
  dn = prev_dn;
  walk([this, &out, &dn, options](std::string_view name) {
    out.push_back(mkdir(dn, name));
    auto new_dn = out.back().get();
    new_dn->service_options = options;
    if (!new_dn->is_invisible()) {
      dn->notify(name, fio::wire::WatchEvent::kAdded);
    }
    dn = new_dn;
    return true;
  });
  dn->service_dir = std::move(service_dir);
  dn->service_path = service_path;

  // If a protocol directory exists for `protocol_id`, then create a Devnode
  // under the protocol directory too.
  if (auto dir = proto_node(protocol_id); dir != nullptr) {
    char name[4] = {};
    zx_status_t status = devfs_seq_name(dir, name, sizeof(name));
    if (status != ZX_OK) {
      return status;
    }
    out.push_back(mkdir(dir, name));
    Devnode* class_dn = out.back().get();
    class_dn->service_options = options;
    if (!class_dn->is_invisible()) {
      dir->notify(name, fio::wire::WatchEvent::kAdded);
    }

    // Clone the service node for the entry in the protocol directory.
    auto endpoints = fidl::CreateEndpoints<fio::Node>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    auto result = fidl::WireCall(dn->service_dir)
                      ->Clone(fio::wire::OpenFlags::kCloneSameRights, std::move(endpoints->server));
    if (!result.ok()) {
      return result.status();
    }
    class_dn->service_dir.channel().swap(endpoints->client.channel());
    class_dn->service_path = service_path;
  }

  return ZX_OK;
}
