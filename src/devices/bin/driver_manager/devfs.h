// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

class Device;
class DcIostate;
struct Watcher;

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

  // Set if this devnode is attached to a remote service.
  fidl::ClientEnd<fuchsia_io::Node> service_node;

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

// Initializes a devfs directory from `device`.
// This library is NOT thread safe. `dispatcher` must be a single threaded dispatcher, and all
// callbacks from the dispatcher should be run on the thread that calls `devfs_init`.
void devfs_init(const fbl::RefPtr<Device>& device, async_dispatcher_t* dispatcher);

// Watches the devfs directory `dn`, and sends events to `watcher`.
zx_status_t devfs_watch(Devnode* dn, zx::channel h, uint32_t mask);

// Borrows the channel connected to the root of devfs.
zx::unowned_channel devfs_root_borrow();

// Clones the channel connected to the root of devfs.
zx::channel devfs_root_clone();

zx_status_t devfs_publish(const fbl::RefPtr<Device>& parent, const fbl::RefPtr<Device>& dev);
void devfs_unpublish(Device* dev);
void devfs_advertise(const fbl::RefPtr<Device>& dev);
void devfs_advertise_modified(const fbl::RefPtr<Device>& dev);
zx_status_t devfs_connect(const Device* dev, fidl::ServerEnd<fuchsia_io::Node> client_remote);
void devfs_connect_diagnostics(fidl::UnownedClientEnd<fuchsia_io::Directory> diagnostics_channel);

// This method is exposed for testing.  It walks the devfs from the given node,
// traversing the given sub-path.
// If ZX_OK is returned, then *device_out refers to the device at the given path
// relative to the devnode.
zx_status_t devfs_walk(Devnode* dn, const char* path, fbl::RefPtr<Device>* device_out);

// Exports `service_node` to `devfs_path`, under `dn`. If `protocol_id` matches
// a known protocol, `service_node` will also be exposed under a class path.
//
// Every Devnode that is created during the export is stored within `out`. As
// each of these Devnodes are children of `dn`, they must live as long as `dn`.
zx_status_t devfs_export(Devnode* dn, fidl::ClientEnd<fuchsia_io::Node> service_node,
                         std::string_view devfs_path, uint32_t protocol_id,
                         std::vector<std::unique_ptr<Devnode>>& out);

// This method is exposed for testing. It returns true if the devfs has active watchers.
bool devfs_has_watchers(Devnode* dn);

// For testing only.
void devfs_prepopulate_class(Devnode* dn);
Devnode* devfs_proto_node(uint32_t protocol_id);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_
