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
#include <variant>

#include <fbl/ref_ptr.h>

#include "src/devices/bin/driver_manager/builtin_devices.h"
#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace fio = fuchsia_io;

constexpr std::string_view kDiagnosticsDirName = "diagnostics";

namespace {

// Helpers from the reference documentation for std::visit<>, to allow
// visit-by-overload of the std::variant<> returned by GetLastReference():
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

}  // namespace

bool Devnode::is_invisible() const {
  return std::visit(
      overloaded{
          [](const NoRemote& no_remote) {
            return static_cast<bool>(no_remote.export_options & ExportOptions::kInvisible);
          },
          [](const Service& service) {
            return static_cast<bool>(service.export_options & ExportOptions::kInvisible);
          },
          [](const Device& device) { return static_cast<bool>(device.flags & DEV_CTX_INVISIBLE); }},
      target_);
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
  explicit DcIostate(Devnode& dn, async_dispatcher_t* dispatcher);
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

bool Devnode::is_dir() const {
  if (!children.is_empty()) {
    return true;
  }
  return std::visit(
      overloaded{[](const NoRemote&) { return true; }, [](const Service& service) { return false; },
                 [](const Device& device) {
                   return !(device.device_controller().is_valid() &&
                            device.coordinator_binding().has_value());
                 }},
      target_);
}

namespace {

// Notify a single watcher about the given operation and path.  On failure,
// frees the watcher.  This can only be called on a watcher that has not yet
// been added to a Devnode's watchers list.
void devfs_notify_single(std::unique_ptr<Watcher>* watcher, std::string_view name,
                         fio::wire::WatchEvent op) {
  const size_t len = name.length();
  if (!*watcher || len > fio::wire::kMaxFilename) {
    return;
  }

  ZX_ASSERT(!(*watcher)->InContainer());

  uint8_t msg[fio::wire::kMaxFilename + 2];
  const uint32_t msg_len = static_cast<uint32_t>(len + 2);
  msg[0] = static_cast<uint8_t>(op);
  msg[1] = static_cast<uint8_t>(len);
  memcpy(msg + 2, name.data(), len);

  // convert to mask
  const fio::wire::WatchMask mask =
      static_cast<fio::wire::WatchMask>(1u << static_cast<uint8_t>(op));

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

  const size_t len = name.length();
  if (len > fio::wire::kMaxFilename) {
    return;
  }

  uint8_t msg[fio::wire::kMaxFilename + 2];
  const uint32_t msg_len = static_cast<uint32_t>(len + 2);
  msg[0] = static_cast<uint8_t>(op);
  msg[1] = static_cast<uint8_t>(len);
  memcpy(msg + 2, name.data(), len);

  // convert to mask
  const fio::wire::WatchMask mask =
      static_cast<fio::wire::WatchMask>(1u << static_cast<uint8_t>(op));

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

Devnode* Devfs::proto_node(uint32_t protocol_id) {
  auto it = proto_info_nodes.find(protocol_id);
  if (it == proto_info_nodes.end()) {
    return nullptr;
  }
  auto& [key, value] = *it;
  return &value;
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
      devfs_notify_single(&watcher, child.name(), fio::wire::WatchEvent::kExisting);
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

const Device* Devnode::device() const {
  return std::visit(overloaded{[](const NoRemote&) -> const Device* { return nullptr; },
                               [](const Service&) -> const Device* { return nullptr; },
                               [](const Device& device) { return &device; }},
                    target_);
}

Devnode* Devnode::parent() const { return parent_; }

std::string_view Devnode::name() const {
  if (name_.has_value()) {
    return name_.value();
  }
  return {};
}

Devnode::ExportOptions Devnode::export_options() const {
  return std::visit(overloaded{[](const NoRemote& no_remote) { return no_remote.export_options; },
                               [](const Service& service) { return service.export_options; },
                               [](const Device&) { return ExportOptions{}; }},
                    target_);
}

Devnode::ExportOptions* Devnode::export_options() {
  return std::visit(overloaded{[](const NoRemote& no_remote) { return &no_remote.export_options; },
                               [](const Service& service) { return &service.export_options; },
                               [](const Device&) -> ExportOptions* { return nullptr; }},
                    target_);
}

Devnode* Devnode::lookup(std::string_view name) {
  for (Devnode& child : children) {
    if (child.name() == name) {
      return &child;
    }
  }
  return nullptr;
}

const Devnode* Devnode::lookup(std::string_view name) const {
  for (const Devnode& child : children) {
    if (child.name() == name) {
      return &child;
    }
  }
  return nullptr;
}

namespace {

zx_status_t fill_dirent(vdirent_t* de, size_t delen, uint64_t ino, std::string_view name,
                        uint8_t type) {
  const size_t len = name.length();
  const size_t sz = sizeof(vdirent_t) + len;

  if (sz > delen || len > NAME_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }
  de->ino = ino;
  de->size = static_cast<uint8_t>(len);
  de->type = type;
  memcpy(de->name, name.data(), len);
  return static_cast<zx_status_t>(sz);
}

}  // namespace

zx_status_t Devnode::readdir(uint64_t* ino_inout, void* data, size_t len) {
  char* ptr = static_cast<char*>(data);
  uint64_t ino = *ino_inout;

  for (const auto& child : children) {
    if (child.ino_ <= ino) {
      continue;
    }
    const bool show =
        !child.is_invisible() &&
        std::visit(
            overloaded{[&child](const NoRemote&) {
                         // "pure" directories (like /dev/class/$NAME) do
                         // not show up if they have no children, to
                         // avoid clutter and confusion.  They remain
                         // openable, so they can be watched.
                         //
                         // An exception being /dev/diagnostics which is
                         // served by different VFS and should be listed
                         // even though it has no devnode children.
                         if (child.ino_ == child.devfs_.diagnostics_devnode.ino_) {
                           return true;
                         }
                         if (child.ino_ == child.devfs_.null_devnode.ino_) {
                           return true;
                         }
                         if (child.ino_ == child.devfs_.zero_devnode.ino_) {
                           return true;
                         }
                         return !child.children.is_empty();
                       },
                       [](const Service&) { return true; }, [](const Device&) { return true; }},
            child.target_);
    if (!show) {
      continue;
    }
    ino = child.ino_;
    auto vdirent = reinterpret_cast<vdirent_t*>(ptr);
    const zx_status_t r = fill_dirent(vdirent, len, ino, child.name(), VTYPE_TO_DTYPE(V_TYPE_DIR));
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
      if (child.name() != name) {
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
          fidl::WireCall(devfs_.diagnostics_.value())
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
  Devnode& dn = *dn_result.value();

  auto open_locally = [&]() {
    auto ios = std::make_unique<DcIostate>(dn, dispatcher);
    if (ios == nullptr) {
      describe(zx::error(ZX_ERR_NO_MEMORY));
      return;
    }
    describe(zx::ok(fio::wire::NodeInfoDeprecated::WithDirectory({})));
    DcIostate::Bind(std::move(ios), fidl::ServerEnd<fio::Directory>{ipc.TakeChannel()});
  };

  if (flags & fio::wire::OpenFlags::kDirectory) {
    open_locally();
    return;
  }

  return std::visit(
      overloaded{[&](const NoRemote&) { open_locally(); },
                 [&](const Service& service) {
                   __UNUSED const fidl::WireResult result =
                       fidl::WireCall(service.remote)
                           ->Open(flags, 0, fidl::StringView::FromExternal(service.path),
                                  std::move(ipc));
                 },
                 [&](const Device& device) {
                   if (device.device_controller().is_valid()) {
                     __UNUSED const fidl::Status result =
                         device.device_controller()->Open(flags, 0, ".", std::move(ipc));
                   } else {
                     open_locally();
                   }
                 }},
      dn.target_);
}

Devnode::Devnode(Devfs& devfs, Device* device)
    : devfs_(devfs),
      parent_(nullptr),
      target_([device]() -> Target {
        if (device != nullptr) {
          return *device;
        }
        return NoRemote{};
      }()),
      ino_(devfs.next_ino++) {}

Devnode::Devnode(Devfs& devfs, Devnode& parent, Target target, fbl::String name)
    : devfs_(devfs),
      parent_(&parent),
      target_(std::move(target)),
      name_(std::move(name)),
      ino_(devfs.next_ino++) {
  if (Devnode* other = parent.lookup(this->name()); other != nullptr) {
    // As of this writing, composite devices all attach at the root node in devfs, including
    // duplicates. Because this is load-bearing, we cannot easily change it.
    //
    // Normally we'd assert that such duplicates are composite devices to prevent regressions, but
    // composite devices are late-bound; a device's membership in a composite is recorded only when
    // `CompositeDevice::TryAssemble()` is called, which is apparently *after* the device is added
    // to devfs.
    //
    // This apparently also happens with non-composite devices. During zxcrypt-test these paths
    // appear more than once:
    //
    // - sys/platform/00:00:2d/ramctl/ramdisk-0/block/zxcrypt/unsealed
    // - sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/data-p-1/block/zxcrypt/unsealed
    //
    // To preserve the invariant that directory entries do not contain duplicates, we do not add
    // this to the parent. Hopefully the first device to arrive is sufficient. Unhook the parent to
    // prevent double cleanup.
    //
    // TODO(https://fxbug.dev/110922): Remove this logic when composite devices no longer surface
    // this way in devfs.
    std::vector<std::string_view> segments;
    for (Devnode* node = this; node != nullptr; node = node->parent_) {
      segments.emplace_back(node->name());
    }
    // Normally we'd assert here that the root node is unnamed, but because we might've unhooked our
    // parent node from its parent, we may not end on the real root node.
    if (segments.back().empty()) {
      segments.pop_back();
    }
    std::reverse(segments.begin(), segments.end());
    parent_ = nullptr;
    LOGF(WARNING, "skipping duplicate devfs path '%s': this=%p other=%p",
         fxl::JoinStrings(segments, "/").c_str(), this, other);
    return;
  }
  parent.children.push_back(this);
}

Devnode::~Devnode() {
  if (InContainer()) {
    if (parent_ != nullptr) {
      parent_->children.erase(*this);
    }
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
  if (!is_invisible()) {
    std::visit(overloaded{[](const NoRemote&) {}, [](const Service&) {},
                          [this](Device& device) {
                            if (device.parent() == nullptr) {
                              return;
                            }
                            Device& parent = *device.parent();
                            std::optional<Devnode>& parent_node = parent.self;
                            if (parent_node.has_value()) {
                              parent_node.value().notify(name(), fio::wire::WatchEvent::kRemoved);
                            }
                            Devnode* dir = devfs_.proto_node(device.protocol_id());
                            if (dir != nullptr) {
                              dir->notify(name(), fio::wire::WatchEvent::kRemoved);
                            }
                          }},
               target_);
  };

  // destroy all watchers
  watchers.clear();

  // detach children
  for (auto& child : children) {
    child.parent_ = nullptr;
  }
  children.clear();
}

DcIostate::DcIostate(Devnode& dn, async_dispatcher_t* dispatcher)
    : devnode_(&dn), dispatcher_(dispatcher) {
  dn.iostate.push_back(this);
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

void Devfs::advertise(Device& device) {
  for (auto* ptr : {&device.link, &device.self}) {
    auto& dn_opt = *ptr;
    if (dn_opt.has_value()) {
      const Devnode& dn = dn_opt.value();
      // parent is nullptr if this node's name collides with another node. Hopefully that node is
      // also being advertised.
      //
      // TODO(https://fxbug.dev/110922): Remove this condition when composite devices no longer
      // surface this way in devfs.
      if (dn.parent_ != nullptr) {
        Devnode& parent = *dn.parent_;
        parent.notify(dn.name(), fio::wire::WatchEvent::kAdded);
      }
    }
  }
}

void Devfs::advertise_modified(Device& device) {
  for (auto* ptr : {&device.link, &device.self}) {
    auto& dn_opt = *ptr;
    if (dn_opt.has_value()) {
      const Devnode& dn = dn_opt.value();
      // parent is nullptr if this node's name collides with another node. Hopefully that node is
      // also being advertised.
      //
      // TODO(https://fxbug.dev/110922): Remove this condition when composite devices no longer
      // surface this way in devfs.
      if (dn.parent_ != nullptr) {
        Devnode& parent = *dn.parent_;
        parent.notify(dn.name(), fio::wire::WatchEvent::kRemoved);
        parent.notify(dn.name(), fio::wire::WatchEvent::kAdded);
      }
    }
  }
}

zx::status<fbl::String> Devnode::seq_name() {
  std::string dest;
  for (uint32_t i = 0; i < 1000; ++i) {
    dest.clear();
    fxl::StringAppendf(&dest, "%03u", (seqcount_++) % 1000);
    if (lookup(dest) == nullptr) {
      return zx::ok(dest);
    }
  }
  return zx::error(ZX_ERR_ALREADY_EXISTS);
}

zx_status_t Devfs::publish(Device& parent, Device& device) {
  if (!parent.self.has_value() || device.self.has_value() || device.link.has_value()) {
    return ZX_ERR_INTERNAL;
  }

  device.self.emplace(*this, parent.self.value(), device, device.name());

  switch (const uint32_t id = device.protocol_id(); id) {
    case ZX_PROTOCOL_TEST_PARENT:
    case ZX_PROTOCOL_MISC:
      // misc devices are singletons, not a class in the sense of other device
      // classes.  They do not get aliases in /dev/class/misc/...  instead they
      // exist only under their parent device.
      break;
    default: {
      // Create link in /dev/class/... if this id has a published class
      if (Devnode* dir_ptr = proto_node(id); dir_ptr != nullptr) {
        Devnode& dir = *dir_ptr;
        fbl::String name = device.name();
        if (id != ZX_PROTOCOL_CONSOLE) {
          zx::status seq_name = dir.seq_name();
          if (seq_name.is_error()) {
            return seq_name.status_value();
          }
          name = seq_name.value();
        }

        device.link.emplace(*this, dir, device, name);
      }
    }
  }

  if (!(device.flags & DEV_CTX_INVISIBLE)) {
    advertise(device);
  }
  return ZX_OK;
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
  if (devnode_->is_dir()) {
    mode = V_TYPE_DIR | V_IRUSR | V_IWUSR;
  } else {
    mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
  }

  fio::wire::NodeAttributes attributes;
  attributes.mode = mode;
  attributes.content_size = 0;
  attributes.link_count = 1;
  attributes.id = devnode_->ino_;
  completer.Reply(ZX_OK, attributes);
}

void DcIostate::DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) {
  completer.Reply(fio::wire::NodeInfoDeprecated::WithDirectory({}));
}

void DcIostate::GetConnectionInfo(GetConnectionInfoCompleter::Sync& completer) {
  completer.Reply({});
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

zx::status<fidl::ClientEnd<fio::Directory>> Devfs::Connect(async_dispatcher_t* dispatcher) {
  if (root_devnode_ == nullptr) {
    return zx::error(ZX_ERR_PEER_CLOSED);
  }
  Devnode& root_devnode = *root_devnode_;
  zx::status endpoints = fidl::CreateEndpoints<fio::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto& [client, server] = endpoints.value();
  std::unique_ptr ios = std::make_unique<DcIostate>(root_devnode, dispatcher);
  DcIostate::Bind(std::move(ios), std::move(endpoints->server));
  return zx::ok(std::move(client));
}

Devfs::Devfs(std::optional<Devnode>& root, Device* device,
             std::optional<fidl::ClientEnd<fuchsia_io::Directory>> diagnostics)
    : root_devnode_(&root.emplace(*this, device)),
      class_devnode(*this, *root_devnode_, Devnode::NoRemote{}, "class"),
      diagnostics_devnode(*this, *root_devnode_, Devnode::NoRemote{}, kDiagnosticsDirName),
      null_devnode(*this, *root_devnode_, Devnode::NoRemote{}, "null"),
      zero_devnode(*this, *root_devnode_, Devnode::NoRemote{}, "zero"),
      diagnostics_(std::move(diagnostics)) {
  // Pre-populate the class directories.
  for (const auto& info : proto_infos) {
    if (!(info.flags & PF_NOPUB)) {
      const auto [it, inserted] = proto_info_nodes.try_emplace(info.id, *this, class_devnode,
                                                               Devnode::NoRemote{}, info.name);
      const auto& [key, value] = *it;
      ZX_ASSERT_MSG(inserted, "duplicate protocol with id %d", key);
    }
  }
}

zx_status_t Devnode::export_dir(fidl::ClientEnd<fio::Directory> service_dir,
                                std::string_view service_path, std::string_view devfs_path,
                                uint32_t protocol_id, ExportOptions options,
                                std::vector<std::unique_ptr<Devnode>>& out) {
  {
    const std::vector segments =
        fxl::SplitString(service_path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                         fxl::SplitResult::kSplitWantAll);
    if (segments.empty() ||
        std::any_of(segments.begin(), segments.end(), std::mem_fn(&std::string_view::empty))) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  const std::vector segments = fxl::SplitString(
      devfs_path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace, fxl::SplitResult::kSplitWantAll);
  if (segments.empty() ||
      std::any_of(segments.begin(), segments.end(), std::mem_fn(&std::string_view::empty))) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Walk the request export path segment-by-segment.
  Devnode* dn = this;
  for (size_t i = 0; i < segments.size(); ++i) {
    const std::string_view name = segments.at(i);
    Devnode* child = dn->lookup(name);
    if (i != segments.size() - 1) {
      // This is not the final path segment. Use the existing node or create one
      // if it doesn't exist.
      if (child != nullptr) {
        dn = child;
        continue;
      }
      Devnode& child = *out.emplace_back(std::make_unique<Devnode>(devfs_, *dn,
                                                                   NoRemote{
                                                                       .export_options = options,
                                                                   },
                                                                   name));
      if (!child.is_invisible()) {
        dn->notify(name, fio::wire::WatchEvent::kAdded);
      }
      dn = &child;
      continue;
    }

    // At this point `dn` is the second-last path segment.
    if (child != nullptr) {
      // The full path described by `devfs_path` already exists.
      return ZX_ERR_ALREADY_EXISTS;
    }

    // If a protocol directory exists for `protocol_id`, then create a Devnode
    // under the protocol directory too.
    if (Devnode* dir_ptr = devfs_.proto_node(protocol_id); dir_ptr != nullptr) {
      Devnode& dn = *dir_ptr;
      zx::status seq_name = dn.seq_name();
      if (seq_name.is_error()) {
        return seq_name.status_value();
      }
      const fbl::String name = seq_name.value();

      // Clone the service node for the entry in the protocol directory.
      zx::status endpoints = fidl::CreateEndpoints<fio::Directory>();
      if (endpoints.is_error()) {
        return endpoints.status_value();
      }
      auto& [client, server] = endpoints.value();
      const fidl::WireResult result = fidl::WireCall(service_dir)
                                          ->Clone(fio::wire::OpenFlags::kCloneSameRights,
                                                  fidl::ServerEnd<fio::Node>{server.TakeChannel()});
      if (!result.ok()) {
        return result.status();
      }

      const Devnode& child =
          *out.emplace_back(std::make_unique<Devnode>(devfs_, dn,
                                                      Service{
                                                          .remote = std::move(client),
                                                          .path = std::string(service_path),
                                                          .export_options = options,
                                                      },
                                                      name));
      if (!child.is_invisible()) {
        dn.notify(name, fio::wire::WatchEvent::kAdded);
      }
    }

    {
      const Devnode& child =
          *out.emplace_back(std::make_unique<Devnode>(devfs_, *dn,
                                                      Service{
                                                          .remote = std::move(service_dir),
                                                          .path = std::string(service_path),
                                                          .export_options = options,
                                                      },
                                                      name));
      if (!child.is_invisible()) {
        dn->notify(name, fio::wire::WatchEvent::kAdded);
      }
    }
  }
  return ZX_OK;
}
