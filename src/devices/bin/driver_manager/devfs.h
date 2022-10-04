// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_

#include <fidl/fuchsia.device.fs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <variant>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

class Devfs;
class Device;
class DcIostate;
struct Watcher;

struct Devnode
    : public fbl::DoublyLinkedListable<Devnode*, fbl::NodeOptions::AllowRemoveFromContainer> {
  using ExportOptions = fuchsia_device_fs::wire::ExportOptions;
  struct NoRemote {
    mutable ExportOptions export_options;
  };
  struct Service {
    fidl::ClientEnd<fuchsia_io::Directory> remote;
    std::string path;
    mutable ExportOptions export_options;
  };

  using Target = std::variant<NoRemote, Service, std::reference_wrapper<Device>>;

  // Constructs a root node.
  Devnode(Devfs& devfs, Device* device);

  // `parent` must outlive `this`.
  Devnode(Devfs& devfs, Devnode& parent, Target target, fbl::String name);

  ~Devnode();

  Devnode(const Devnode&) = delete;
  Devnode& operator=(const Devnode&) = delete;

  Devnode(Devnode&&) = delete;
  Devnode& operator=(Devnode&&) = delete;

  // Watches the devfs directory `dn`, and sends events to `server_end`.
  zx_status_t watch(async_dispatcher_t* dispatcher,
                    fidl::ServerEnd<fuchsia_io::DirectoryWatcher> server_end,
                    fuchsia_io::wire::WatchMask mask);

  zx::status<Devnode*> walk(std::string_view path);

  // Exports `service_path` from `service_dir` to `devfs_path`, under `dn`. If
  // `protocol_id` matches a known protocol, `service_path` will also be exposed
  // under a class path.
  //
  // Every Devnode that is created during the export is stored within `out`. As
  // each of these Devnodes are children of `dn`, they must live as long as `dn`.
  zx_status_t export_dir(fidl::ClientEnd<fuchsia_io::Directory> service_dir,
                         std::string_view service_path, std::string_view devfs_path,
                         uint32_t protocol_id, ExportOptions options,
                         std::vector<std::unique_ptr<Devnode>>& out);

  void notify(std::string_view name, fuchsia_io::wire::WatchEvent op);

  // This method is exposed for testing.
  bool has_watchers() const;

  // This method is exposed for testing.
  Devnode* lookup(std::string_view name);
  const Devnode* lookup(std::string_view name) const;

  const Device* device() const;
  Devnode* parent() const;
  std::string_view name() const;
  ExportOptions export_options() const;
  ExportOptions* export_options();

  // Publishes the node to devfs. Asserts if called more than once.
  void publish();

 private:
  friend class DcIostate;
  friend class Devfs;

  // A devnode is a directory (from stat's perspective) if it has children, or
  // if it doesn't have a device, or if its device has no rpc handle.
  bool is_dir() const;

  void open(async_dispatcher_t* dispatcher, fidl::ServerEnd<fuchsia_io::Node> ipc,
            std::string_view path, fuchsia_io::OpenFlags flags);

  zx_status_t readdir(uint64_t* ino_inout, void* data, size_t len);

  zx::status<fbl::String> seq_name();

  Devfs& devfs_;

  // Pointer to our parent, for removing ourselves from its list of
  // children. Our parent must outlive us.
  Devnode* parent_;

  const Target target_;

  const std::optional<fbl::String> name_;
  const uint64_t ino_;

  // used to assign unique small device numbers
  // for class device links
  uint32_t seqcount_ = 0;

  fbl::DoublyLinkedList<std::unique_ptr<Watcher>> watchers;

  // list of our child devnodes
  struct {
    fbl::DoublyLinkedList<Devnode*> unpublished;
    fbl::DoublyLinkedList<Devnode*> published;
  } children;

  // list of attached iostates
  fbl::DoublyLinkedList<DcIostate*> iostate;
};

class Devfs {
 public:
  // `root` and `device` must outlive `this`.
  Devfs(std::optional<Devnode>& root, Device* device,
        std::optional<fidl::ClientEnd<fuchsia_io::Directory>> diagnostics = {});

  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> Connect(async_dispatcher_t* dispatcher);

  zx_status_t initialize(Device& parent, Device& device);
  void publish(Device& device);
  void advertise_modified(Device& dev);

  // For testing only.
  Devnode* proto_node(uint32_t protocol_id);

 private:
  friend struct Devnode;

  uint64_t next_ino = 1;

  Devnode* root_devnode_;

  Devnode class_devnode;

  // Dummy node to represent dev/diagnostics directory.
  Devnode diagnostics_devnode;

  // Dummy node to represent dev/null directory.
  Devnode null_devnode;

  // Dummy node to represent dev/zero directory.
  Devnode zero_devnode;

  // Connection to diagnostics VFS server.
  const std::optional<fidl::ClientEnd<fuchsia_io::Directory>> diagnostics_;

  struct ProtocolInfo {
    std::string_view name;
    uint32_t id;
    uint32_t flags;
  };

  static constexpr ProtocolInfo proto_infos[] = {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) {name, val, flags},
#include <lib/ddk/protodefs.h>
  };

  std::unordered_map<uint32_t, Devnode> proto_info_nodes;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_
