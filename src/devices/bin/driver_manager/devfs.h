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
  Devnode(Devfs& devfs, const fbl::RefPtr<Device>& dev, std::string_view name);
  ~Devnode();

  Devnode(const Devnode&) = delete;
  Devnode& operator=(const Devnode&) = delete;

  Devnode(Devnode&&) = delete;
  Devnode& operator=(Devnode&&) = delete;

  // Watches the devfs directory `dn`, and sends events to `server_end`.
  zx_status_t watch(async_dispatcher_t* dispatcher,
                    fidl::ServerEnd<fuchsia_io::DirectoryWatcher> server_end,
                    fuchsia_io::wire::WatchMask mask);

  fbl::String name;
  const uint64_t ino = 0;

  // nullptr if we are a pure directory node,
  // otherwise the device we are referencing
  Device* device;

  zx::status<Devnode*> walk(std::string_view path);

  void notify(std::string_view name, fuchsia_io::wire::WatchEvent op);

  // This method is exposed for testing. It returns true if the devfs has active watchers.
  bool has_watchers() const;

  // Set if this devnode is attached to a remote service.
  fidl::ClientEnd<fuchsia_io::Directory> service_dir;
  std::string service_path;
  fuchsia_device_fs::wire::ExportOptions service_options;

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

 private:
  friend class DcIostate;
  friend class Devfs;

  bool is_special_ino(uint64_t ino) const;
  bool is_invisible() const;

  void open(async_dispatcher_t* dispatcher, fidl::ServerEnd<fuchsia_io::Node> ipc,
            std::string_view path, fuchsia_io::OpenFlags flags);

  zx_status_t readdir(uint64_t* ino_inout, void* data, size_t len);

  const Devfs& devfs_;
};

zx_status_t devfs_connect(const Device* dev, fidl::ServerEnd<fuchsia_io::Node> client_remote);

class Devfs {
 public:
  // Initializes a devfs directory from `device`.
  explicit Devfs(const fbl::RefPtr<Device>& device);

  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> Connect(async_dispatcher_t* dispatcher);

  zx_status_t publish(const fbl::RefPtr<Device>& parent, const fbl::RefPtr<Device>& dev);
  void unpublish(Device* dev);
  void advertise(const fbl::RefPtr<Device>& dev);
  void advertise_modified(const fbl::RefPtr<Device>& dev);
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
  Devnode* proto_node(uint32_t protocol_id) const;

 private:
  friend struct Devnode;

  std::unique_ptr<Devnode> mkdir(Devnode* parent, std::string_view name);

  uint64_t next_ino = 1;

  Devnode* root_devnode_;

  std::unique_ptr<Devnode> class_devnode;

  // Dummy node to represent dev/diagnostics directory.
  std::unique_ptr<Devnode> diagnostics_devnode;

  // Dummy node to represent dev/null directory.
  std::unique_ptr<Devnode> null_devnode;

  // Dummy node to represent dev/zero directory.
  std::unique_ptr<Devnode> zero_devnode;

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

  std::unordered_map<uint32_t, std::unique_ptr<Devnode>> proto_info_nodes;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_
