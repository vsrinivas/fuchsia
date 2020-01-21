// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_DRIVER_MANAGER_DEVFS_H_
#define SRC_DEVICES_DRIVER_MANAGER_DEVFS_H_

#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>

namespace devmgr {

class Device;
struct Devnode;

// Initializes a devfs directory from |device|.
void devfs_init(const fbl::RefPtr<Device>& device, async_dispatcher_t* dispatcher);

// Watches the devfs directory |dn|, and sends events to |watcher|.
zx_status_t devfs_watch(Devnode* dn, zx::channel h, uint32_t mask);

// Borrows the channel connected to the root of devfs.
zx::unowned_channel devfs_root_borrow();

// Clones the channel connected to the root of devfs.
zx::channel devfs_root_clone();

zx_status_t devfs_publish(const fbl::RefPtr<Device>& parent, const fbl::RefPtr<Device>& dev);
void devfs_unpublish(Device* dev);
void devfs_advertise(const fbl::RefPtr<Device>& dev);
void devfs_advertise_modified(const fbl::RefPtr<Device>& dev);
zx_status_t devfs_connect(const Device* dev, zx::channel client_remote);

// This method is exposed for testing.  It walks the devfs from the given node,
// traversing the given sub-path.
// If ZX_OK is returned, then *device_out refers to the device at the given path
// relative to the devnode.
zx_status_t devfs_walk(Devnode* dn, const char* path, fbl::RefPtr<Device>* device_out);

}  // namespace devmgr

#endif  // SRC_DEVICES_DRIVER_MANAGER_DEVFS_H_
