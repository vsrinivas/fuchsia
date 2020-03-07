// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_H_

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

#include "async_loop_owned_rpc_handler.h"
#include "devfs_connection.h"
#include "driver_host_context.h"
#include "lock.h"
#include "zx_device.h"

namespace internal {
namespace fuchsia = ::llcpp::fuchsia;

struct BindContext {
  fbl::RefPtr<zx_device_t> parent;
  fbl::RefPtr<zx_device_t> child;
};

struct CreationContext {
  fbl::RefPtr<zx_device_t> parent;
  fbl::RefPtr<zx_device_t> child;
  zx::unowned_channel device_controller_rpc;
  zx::unowned_channel coordinator_rpc;
};

void set_bind_context(internal::BindContext* ctx);
void set_creation_context(internal::CreationContext* ctx);

}  // namespace internal

// Nothing outside of devmgr/{devmgr,driver_host,rpc-device}.c
// should be calling internal::*() APIs, as this could
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

  zx_status_t BindOp(internal::BindContext* bind_context,
                     const fbl::RefPtr<zx_device_t>& device) const {
    fbl::StringBuffer<32> trace_label;
    trace_label.AppendPrintf("%s:bind", name_);
    TRACE_DURATION("driver_host:driver-hooks", trace_label.data());

    internal::set_bind_context(bind_context);
    auto status = ops_->bind(ctx_, device.get());
    internal::set_bind_context(nullptr);
    return status;
  }

  zx_status_t CreateOp(internal::CreationContext* creation_context,
                       const fbl::RefPtr<zx_device_t>& parent, const char* name, const char* args,
                       zx_handle_t rpc_channel) const {
    internal::set_creation_context(creation_context);
    auto status = ops_->create(ctx_, parent.get(), name, args, rpc_channel);
    internal::set_creation_context(nullptr);
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

extern zx_protocol_device_t device_default_ops;

namespace internal {

// |client_remote| will only be a valid handle if the device was added with
// DEVICE_ADD_INVISIBLE or DEVICE_ADD_MUST_ISOLATE.
zx_status_t device_add(const fbl::RefPtr<zx_device_t>& dev, const fbl::RefPtr<zx_device_t>& parent,
                       const zx_device_prop_t* props, uint32_t prop_count, const char* proxy_args,
                       zx::channel client_remote) REQ_DM_LOCK;
zx_status_t device_init(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
void device_init_reply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                       const device_init_reply_args_t* args) REQ_DM_LOCK;
// TODO(fxb/34574): this should be removed once device_remove() is removed.
zx_status_t device_remove_deprecated(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t device_remove(const fbl::RefPtr<zx_device_t>& dev,
                          bool unbind_self = false) REQ_DM_LOCK;
void device_unbind_reply(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
void device_suspend_reply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                          uint8_t out_state) REQ_DM_LOCK;
void device_resume_reply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                         uint8_t out_power_state, uint32_t out_perf_state) REQ_DM_LOCK;
zx_status_t device_bind(const fbl::RefPtr<zx_device_t>& dev, const char* drv_libname) REQ_DM_LOCK;
zx_status_t device_rebind(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t device_unbind(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t device_complete_removal(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
zx_status_t device_run_compatibility_tests(const fbl::RefPtr<zx_device_t>& dev,
                                           int64_t hook_wait_time) REQ_DM_LOCK;
zx_status_t device_create(zx_driver_t* drv, const char* name, void* ctx,
                          const zx_protocol_device_t* ops,
                          fbl::RefPtr<zx_device_t>* out) REQ_DM_LOCK;
zx_status_t device_open(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                        uint32_t flags) REQ_DM_LOCK;
zx_status_t device_close(fbl::RefPtr<zx_device_t> dev, uint32_t flags) REQ_DM_LOCK;
void device_system_suspend(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags) REQ_DM_LOCK;
void device_suspend_new(const fbl::RefPtr<zx_device_t>& dev,
                        ::llcpp::fuchsia::device::DevicePowerState requested_state);
zx_status_t device_set_performance_state(const fbl::RefPtr<zx_device_t>& dev,
                                         uint32_t requested_state, uint32_t* out_state);
zx_status_t device_configure_auto_suspend(
    const fbl::RefPtr<zx_device_t>& dev, bool enable,
    ::llcpp::fuchsia::device::DevicePowerState requested_state);
void device_system_resume(const fbl::RefPtr<zx_device_t>& dev,
                          uint32_t target_system_state) REQ_DM_LOCK;
void device_resume_new(const fbl::RefPtr<zx_device_t>& dev);
void device_destroy(zx_device_t* dev) REQ_DM_LOCK;

zx_status_t load_firmware(const fbl::RefPtr<zx_device_t>& dev, const char* path, zx_handle_t* fw,
                          size_t* size) REQ_DM_LOCK;
zx_status_t get_topo_path(const fbl::RefPtr<zx_device_t>& dev, char* path, size_t max,
                          size_t* actual);

zx_status_t get_metadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type, void* buf,
                         size_t buflen, size_t* actual) REQ_DM_LOCK;

zx_status_t get_metadata_size(const fbl::RefPtr<zx_device_t>& dev, uint32_t type,
                              size_t* size) REQ_DM_LOCK;

zx_status_t add_metadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type, const void* data,
                         size_t length) REQ_DM_LOCK;

zx_status_t publish_metadata(const fbl::RefPtr<zx_device_t>& dev, const char* path, uint32_t type,
                             const void* data, size_t length) REQ_DM_LOCK;

zx_status_t device_add_composite(const fbl::RefPtr<zx_device_t>& dev, const char* name,
                                 const composite_device_desc_t* comp_desc) REQ_DM_LOCK;

zx_status_t schedule_work(const fbl::RefPtr<zx_device_t>& dev, void (*callback)(void*),
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
  void CreateDevice(zx::channel coordinator_rpc, zx::channel device_controller_rpc,
                    ::fidl::StringView driver_path, ::zx::vmo driver, ::zx::handle parent_proxy,
                    ::fidl::StringView proxy_args, uint64_t local_device_id,
                    CreateDeviceCompleter::Sync completer) override;
  void CreateCompositeDevice(zx::channel coordinator_rpc, zx::channel device_controller_rpc,
                             ::fidl::VectorView<uint64_t> components, ::fidl::StringView name,
                             uint64_t local_device_id,
                             CreateCompositeDeviceCompleter::Sync completer) override;
  void CreateDeviceStub(zx::channel coordinator_rpc, zx::channel device_controller_rpc,
                        uint32_t protocol_id, uint64_t local_device_id,
                        CreateDeviceStubCompleter::Sync completer) override;
};

zx_status_t fidl_handler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie);

// Attaches channel |c| to new state representing an open connection to |dev|.
zx_status_t device_connect(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags, zx::channel c);

zx_status_t start_connection(fbl::RefPtr<DevfsConnection> ios, zx::channel h);

// routines driver_host uses to talk to dev coordinator
// |client_remote| will only be a valid handle if the device was added with
// DEVICE_ADD_INVISIBLE or DEVICE_ADD_MUST_ISOLATE.
zx_status_t add(const fbl::RefPtr<zx_device_t>& dev, const fbl::RefPtr<zx_device_t>& child,
                const char* proxy_args, const zx_device_prop_t* props, uint32_t prop_count,
                zx::channel client_remote) REQ_DM_LOCK;
// Note that remove() takes a RefPtr rather than a const RefPtr&.
// It intends to consume a reference.
zx_status_t remove(fbl::RefPtr<zx_device_t> dev) REQ_DM_LOCK;
zx_status_t schedule_remove(const fbl::RefPtr<zx_device_t>& dev, bool unbind_self) REQ_DM_LOCK;
zx_status_t schedule_unbind_children(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
void make_visible(const fbl::RefPtr<zx_device_t>& dev, const device_make_visible_args_t* args);

// State that is shared between the zx_device implementation and driver_host-core.cpp
void finalize() REQ_DM_LOCK;
extern fbl::DoublyLinkedList<zx_device*, zx_device::DeferNode> defer_device_list USE_DM_LOCK;
extern int enumerators USE_DM_LOCK;

// Lookup the a driver by name, and if it's not found, install the given vmo as
// that driver.
zx_status_t find_driver(fbl::StringPiece libname, zx::vmo vmo, fbl::RefPtr<zx_driver_t>* out);

// Construct a string describing the path of |dev| relative to its most
// distant ancestor in this driver_host.
const char* mkdevpath(const fbl::RefPtr<zx_device_t>& dev, char* path, size_t max);

// Retrieve the singleton Devhost context.
DevhostContext& DevhostCtx();

// Retrieve the singleton async loop
async::Loop* DevhostAsyncLoop();

}  // namespace internal

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_H_
