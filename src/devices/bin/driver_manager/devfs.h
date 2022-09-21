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

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

class Devfs;
class Device;
class DcIostate;
struct Watcher;

struct Devnode : public fbl::DoublyLinkedListable<Devnode*> {
  // `parent` and `device` must outlive `this`.
  Devnode(Devfs& devfs, Devnode* parent, Device* device, fbl::String name);
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

  void notify(std::string_view name, fuchsia_io::wire::WatchEvent op);

  // This method is exposed for testing. It returns true if the devfs has active watchers.
  bool has_watchers() const;

  Device* device() const;
  Devnode* parent() const;
  std::string_view name() const;

  // Set if this devnode is attached to a remote service.
  fidl::ClientEnd<fuchsia_io::Directory> service_dir;
  std::string service_path;
  fuchsia_device_fs::wire::ExportOptions service_options;

  fbl::DoublyLinkedList<std::unique_ptr<Watcher>> watchers;

  // list of our child devnodes
  fbl::DoublyLinkedList<Devnode*> children;

  // list of attached iostates
  fbl::DoublyLinkedList<DcIostate*> iostate;

 private:
  friend class DcIostate;
  friend class Devfs;

  // A devnode is a directory (from stat's perspective) if it has children, or
  // if it doesn't have a device, or if its device has no rpc handle.
  bool is_dir() const;
  bool is_invisible() const;

  // Local devnodes are ones that we should not hand off OPEN RPCs to the
  // underlying driver_host.
  bool is_local() const;
  bool is_special_ino(uint64_t ino) const;

  Devnode* lookup(std::string_view name);

  void open(async_dispatcher_t* dispatcher, fidl::ServerEnd<fuchsia_io::Node> ipc,
            std::string_view path, fuchsia_io::OpenFlags flags);

  zx_status_t readdir(uint64_t* ino_inout, void* data, size_t len);

  zx_status_t seq_name(char* data, size_t size);

  Devfs& devfs_;

  // Pointer to our parent, for removing ourselves from its list of
  // children. Our parent must outlive us.
  Devnode* parent_;

  // nullptr if we are a pure directory node,
  // otherwise the device we are referencing
  Device* device_;

  const fbl::String name_;
  const uint64_t ino_;

  // used to assign unique small device numbers
  // for class device links
  uint32_t seqcount_ = 0;
};

zx_status_t devfs_connect(const Device* dev, fidl::ServerEnd<fuchsia_io::Node> client_remote);

class Devfs {
 public:
  // `device` must outlive `this`.
  explicit Devfs(Device* device);

  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> Connect(async_dispatcher_t* dispatcher);

  zx_status_t publish(Device& parent, Device& dev);
  void advertise(Device& device);
  void advertise_modified(Device& dev);
  void connect_diagnostics(fidl::ClientEnd<fuchsia_io::Directory> diagnostics_channel);

  // Exports `service_path` from `service_dir` to `devfs_path`, under `dn`. If
  // `protocol_id` matches a known protocol, `service_path` will also be exposed
  // under a class path.
  //
  // Every Devnode that is created during the export is stored within `out`. As
  // each of these Devnodes are children of `dn`, they must live as long as `dn`.
  zx_status_t export_dir(Devnode* dn, fidl::ClientEnd<fuchsia_io::Directory> service_dir,
                         std::string_view service_path, std::string_view devfs_path,
                         uint32_t protocol_id, fuchsia_device_fs::wire::ExportOptions options,
                         std::vector<std::unique_ptr<Devnode>>& out);

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
  std::optional<fidl::ClientEnd<fuchsia_io::Directory>> diagnostics_channel;

  struct ProtocolInfo {
    const char* name;
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
