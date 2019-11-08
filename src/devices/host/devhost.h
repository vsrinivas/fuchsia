// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_HOST_DEVHOST_H_
#define SRC_DEVICES_HOST_DEVHOST_H_

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/fidl.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

#include "async-loop-owned-rpc-handler.h"
#include "devhost-context.h"
#include "lock.h"
#include "zx-device.h"

namespace devmgr {

namespace fuchsia = ::llcpp::fuchsia;

struct BindContext {
  fbl::RefPtr<zx_device_t> parent;
  fbl::RefPtr<zx_device_t> child;
};

struct CreationContext {
  fbl::RefPtr<zx_device_t> parent;
  fbl::RefPtr<zx_device_t> child;
  zx::unowned_channel rpc;
};

void devhost_set_bind_context(BindContext* ctx);
void devhost_set_creation_context(CreationContext* ctx);

}  // namespace devmgr

// Nothing outside of devmgr/{devmgr,devhost,rpc-device}.c
// should be calling devhost_*() APIs, as this could
// violate the internal locking design.

// Safe external APIs are in device.h and device_internal.h

// Note that this must be a struct to match the public opaque declaration.
struct zx_driver : fbl::DoublyLinkedListable<fbl::RefPtr<zx_driver>>, fbl::RefCounted<zx_driver> {
  static zx_status_t Create(fbl::RefPtr<zx_driver>* out_driver);

  const char* name() const { return name_; }

  zx_driver_rec_t* driver_rec() const { return driver_rec_; }

  zx_status_t status() const { return status_; }

  const fbl::String& libname() const { return libname_; }

  void set_name(const char* name) { name_ = name; }

  void set_driver_rec(zx_driver_rec_t* driver_rec) { driver_rec_ = driver_rec; }

  void set_ops(const zx_driver_ops_t* ops) { ops_ = ops; }

  void set_status(zx_status_t status) { status_ = status; }

  void set_libname(fbl::StringPiece libname) { libname_ = libname; }

  // Interface to |ops|. These names contain Op in order to not
  // collide with e.g. RefPtr names.

  bool has_init_op() const { return ops_->init != nullptr; }

  bool has_bind_op() const { return ops_->bind != nullptr; }

  bool has_create_op() const { return ops_->create != nullptr; }

  bool has_run_unit_tests_op() const { return ops_->run_unit_tests != nullptr; }

  zx_status_t InitOp() { return ops_->init(&ctx_); }

  zx_status_t BindOp(devmgr::BindContext* bind_context,
                     const fbl::RefPtr<zx_device_t>& device) const {
    devmgr::devhost_set_bind_context(bind_context);
    auto status = ops_->bind(ctx_, device.get());
    devmgr::devhost_set_bind_context(nullptr);
    return status;
  }

  zx_status_t CreateOp(devmgr::CreationContext* creation_context,
                       const fbl::RefPtr<zx_device_t>& parent, const char* name, const char* args,
                       zx_handle_t rpc_channel) const {
    devmgr::devhost_set_creation_context(creation_context);
    auto status = ops_->create(ctx_, parent.get(), name, args, rpc_channel);
    devmgr::devhost_set_creation_context(nullptr);
    return status;
  }

  void ReleaseOp() const {
    // TODO(kulakowski/teisenbe) Consider poisoning the ops_ table on release.
    ops_->release(ctx_);
  }

  bool RunUnitTestsOp(const fbl::RefPtr<zx_device_t>& parent, zx::channel test_output) const {
    return ops_->run_unit_tests(ctx_, parent.get(), test_output.release());
  }

 private:
  friend std::unique_ptr<zx_driver> std::make_unique<zx_driver>();
  zx_driver() = default;

  const char* name_ = nullptr;
  zx_driver_rec_t* driver_rec_ = nullptr;
  const zx_driver_ops_t* ops_ = nullptr;
  void* ctx_ = nullptr;
  fbl::String libname_;
  zx_status_t status_ = ZX_OK;
};

