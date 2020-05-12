// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_HOST_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_HOST_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>

#include <utility>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "device.h"
#include "fdio.h"

class Coordinator;

class DriverHost : public fbl::RefCounted<DriverHost>,
                   public fbl::DoublyLinkedListable<DriverHost*> {
 public:
  using LoaderServiceConnector = fit::function<zx_status_t(zx::channel*)>;

  enum Flags : uint32_t {
    kDying = 1 << 0,
    kSuspend = 1 << 1,
  };

  // This constructor is public so that tests can create DriverHosts without launching processes.
  // The main program logic will want to use DriverHost::Launch
  // |coordinator| must outlive this DriverHost object.
  // |rpc| is a client channel speaking fuchsia.device.manager/DriverHostController
  // |proc| is a handle to the driver_host process this DriverHost tracks.
  DriverHost(Coordinator* coordinator, zx::channel rpc, zx::process proc);
  ~DriverHost();

  // |coordinator| must outlive this DriverHost object.
  static zx_status_t Launch(Coordinator* coordinator,
                            const LoaderServiceConnector& loader_connector,
                            const char* driver_host_bin, const char* proc_name,
                            const char* const* proc_env, const zx::resource& root_resource,
                            zx::unowned_job driver_host_job, FsProvider* fs_provider,
                            fbl::RefPtr<DriverHost>* out);

  const zx::channel& hrpc() const { return hrpc_; }
  zx::unowned_process proc() const { return zx::unowned_process(proc_); }
  zx_koid_t koid() const { return koid_; }
  // Note: this is a non-const reference to make |= etc. ergonomic.
  uint32_t& flags() { return flags_; }
  fbl::TaggedDoublyLinkedList<Device*, Device::DriverHostListTag>& devices() { return devices_; }

  // Returns a device id that will be unique within this driver_host.
  uint64_t new_device_id() { return next_device_id_++; }

 private:
  Coordinator* coordinator_;

  zx::channel hrpc_;
  zx::process proc_;
  zx_koid_t koid_ = 0;
  uint32_t flags_ = 0;

  // The next ID to be allocated to a device in this driver_host.  Skip 0 to make
  // an uninitialized value more obvious.
  uint64_t next_device_id_ = 1;

  // list of all devices on this driver_host
  fbl::TaggedDoublyLinkedList<Device*, Device::DriverHostListTag> devices_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_HOST_H_
