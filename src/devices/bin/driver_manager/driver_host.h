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
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

using LoaderServiceConnector = fit::function<zx_status_t(zx::channel*)>;
class Coordinator;

struct DriverHostConfig {
  const char* name;
  const char* binary;
  const char* const* env;

  const zx::unowned_job job;
  const zx::unowned_resource root_resource;

  const LoaderServiceConnector* loader_service_connector;
  FsProvider* fs_provider;

  Coordinator* coordinator;
};

class DriverHost : public fbl::RefCounted<DriverHost>,
                   public fbl::DoublyLinkedListable<DriverHost*> {
 public:
  enum Flags : uint32_t {
    kDying = 1 << 0,
    kSuspend = 1 << 1,
  };

  // This constructor is public so that tests can create DriverHosts without launching processes.
  // The main program logic will want to use DriverHost::Launch
  // |coordinator| must outlive this DriverHost object.
  // |rpc| is a client channel speaking fuchsia.device.manager/DriverHostController
  // |diagnostics| is a client to driver host diagnostics directory
  // |proc| is a handle to the driver_host process this DriverHost tracks.
  DriverHost(Coordinator* coordinator,
             fidl::ClientEnd<fuchsia_device_manager::DevhostController> controller,
             fidl::ClientEnd<fuchsia_io::Directory> diagnostics, zx::process proc);
  ~DriverHost();

  // |coordinator| must outlive this DriverHost object. If |loader_conector| is nullptr, the
  // default loader service is used, which is useful in test environments.
  static zx_status_t Launch(const DriverHostConfig& config, fbl::RefPtr<DriverHost>* out);

  fidl::WireClient<fuchsia_device_manager::DevhostController>& controller() {
    return controller_;
  }
  zx::unowned_process proc() const { return zx::unowned_process(proc_); }
  zx_koid_t koid() const { return koid_; }
  // Note: this is a non-const reference to make |= etc. ergonomic.
  uint32_t& flags() { return flags_; }
  fbl::TaggedDoublyLinkedList<Device*, Device::DriverHostListTag>& devices() { return devices_; }

  // Returns a device id that will be unique within this driver_host.
  uint64_t new_device_id() { return next_device_id_++; }

 private:
  Coordinator* coordinator_;

  fidl::WireClient<fuchsia_device_manager::DevhostController> controller_;
  zx::process proc_;
  zx_koid_t koid_ = 0;
  uint32_t flags_ = 0;

  // The next ID to be allocated to a device in this driver_host.  Skip 0 to make
  // an uninitialized value more obvious.
  uint64_t next_device_id_ = 1;

  // list of all devices on this driver_host
  fbl::TaggedDoublyLinkedList<Device*, Device::DriverHostListTag> devices_;

  // Holding reference to driver host inspect directory so that it will not be freed while in use
  fbl::RefPtr<fs::PseudoDir> driver_host_dir_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_HOST_H_