namespace devmgr {

extern zx_protocol_device_t device_default_ops;

// |client_remote| will only be a valid handle if the device was added with
// DEVICE_ADD_INVISIBLE or DEVICE_ADD_MUST_ISOLATE.
zx_status_t devhost_device_add(const fbl::RefPtr<zx_device_t>& dev,
                               const fbl::RefPtr<zx_device_t>& parent,
                               const zx_device_prop_t* props, uint32_t prop_count,
                               const char* proxy_args, zx::channel client_remote) REQ_DM_LOCK;
// TODO(fxb/34574): this should be removed once device_remove() is removed.
zx_status_t devhost_device_remove_deprecated(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t devhost_device_remove(const fbl::RefPtr<zx_device_t>& dev,
                                  bool unbind_self = false) REQ_DM_LOCK;
void devhost_device_unbind_reply(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t devhost_device_bind(const fbl::RefPtr<zx_device_t>& dev,
                                const char* drv_libname) REQ_DM_LOCK;
zx_status_t devhost_device_rebind(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t devhost_device_unbind(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t devhost_device_complete_removal(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t devhost_device_run_compatibility_tests(const fbl::RefPtr<zx_device_t>& dev,
                                                   int64_t hook_wait_time) REQ_DM_LOCK;
zx_status_t devhost_device_create(zx_driver_t* drv, const char* name, void* ctx,
                                  const zx_protocol_device_t* ops,
                                  fbl::RefPtr<zx_device_t>* out) REQ_DM_LOCK;
zx_status_t devhost_device_open(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                                uint32_t flags) REQ_DM_LOCK;
zx_status_t devhost_device_close(fbl::RefPtr<zx_device_t> dev, uint32_t flags) REQ_DM_LOCK;
zx_status_t devhost_device_suspend(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags) REQ_DM_LOCK;
zx_status_t devhost_device_suspend_new(const fbl::RefPtr<zx_device_t>& dev,
                                       fuchsia_device_DevicePowerState requested_state,
                                       fuchsia_device_DevicePowerState* out_state);
zx_status_t devhost_device_set_performance_state(const fbl::RefPtr<zx_device_t>& dev,
                                                 uint32_t requested_state, uint32_t* out_state);
zx_status_t devhost_device_resume(const fbl::RefPtr<zx_device_t>& dev,
                                  uint32_t target_system_state) REQ_DM_LOCK;
zx_status_t devhost_device_resume_new(const fbl::RefPtr<zx_device_t>& dev,
                                      fuchsia_device_DevicePowerState requested_state,
                                      fuchsia_device_DevicePowerState* out_state);
void devhost_device_destroy(zx_device_t* dev) REQ_DM_LOCK;

zx_status_t devhost_load_firmware(const fbl::RefPtr<zx_device_t>& dev, const char* path,
                                  zx_handle_t* fw, size_t* size) REQ_DM_LOCK;
zx_status_t devhost_get_topo_path(const fbl::RefPtr<zx_device_t>& dev, char* path, size_t max,
                                  size_t* actual);

zx_status_t devhost_get_metadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type, void* buf,
                                 size_t buflen, size_t* actual) REQ_DM_LOCK;

zx_status_t devhost_get_metadata_size(const fbl::RefPtr<zx_device_t>& dev, uint32_t type,
                                      size_t* size) REQ_DM_LOCK;

zx_status_t devhost_add_metadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type,
                                 const void* data, size_t length) REQ_DM_LOCK;

zx_status_t devhost_publish_metadata(const fbl::RefPtr<zx_device_t>& dev, const char* path,
                                     uint32_t type, const void* data, size_t length) REQ_DM_LOCK;

zx_status_t devhost_device_add_composite(const fbl::RefPtr<zx_device_t>& dev, const char* name,
                                         const composite_device_desc_t* comp_desc) REQ_DM_LOCK;

zx_status_t devhost_schedule_work(const fbl::RefPtr<zx_device_t>& dev, void (*callback)(void*),
                                  void* cookie) REQ_DM_LOCK;

class DevhostControllerConnection : public AsyncLoopOwnedRpcHandler<DevhostControllerConnection>,
                                    public fuchsia::device::manager::DevhostController::Interface {
 public:
  DevhostControllerConnection() = default;

  static void HandleRpc(std::unique_ptr<DevhostControllerConnection> conn,
                        async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
  zx_status_t HandleRead();

 private:
  void CreateDevice(zx::channel rpc, ::fidl::StringView driver_path, ::zx::vmo driver,
                    ::zx::handle parent_proxy, ::fidl::StringView proxy_args,
                    uint64_t local_device_id, CreateDeviceCompleter::Sync completer) override;
  void CreateCompositeDevice(zx::channel rpc, ::fidl::VectorView<uint64_t> components,
                             ::fidl::StringView name, uint64_t local_device_id,
                             CreateCompositeDeviceCompleter::Sync completer) override;
  void CreateDeviceStub(zx::channel rpc, uint32_t protocol_id, uint64_t local_device_id,
                        CreateDeviceStubCompleter::Sync completer) override;
};

struct DevfsConnection : AsyncLoopOwnedRpcHandler<DevfsConnection> {
  DevfsConnection() = default;

  static void HandleRpc(std::unique_ptr<DevfsConnection> conn, async_dispatcher_t* dispatcher,
                        async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

  fbl::RefPtr<zx_device_t> dev;
  size_t io_off = 0;
  uint32_t flags = 0;
};

zx_status_t devhost_fidl_handler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie);

// Attaches channel |c| to new state representing an open connection to |dev|.
zx_status_t devhost_device_connect(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags,
                                   zx::channel c);

zx_status_t devhost_start_connection(std::unique_ptr<DevfsConnection> ios, zx::channel h);

// routines devhost uses to talk to dev coordinator
// |client_remote| will only be a valid handle if the device was added with
// DEVICE_ADD_INVISIBLE or DEVICE_ADD_MUST_ISOLATE.
zx_status_t devhost_add(const fbl::RefPtr<zx_device_t>& dev, const fbl::RefPtr<zx_device_t>& child,
                        const char* proxy_args, const zx_device_prop_t* props, uint32_t prop_count,
                        zx::channel client_remote) REQ_DM_LOCK;
// Note that devhost_remove() takes a RefPtr rather than a const RefPtr&.
// It intends to consume a reference.
zx_status_t devhost_remove(fbl::RefPtr<zx_device_t> dev) REQ_DM_LOCK;
zx_status_t devhost_send_unbind_done(const fbl::RefPtr<zx_device_t>& dev);
zx_status_t devhost_schedule_remove(const fbl::RefPtr<zx_device_t>& dev,
                                    bool unbind_self) REQ_DM_LOCK;
zx_status_t devhost_schedule_unbind_children(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
void devhost_make_visible(const fbl::RefPtr<zx_device_t>& dev);

// State that is shared between the zx_device implementation and devhost-core.cpp
void devhost_finalize() REQ_DM_LOCK;
extern fbl::DoublyLinkedList<zx_device*, zx_device::DeferNode> defer_device_list USE_DM_LOCK;
extern int devhost_enumerators USE_DM_LOCK;

// Lookup the a driver by name, and if it's not found, install the given vmo as
// that driver.
zx_status_t dh_find_driver(fbl::StringPiece libname, zx::vmo vmo, fbl::RefPtr<zx_driver_t>* out);

// Construct a string describing the path of |dev| relative to its most
// distant ancestor in this devhost.
const char* mkdevpath(const fbl::RefPtr<zx_device_t>& dev, char* path, size_t max);

// Retrieve the singleton Devhost context.
DevhostContext& DevhostCtx();

// Retrieve the singleton async loop
async::Loop* DevhostAsyncLoop();

}  // namespace devmgr

#endif  // SRC_DEVICES_HOST_DEVHOST_H_
